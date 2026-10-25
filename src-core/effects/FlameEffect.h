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

#include "RenderableEffect.h"

#include <string>
#include <vector>

class DmxFlame;
class EffectLayer;
struct FlamePreset;

#define FLAME_ANGLE_MIN -105
#define FLAME_ANGLE_MAX 105

// The user's choices on one Flame effect, read from its settings.
struct FlameParams {
    enum class Action { Preset, Shot, Sweep };
    enum class Repeat { Once, BackToBack, Marks };

    Action action = Action::Preset;
    Repeat repeat = Repeat::Once;
    std::string timingTrack;
    int preset = 8;
    int angleDeg = 0;
    int endAngleDeg = 0;
    std::vector<int> pattern; // angle positions, cycled per shot
    int shotMs = 500;
    int gapMs = 0;
    bool moveEarly = false;
    bool fitLength = true;

    // prefix is "E_" for effect settings, "" for a render SettingsMap
    static FlameParams FromSettings(const SettingsMap& s, const std::string& prefix);
    static std::vector<int> ParsePattern(const std::string& text);
};

// One shot: the nozzle starts moving at leadStartMs, fire is held from
// fireStartMs to fireEndMs.
struct FlameShot {
    int leadStartMs = 0;
    int fireStartMs = 0;
    int fireEndMs = 0;
    float startDeg = 0.0f;
    float endDeg = 0.0f;
    bool late = false; // not enough room to finish moving before firing
    bool cut = false;  // stopped early (next timing mark or end of effect)
};

struct FlamePlan {
    std::vector<FlameShot> shots;
    const FlamePreset* preset = nullptr;
    FlameParams::Action action = FlameParams::Action::Shot;
    int startMs = 0;
    int endMs = 0;
    int shotLenMs = 0;
};

class FlameEffect : public RenderableEffect {
public:
    FlameEffect(int id);
    virtual ~FlameEffect();

    virtual bool CanBeRandom() override { return false; }
    virtual void Render(Effect* effect, const SettingsMap& settings, RenderBuffer& buffer) override;
    virtual FrameParallelism GetFrameParallelism(const SettingsMap& settings) const override { return FrameParallelism::Pure; }
    virtual bool CanRenderPartialTimeInterval() const override { return true; }
    virtual void RenameTimingTrack(std::string oldname, std::string newname, Effect* effect) override;
    virtual std::list<std::string> CheckEffectSettings(const SettingsMap& settings, AudioManager* media, Model* model, Effect* eff, bool renderCache) override;
    virtual int DrawEffectBackground(const Effect* e, int x1, int y1, int x2, int y2,
                                     xlVertexColorAccumulator& backgrounds, xlColor* colorMask, bool ramps) override;

    virtual double GetSettingVCMin(const std::string& name) const override {
        if (name == "E_VALUECURVE_Flame_Angle") return FLAME_ANGLE_MIN;
        return RenderableEffect::GetSettingVCMin(name);
    }
    virtual double GetSettingVCMax(const std::string& name) const override {
        if (name == "E_VALUECURVE_Flame_Angle") return FLAME_ANGLE_MAX;
        return RenderableEffect::GetSettingVCMax(name);
    }

    // Works out every shot in the effect. marks = timing mark start times (ms),
    // only used for Repeat::Marks.
    static FlamePlan BuildPlan(const FlameParams& p, const DmxFlame* model, int startMs, int endMs, int frameMs, const std::vector<int>& marks);
    static std::vector<int> CollectMarks(EffectLayer* timing, int startMs, int endMs);

    // Length of shot (or preset) on its own, in ms; 0 when it follows the effect length.
    static int ShotLengthMs(const FlameParams& p, const DmxFlame* model);
    // Travel time the "move early" step adds in front of the first shot.
    static int FirstLeadMs(const FlameParams& p, const DmxFlame* model);
    // Effect length that fits one shot exactly (lead + shot), frame-aligned; 0 if it doesn't apply.
    static int FitLengthMs(const FlameParams& p, const DmxFlame* model, int frameMs);
    // Where the nozzle rests when idle, in degrees.
    static float IdleDeg(const DmxFlame* model);

    static const DmxFlame* FlameModelFor(const Model* m);
};
