#pragma once

/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

// Shared tempo timing for effects that normally fit a Cycles count to the
// effect's length. An effect exposes three settings, all named from one prefix:
//
//   CHOICE_<prefix>_TempoMode   "Cycles" (default) | "BPM" | "Timing Track"
//   TEXTCTRL_<prefix>_BPM       beats per minute; one cycle per beat
//   CHOICE_<prefix>_TempoTrack  timing track; one cycle per mark
//
// In "Cycles" mode Get() returns inactive and the effect keeps its original
// math untouched, so existing sequences render exactly as before.
//
// Header-only on purpose: adding a .cpp would mean editing the Xcode, CMake and
// Visual Studio project files, which then have to be carried through every
// upstream rebase.

#include <algorithm>
#include <cmath>
#include <string>

#include "../render/Effect.h"
#include "../render/EffectLayer.h"
#include "../render/Element.h"
#include "../render/RenderBuffer.h"
#include "../render/SequenceElements.h"
#include "../utils/UtilClasses.h"

namespace EffectTempo {

constexpr double kDefaultBPM = 120.0;

struct Tempo {
    bool active = false;
    // Cycles elapsed, unwrapped. BPM mode counts from the effect's start;
    // timing-track mode counts marks from the start of the track.
    double phase = 0.0;

    double Position() const { return phase - std::floor(phase); }
};

inline double ReadBPM(const SettingsMap& settings, const std::string& prefix)
{
    const std::string id = prefix + "_BPM";
    const SettingValue* v = settings.FindValue("TEXTCTRL_" + id);
    if (v == nullptr || v->length() == 0) {
        v = settings.FindValue("SLIDER_" + id);
    }
    if (v == nullptr || v->length() == 0) {
        return kDefaultBPM;
    }
    return v->getDouble(kDefaultBPM);
}

// Missing track, empty track, or a frame before the first mark all hold the
// pattern still at the start of a cycle rather than falling back to Cycles:
// a frozen effect is an obvious sign the timing track needs attention, where a
// silent fallback would look like it was working.
inline double TrackPhase(SequenceElements* seq, const std::string& trackName, uint32_t ms)
{
    if (seq == nullptr || trackName.empty()) return 0.0;
    TimingElement* t = seq->GetTimingElement(trackName);
    if (t == nullptr) return 0.0;
    EffectLayer* el = t->GetEffectLayer(0);
    if (el == nullptr) return 0.0;

    const std::vector<Effect*>& marks = el->GetEffects();
    auto after = std::upper_bound(marks.begin(), marks.end(), ms,
                                  [](uint32_t v, const Effect* e) { return v < (uint32_t)e->GetStartTimeMS(); });
    if (after == marks.begin()) return 0.0;

    const Effect* mark = *(after - 1);
    const double index = (double)std::distance(marks.begin(), after - 1);
    const uint32_t start = (uint32_t)mark->GetStartTimeMS();
    const uint32_t end = (uint32_t)mark->GetEndTimeMS();
    if (ms >= end || end <= start) {
        // In a gap between marks or past the last one: that cycle is finished.
        return index + 1.0;
    }
    return index + double(ms - start) / double(end - start);
}

inline Tempo Get(const SettingsMap& settings, const std::string& prefix, const RenderBuffer& buffer, SequenceElements* seq)
{
    Tempo t;
    const std::string& mode = settings.Get("CHOICE_" + prefix + "_TempoMode", "Cycles");
    if (mode == "BPM") {
        t.active = true;
        const double bpm = ReadBPM(settings, prefix);
        if (bpm > 0.0) {
            const double elapsedSec = double(buffer.curPeriod - buffer.curEffStartPer) * buffer.frameTimeInMs / 1000.0;
            t.phase = elapsedSec * bpm / 60.0;
        }
    } else if (mode == "Timing Track") {
        t.active = true;
        const std::string& track = settings.Get("CHOICE_" + prefix + "_TempoTrack", "");
        t.phase = TrackPhase(seq, track, (uint32_t)(buffer.curPeriod * buffer.frameTimeInMs));
    }
    return t;
}

inline void RenameTrack(Effect* effect, const std::string& prefix, const std::string& oldName, const std::string& newName)
{
    const std::string key = "E_CHOICE_" + prefix + "_TempoTrack";
    if (effect->GetSettings().Get(key, "") == oldName) {
        effect->GetSettings()[key] = newName;
    }
}

} // namespace EffectTempo
