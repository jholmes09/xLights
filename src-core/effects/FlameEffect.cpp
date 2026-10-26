/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "../../include/flame-16.xpm"
#include "../../include/flame-24.xpm"
#include "../../include/flame-32.xpm"
#include "../../include/flame-48.xpm"
#include "../../include/flame-64.xpm"

#include "FlameEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <spdlog/fmt/fmt.h>

#include "../graphics/xlGraphicsAccumulators.h"
#include "../models/DMX/DmxFlame.h"
#include "../models/DMX/FlameFixture.h"
#include "../models/ModelGroup.h"
#include "../render/Effect.h"
#include "../render/EffectLayer.h"
#include "../render/Element.h"
#include "../render/RenderBuffer.h"
#include "../render/RenderContext.h"
#include "../render/SequenceElements.h"
#include "../render/ValueCurve.h"
#include "UtilClasses.h"
#include "UtilFunctions.h"

namespace {
    int AlignUp(int ms, int frameMs) {
        if (frameMs <= 0) return ms;
        return ((ms + frameMs - 1) / frameMs) * frameMs;
    }
    int AlignDown(int ms, int frameMs) {
        if (frameMs <= 0) return ms;
        return (ms / frameMs) * frameMs;
    }
    // Small deterministic generator: the same unit and effect always get the
    // same "random" shots, so renders and scrubbing never change.
    struct FlameRng {
        uint64_t state;
        explicit FlameRng(uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
        uint64_t Next() {
            uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }
        double Unit() { return (double)(Next() >> 11) / 9007199254740992.0; } // [0,1)
    };
    uint64_t HashName(const std::string& s, uint64_t seed) {
        uint64_t h = 1469598103934665603ULL ^ seed;
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    }
    int AlignNearest(int ms, int frameMs) {
        if (frameMs <= 0) return ms;
        return ((ms + frameMs / 2) / frameMs) * frameMs;
    }
}

FlameParams FlameParams::FromSettings(const SettingsMap& s, const std::string& prefix) {
    FlameParams p;
    std::string action = s.Get(prefix + "CHOICE_Flame_Action", "Preset");
    if (action == "Shot") {
        p.action = Action::Shot;
    } else if (action == "Sweep") {
        p.action = Action::Sweep;
    } else {
        p.action = Action::Preset;
    }
    std::string repeat = s.Get(prefix + "CHOICE_Flame_Repeat", "Once");
    if (repeat == "Back-to-back") {
        p.repeat = Repeat::BackToBack;
    } else if (repeat == "On timing marks") {
        p.repeat = Repeat::Marks;
    } else if (repeat == "Random") {
        p.repeat = Repeat::Random;
    } else {
        p.repeat = Repeat::Once;
    }
    p.timingTrack = s.Get(prefix + "CHOICE_Flame_TimingTrack", "");
    p.preset = s.GetInt(prefix + "SPINCTRL_Flame_Preset", 8);
    p.angleDeg = std::clamp(s.GetInt(prefix + "SLIDER_Flame_Angle", 0), FLAME_ANGLE_MIN, FLAME_ANGLE_MAX);
    p.endAngleDeg = std::clamp(s.GetInt(prefix + "SLIDER_Flame_EndAngle", 0), FLAME_ANGLE_MIN, FLAME_ANGLE_MAX);
    p.pattern = ParsePattern(s.Get(prefix + "TEXTCTRL_Flame_Pattern", ""));
    p.angleCurve = s.Get(prefix + "VALUECURVE_Flame_Angle", "");
    p.shotMs = std::max(10, s.GetInt(prefix + "SPINCTRL_Flame_ShotMs", 500));
    p.gapMs = std::max(0, s.GetInt(prefix + "SPINCTRL_Flame_GapMs", 0));
    p.shotMaxMs = std::max(0, s.GetInt(prefix + "SPINCTRL_Flame_ShotMaxMs", 0));
    p.rate = std::clamp(s.GetDouble(prefix + "TEXTCTRL_Flame_Rate", 1.0), 0.05, 20.0);
    p.moveEarly = s.GetBool(prefix + "CHECKBOX_Flame_MoveEarly", false);
    p.fitLength = s.GetBool(prefix + "CHECKBOX_Flame_FitLength", true);
    return p;
}

std::vector<int> FlameParams::ParsePattern(const std::string& text) {
    std::vector<int> r;
    const char* c = text.c_str();
    while (*c) {
        if (*c >= '0' && *c <= '9') {
            char* end = nullptr;
            long v = std::strtol(c, &end, 10);
            if (v > 0 && v < 1000) {
                r.push_back((int)v);
            }
            c = end;
        } else {
            ++c;
        }
    }
    return r;
}

FlameEffect::FlameEffect(int id) :
    RenderableEffect(id, "Flame", flame_16, flame_24, flame_32, flame_48, flame_64) {
}

FlameEffect::~FlameEffect() {
}

const DmxFlame* FlameEffect::FlameModelFor(const Model* m) {
    if (m != nullptr && m->GetDisplayAs() == DisplayAsType::DmxFlame) {
        return static_cast<const DmxFlame*>(m);
    }
    return nullptr;
}

float FlameEffect::IdleDeg(const DmxFlame* model) {
    if (model == nullptr) return 0.0f;
    const auto& fx = model->GetFixture();
    if (!fx.HasAngle()) return 0.0f;
    for (const auto& [ch, v] : fx.idle) {
        if (ch == fx.angleChannel) {
            return fx.ValueToAngle(v);
        }
    }
    return 0.0f;
}

int FlameEffect::ShotLengthMs(const FlameParams& p, const DmxFlame* model) {
    if (model == nullptr) return p.shotMs;
    const auto& fx = model->GetFixture();
    if (p.action == FlameParams::Action::Preset && fx.HasPresets()) {
        if (const auto* pr = fx.FindPreset(p.preset)) {
            return pr->lengthMs;
        }
    }
    return p.shotMs;
}

namespace {
    struct ShotAngles {
        float start = 0.0f;
        float end = 0.0f;
    };

