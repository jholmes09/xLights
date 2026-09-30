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

class SpiralsEffect : public RenderableEffect
{
public:
    SpiralsEffect(int id);
    virtual ~SpiralsEffect();
    virtual void Render(Effect* effect, const SettingsMap& settings, RenderBuffer& buffer) override;
    virtual FrameParallelism GetFrameParallelism(const SettingsMap& settings) const override { return FrameParallelism::Pure; }
    virtual bool SupportsLinearColorCurves(const SettingsMap& SettingsMap) const override;
    virtual bool CanRenderPartialTimeInterval() const override
    {
        return true;
    }
    virtual void RenameTimingTrack(std::string oldname, std::string newname, Effect* effect) override;

    // Cached from Spirals.json by OnMetadataLoaded(). Exposed as statics so
    // the Metal subclass can read them via unqualified name.
    static int sCountDefault;
    static int sCountMin;
    static int sCountMax;
    static double sMovementDefault;
    static double sMovementMin;
    static double sMovementMax;
    static int sMovementDivisor;
    static double sRotationDefault;
    static double sRotationMin;
    static double sRotationMax;
    static int sRotationDivisor;
    static int sThicknessDefault;
    static int sThicknessMin;
    static int sThicknessMax;
    static bool sBlendDefault;
    static bool s3DDefault;
    static bool sGrowDefault;
    static bool sShrinkDefault;

protected:
    virtual void OnMetadataLoaded() override;
    // 0..1 position within the current cycle: from Movement in Cycles mode, or
    // from the BPM / timing-track tempo (direction still comes from Movement's sign).
    double GetSpiralsPosition(RenderBuffer& buffer, const SettingsMap& SettingsMap, float Movement);
};