    // The angle slider's value curve, read once per plan. Repeated shots each
    // take the curve's value at their own fire time and hold it.
    struct AngleCurve {
        ValueCurve vc;
        bool active = false;
        int startMs = 0;
        int endMs = 1;
        AngleCurve(const std::string& text, int s, int e) : startMs(s), endMs(std::max(e, s + 1)) {
            if (!text.empty()) {
                vc.SetLimits(FLAME_ANGLE_MIN, FLAME_ANGLE_MAX);
                vc.SetDivisor(1);
                vc.Deserialise(text);
                active = vc.IsActive();
            }
        }
        float At(int ms) {
            float offset = std::clamp((float)(ms - startMs) / (float)(endMs - startMs), 0.0f, 1.0f);
            return vc.GetOutputValueAt(offset, startMs, endMs);
        }
    };

    FlameParams::Action EffectiveAction(const FlameParams& p, const FlameFixture& fx) {
        if (p.action == FlameParams::Action::Preset && !fx.HasPresets()) return FlameParams::Action::Shot;
        if (p.action == FlameParams::Action::Sweep && !fx.HasAngle()) return FlameParams::Action::Shot;
        return p.action;
    }

    ShotAngles AnglesForShot(const FlameParams& p, const DmxFlame* model, int index, const FlamePreset* preset, AngleCurve* curve = nullptr, int atMs = 0) {
        ShotAngles a;
        if (model == nullptr) return a;
        const auto& fx = model->GetFixture();
        if (!fx.HasAngle()) return a;
        switch (EffectiveAction(p, fx)) {
        case FlameParams::Action::Preset:
            if (preset != nullptr && !preset->positions.empty()) {
                a.start = fx.PositionDeg(preset->positions.front());
                a.end = fx.PositionDeg(preset->positions.back());
            }
            return a;
        case FlameParams::Action::Shot:
            if (!p.pattern.empty()) {
                int pos = p.pattern[index % p.pattern.size()];
                a.start = a.end = fx.PositionDeg(pos);
            } else if (curve != nullptr && curve->active) {
                a.start = a.end = curve->At(atMs);
            } else {
                a.start = a.end = (float)p.angleDeg;
            }
            break;
        case FlameParams::Action::Sweep:
            a.start = (float)p.angleDeg;
            a.end = (float)p.endAngleDeg;
            if (index % 2 == 1) std::swap(a.start, a.end); // back-to-back sweeps go back and forth
            break;
        }
        a.start = model->ClampAngle(a.start);
        a.end = model->ClampAngle(a.end);
        return a;
    }
}

int FlameEffect::FirstLeadMs(const FlameParams& p, const DmxFlame* model) {
    if (!p.moveEarly || model == nullptr || !model->GetFixture().HasAngle()) {
        return 0;
    }
    const auto& fx = model->GetFixture();
    const FlamePreset* preset = EffectiveAction(p, fx) == FlameParams::Action::Preset ? fx.FindPreset(p.preset) : nullptr;
    ShotAngles a = AnglesForShot(p, model, 0, preset);
    return fx.TravelMs(IdleDeg(model), a.start);
}

int FlameEffect::FitLengthMs(const FlameParams& p, const DmxFlame* model, int frameMs) {
    if (p.repeat != FlameParams::Repeat::Once || model == nullptr) {
        return 0;
    }
    if (p.action != FlameParams::Action::Preset || !model->GetFixture().HasPresets()) {
        return 0; // a single shot or sweep lasts as long as the effect
    }
    int len = ShotLengthMs(p, model);
    return AlignUp(FirstLeadMs(p, model), frameMs) + AlignUp(len, frameMs);
}

std::vector<int> FlameEffect::CollectMarks(EffectLayer* timing, int startMs, int endMs) {
    std::vector<int> r;
    if (timing == nullptr) return r;
    std::unique_lock<std::recursive_mutex> lock(timing->GetLock());
    for (int i = 0; i < timing->GetEffectCount(); ++i) {
        Effect* e = timing->GetEffect(i);
        if (e == nullptr) continue;
        int t = e->GetStartTimeMS();
        if (t >= startMs && t < endMs) {
            r.push_back(t);
        }
    }
    std::sort(r.begin(), r.end());
    return r;
}

FlamePlan FlameEffect::BuildPlan(const FlameParams& p, const DmxFlame* model, int startMs, int endMs, int frameMs, const std::vector<int>& marks) {
    FlamePlan plan;
    if (frameMs <= 0) frameMs = 25;
    int S = AlignDown(startMs, frameMs);
    int E = AlignDown(endMs, frameMs);
    plan.startMs = S;
    plan.endMs = E;
    if (model == nullptr || E <= S) {
        return plan;
    }
    const auto& fx = model->GetFixture();
    plan.action = EffectiveAction(p, fx);
    if (plan.action == FlameParams::Action::Preset) {
        plan.preset = fx.FindPreset(p.preset);
        if (plan.preset == nullptr) {
            return plan;
        }
    }
    int len = plan.action == FlameParams::Action::Preset ? plan.preset->lengthMs : p.shotMs;
    int lenAligned = std::max(frameMs, AlignUp(len, frameMs));
    plan.shotLenMs = lenAligned;
    const bool canMove = p.moveEarly && fx.HasAngle();
    auto travel = [&](float from, float to) { return canMove ? fx.TravelMs(from, to) : 0; };
    // real travel time, used to aim ahead and to flag shots the nozzle can't reach in time
    auto realTravel = [&](float from, float to) { return fx.HasAngle() ? AlignUp(fx.TravelMs(from, to), frameMs) : 0; };

    float prevDeg = IdleDeg(model);
    AngleCurve curve(p.angleCurve, S, E);
    switch (p.repeat) {
    case FlameParams::Repeat::Once: {
        ShotAngles a = AnglesForShot(p, model, 0, plan.preset);
        FlameShot s;
        int lead = AlignUp(travel(prevDeg, a.start), frameMs);
        s.leadStartMs = S;
        s.fireStartMs = std::min(E, S + lead);
        s.travelMs = lead;
        if (plan.action != FlameParams::Action::Preset) {
            s.fireEndMs = E;
        } else {
            s.fireEndMs = std::min(E, s.fireStartMs + lenAligned);
            s.cut = s.fireStartMs + lenAligned > E;
        }
        s.startDeg = a.start;
        s.endDeg = a.end;
        if (s.fireEndMs > s.fireStartMs) {
            plan.shots.push_back(s);
        }
        break;
    }
    case FlameParams::Repeat::BackToBack: {
        int prevEnd = S;
        for (int i = 0; i < 100000; ++i) {
            int estimate = i == 0 ? S : prevEnd + std::max(frameMs, AlignUp(p.gapMs, frameMs));
            ShotAngles a = AnglesForShot(p, model, i, plan.preset, &curve, estimate);
            int lead = AlignUp(travel(prevDeg, a.start), frameMs);
            FlameShot s;
            s.startDeg = a.start;
            s.endDeg = a.end;
            if (i == 0) {
                s.fireStartMs = S + lead;
                s.leadStartMs = S;
            } else {
                // the unit needs at least one frame with fire low to fire again
                int wait = std::max({ frameMs, AlignUp(p.gapMs, frameMs), lead });
                s.fireStartMs = prevEnd + wait;
                // aim at this shot as soon as the last one ends
                s.leadStartMs = prevEnd;
            }
            s.travelMs = realTravel(prevDeg, a.start);
            s.late = s.travelMs > s.fireStartMs - s.leadStartMs;
            s.fireEndMs = s.fireStartMs + lenAligned;
            if (s.fireStartMs >= E) break;
            if (s.fireEndMs > E) {
                if (i > 0) break; // a shot that wouldn't finish isn't started
                s.fireEndMs = E;
                s.cut = true;
            }
            plan.shots.push_back(s);
            prevEnd = s.fireEndMs;
            prevDeg = a.end;
        }
        break;
    }
    case FlameParams::Repeat::Random: {
        FlameRng rng(HashName(model->GetFullName(), (uint64_t)startMs));
        const bool varied = plan.action != FlameParams::Action::Preset && p.shotMaxMs > len;
        const double meanLen = varied ? (len + p.shotMaxMs) / 2.0 : (double)lenAligned;
        const double meanInterval = 1000.0 / p.rate;
        const double meanWait = std::max(0.0, meanInterval - meanLen);
        const int minGap = std::max(frameMs, AlignUp(p.gapMs, frameMs));
        // first shot lands somewhere in the first interval so units don't start together
        double t = S + rng.Unit() * meanInterval;
        int prevEnd = S;
        const std::vector<int>& choices = p.pattern;
        for (int i = 0; i < 100000; ++i) {
            int shotLen = lenAligned;
            if (varied) {
                shotLen = std::max(frameMs, AlignUp(len + (int)(rng.Unit() * (p.shotMaxMs - len + 1)), frameMs));
            }
            FlameShot s;
            s.fireStartMs = std::max(AlignUp((int)t, frameMs), i == 0 ? S : prevEnd + minGap);
            if (fx.HasAngle() && !choices.empty() && plan.action != FlameParams::Action::Preset) {
                int pos = choices[(size_t)(rng.Unit() * choices.size()) % choices.size()];
                s.startDeg = s.endDeg = model->ClampAngle(fx.PositionDeg(pos));
            } else {
                ShotAngles a = AnglesForShot(p, model, i, plan.preset, &curve, s.fireStartMs);
                s.startDeg = a.start;
                s.endDeg = a.end;
            }
            s.fireEndMs = s.fireStartMs + shotLen;
            if (s.fireEndMs > E) {
                break;
            }
            // aim at this shot as soon as the last one ends
            s.leadStartMs = i == 0 ? S : prevEnd;
            s.travelMs = realTravel(prevDeg, s.startDeg);
            s.late = s.travelMs > s.fireStartMs - s.leadStartMs;
            plan.shots.push_back(s);
            prevEnd = s.fireEndMs;
            prevDeg = s.endDeg;
            t = s.fireEndMs + minGap + rng.Unit() * 2.0 * meanWait;
        }
        break;
    }
    case FlameParams::Repeat::Marks: {
        std::vector<int> times;
        for (int m : marks) {
            int t = std::max(S, AlignNearest(m, frameMs));
            if (t < E && (times.empty() || t > times.back())) {
                times.push_back(t);
            }
        }
        int prevEnd = S;
        for (size_t i = 0; i < times.size(); ++i) {
            ShotAngles a = AnglesForShot(p, model, (int)i, plan.preset, &curve, times[i]);
            FlameShot s;
            s.startDeg = a.start;
            s.endDeg = a.end;
            s.fireStartMs = times[i];
            int limit = E;
            if (i + 1 < times.size()) {
                // too-close marks: the running shot is cut one frame early so
                // the next one fires on its mark
                limit = times[i + 1] - frameMs;
            }
            s.fireEndMs = std::min(s.fireStartMs + lenAligned, limit);
            s.cut = s.fireEndMs < s.fireStartMs + lenAligned;
            // aim at this shot as soon as the last one ends
            s.leadStartMs = i == 0 ? S : prevEnd;
            s.travelMs = realTravel(prevDeg, a.start);
            s.late = s.travelMs > s.fireStartMs - s.leadStartMs;
            if (s.fireEndMs <= s.fireStartMs) {
                continue;
            }
            plan.shots.push_back(s);
            prevEnd = s.fireEndMs;
            prevDeg = a.end;
        }
        break;
    }
    }
    return plan;
}

void FlameEffect::RenameTimingTrack(std::string oldname, std::string newname, Effect* effect) {
    if (effect->GetSettings().Get("E_CHOICE_Flame_TimingTrack", "") == oldname) {
        effect->GetSettings()["E_CHOICE_Flame_TimingTrack"] = newname;
    }
}

std::list<std::string> FlameEffect::CheckEffectSettings(const SettingsMap& settings, AudioManager* media, Model* model, Effect* eff, bool renderCache) {
    std::list<std::string> res = RenderableEffect::CheckEffectSettings(settings, media, model, eff, renderCache);
    const DmxFlame* flame = FlameModelFor(model);
    if (flame == nullptr) {
        if (model != nullptr && model->GetDisplayAs() != DisplayAsType::ModelGroup) {
            res.push_back(fmt::format("    WARN: Flame effect on '{}', which is not a DMX Flame model. It does nothing. Start {}", model->GetFullName(), FORMATTIME(eff->GetStartTimeMS())));
        }
        return res;
    }
    FlameParams p = FlameParams::FromSettings(settings, "E_");
    if (p.repeat == FlameParams::Repeat::Marks) {
        if (p.timingTrack.empty()) {
            res.push_back(fmt::format("    ERR: Flame effect set to fire on timing marks but no timing track is chosen. Model '{}', Start {}", model->GetFullName(), FORMATTIME(eff->GetStartTimeMS())));
        } else if (GetTiming(p.timingTrack, eff->GetParentEffectLayer()->GetParentElement()->GetSequenceElements()) == nullptr) {
            res.push_back(fmt::format("    ERR: Flame effect uses unknown timing track '{}'. Model '{}', Start {}", p.timingTrack, model->GetFullName(), FORMATTIME(eff->GetStartTimeMS())));
        }
    }
    const auto& fx = flame->GetFixture();
    if (p.action == FlameParams::Action::Preset && fx.HasPresets() && fx.FindPreset(p.preset) == nullptr) {
        res.push_back(fmt::format("    ERR: Flame effect uses preset {}, which '{}' doesn't have. Model '{}', Start {}", p.preset, fx.name, model->GetFullName(), FORMATTIME(eff->GetStartTimeMS())));
    }
    return res;
}

void FlameEffect::Render(Effect* effect, const SettingsMap& settings, RenderBuffer& buffer) {
    const DmxFlame* model = FlameModelFor(buffer.GetModel());
    if (model == nullptr) {
        return;
    }
    const auto& fx = model->GetFixture();
    FlameParams p = FlameParams::FromSettings(settings, "");

    const int ft = buffer.frameTimeInMs;
    // The buffer's end time is the start of the effect's last frame; the plan
    // works with the effect's real (exclusive) end, same as the timeline.
    const int S = effect != nullptr ? effect->GetStartTimeMS() : (int)buffer.GetStartTimeMS();
    const int E = effect != nullptr ? effect->GetEndTimeMS() : (int)buffer.GetEndTimeMS() + ft;
    const int t = buffer.curPeriod * ft;

    std::vector<int> marks;
    if (p.repeat == FlameParams::Repeat::Marks) {
        SequenceElements* se = GetSequenceElements(buffer);
        marks = CollectMarks(GetTiming(p.timingTrack, se), S, E);
    }
    FlamePlan plan = BuildPlan(p, model, S, E, ft, marks);

    bool firing = false;
    bool presetActive = false;
    float deg = IdleDeg(model);
    const FlameShot* cur = nullptr;
    for (const auto& s : plan.shots) {
        if (t < s.leadStartMs) {
            break;
        }
        cur = &s;
    }
    if (cur != nullptr) {
        if (t >= cur->fireStartMs && t < cur->fireEndMs) {
            firing = true;
            presetActive = plan.preset != nullptr;
            if (plan.action == FlameParams::Action::Sweep) {
                int span = (p.repeat == FlameParams::Repeat::Once || p.repeat == FlameParams::Repeat::Random) ? (cur->fireEndMs - cur->fireStartMs) : plan.shotLenMs;
                float f = span > 0 ? std::clamp((float)(t - cur->fireStartMs) / (float)span, 0.0f, 1.0f) : 1.0f;
                deg = cur->startDeg + (cur->endDeg - cur->startDeg) * f;
            } else if (plan.action == FlameParams::Action::Shot && p.repeat == FlameParams::Repeat::Once && p.pattern.empty() && fx.HasAngle()) {
                // a single continuous shot may sweep with the curve; repeated
                // shots each hold the angle worked out in the plan
                float pos = buffer.GetEffectTimeIntervalPosition();
                deg = model->ClampAngle((float)GetValueCurveInt("Flame_Angle", p.angleDeg, settings, pos, FLAME_ANGLE_MIN, FLAME_ANGLE_MAX, buffer.GetStartTimeMS(), buffer.GetEndTimeMS()));
            } else {
                deg = cur->startDeg;
            }
        } else if (t < cur->fireStartMs) {
            // moving to the next shot's angle
            deg = cur->startDeg;
            presetActive = plan.preset != nullptr;
        } else {
            deg = cur->endDeg;
        }
    } else if (!plan.shots.empty()) {
        deg = plan.shots.front().startDeg;
    } else if (fx.HasAngle() && plan.action != FlameParams::Action::Preset) {
        // nothing to fire (e.g. no timing marks yet): still aim where the user set it
        deg = !p.pattern.empty() ? model->ClampAngle(fx.PositionDeg(p.pattern.front())) : model->ClampAngle((float)p.angleDeg);
    }

    auto put = [&buffer, &fx](int ch, int value) {
        if (ch < 1 || ch > fx.ChannelCount()) return;
        uint8_t v = (uint8_t)std::clamp(value, 0, 255);
        buffer.SetPixel(ch - 1, 0, xlColor(v, v, v), false, false, true);
    };

    for (int ch = 1; ch <= fx.ChannelCount(); ++ch) {
        put(ch, fx.IdleValue(ch));
    }
    put(fx.fireChannel, firing ? fx.fireOn : fx.fireOff);
    if (fx.HasAngle()) {
        put(fx.angleChannel, fx.AngleToValue(deg));
    }
    if (fx.HasSpeed()) {
        put(fx.speedChannel, fx.speedMax);
    }
    if (fx.HasDuration()) {
        int d = 0;
        if (plan.action == FlameParams::Action::Shot) {
            int ms = p.shotMs;
            if (p.repeat == FlameParams::Repeat::Once && !plan.shots.empty()) {
                ms = plan.shots.front().fireEndMs - plan.shots.front().fireStartMs;
            } else if (p.repeat == FlameParams::Repeat::Random && cur != nullptr) {
                ms = cur->fireEndMs - cur->fireStartMs;
            }
            int steps = (ms + fx.durationMsPerStep - 1) / fx.durationMsPerStep;
            d = steps > fx.durationMax ? 0 : std::max(fx.durationMin, steps);
        }
        put(fx.durationChannel, d);
    }
    if (fx.presetChannel > 0) {
        put(fx.presetChannel, presetActive && plan.preset != nullptr ? plan.preset->value : fx.presetNone);
    }
}

int FlameEffect::DrawEffectBackground(const Effect* e, int x1, int y1, int x2, int y2,
                                      xlVertexColorAccumulator& bg, xlColor* colorMask, bool ramps) {
    if (e == nullptr || x2 <= x1) {
        return 1;
    }
    EffectLayer* layer = e->GetParentEffectLayer();
    Element* el = layer != nullptr ? layer->GetParentElement() : nullptr;
    SequenceElements* se = el != nullptr ? el->GetSequenceElements() : nullptr;
    RenderContext* ctx = se != nullptr ? se->GetRenderContext() : nullptr;
    if (ctx == nullptr) {
        return 1;
    }
    // One lane per flame unit: a single model gets the whole bar, a group
    // gets a thin lane for each of its flame units.
    std::vector<const DmxFlame*> units;
    const Model* m = ctx->GetModel(el->GetModelName());
    if (const DmxFlame* f = FlameModelFor(m)) {
        units.push_back(f);
    } else if (m != nullptr && m->GetDisplayAs() == DisplayAsType::ModelGroup) {
        for (const Model* member : static_cast<const ModelGroup*>(m)->GetFlatModels(true, false)) {
            if (const DmxFlame* f = FlameModelFor(member)) {
                units.push_back(f);
                if (units.size() >= 24) break;
            }
        }
    }
    if (units.empty()) {
        return 1;
    }
    int ft = se->GetFrameMS();
    FlameParams p = FlameParams::FromSettings(e->GetSettings(), "E_");
    std::vector<int> marks;
    if (p.repeat == FlameParams::Repeat::Marks) {
        marks = CollectMarks(GetTiming(p.timingTrack, se), e->GetStartTimeMS(), e->GetEndTimeMS());
    }
    double span = (double)(e->GetEndTimeMS() - e->GetStartTimeMS());
    if (span <= 0) {
        return 1;
    }
    auto xAt = [&](int ms) {
        double f = (double)(ms - e->GetStartTimeMS()) / span;
        return (float)(x1 + std::clamp(f, 0.0, 1.0) * (x2 - x1));
    };

    xlColor burn(255, 120, 20);
    xlColor burnCut(220, 40, 30);
    xlColor lead(90, 140, 220);
    xlColor leadLate(240, 180, 30);
    xlColor tick(255, 240, 160);
    burn.ApplyMask(colorMask);
    burnCut.ApplyMask(colorMask);
    lead.ApplyMask(colorMask);
    leadLate.ApplyMask(colorMask);
    tick.ApplyMask(colorMask);

    const float laneTop = (float)y1 + 2.0f;
    const float laneH = ((float)y2 - 2.0f - laneTop) / (float)units.size();
    const bool single = units.size() == 1;
    for (size_t u = 0; u < units.size(); ++u) {
        FlamePlan plan = BuildPlan(p, units[u], e->GetStartTimeMS(), e->GetEndTimeMS(), ft, marks);
        const float top = laneTop + laneH * u + (single ? 1.0f : 0.0f);
        const float bot = top + laneH - (single ? 2.0f : 1.0f);
        const float inset = single ? 2.0f : 0.0f;
        for (const auto& s : plan.shots) {
            float lx = xAt(std::max(s.leadStartMs, s.fireStartMs - std::max(s.travelMs, 0)));
            float fx0 = xAt(s.fireStartMs);
            float fx1 = xAt(s.fireEndMs);
            if (fx0 > lx + 0.5f) {
                // hatched band: the nozzle is moving, no flame yet
                const xlColor& c = s.late ? leadLate : lead;
                float stripe = 4.0f;
                for (float x = lx; x < fx0; x += stripe * 2) {
                    bg.AddRectAsTriangles(x, top, std::min(x + stripe, fx0), bot, c);
                }
            }
            if (fx1 > fx0) {
                bg.AddRectAsTriangles(fx0, top + inset, std::max(fx0 + 1.0f, fx1), bot - inset, s.cut ? burnCut : burn);
            }
            if (single) {
                bg.AddRectAsTriangles(fx0, (float)y1 + 1.0f, fx0 + 2.0f, (float)y2 - 1.0f, tick);
            }
        }
    }
    return 2;
}
