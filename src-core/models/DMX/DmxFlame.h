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

#include <functional>

#include "DmxModel.h"
#include "FlameFixture.h"

class SequenceData;

// A DMX flame unit (Galaxis G-Flame, Showven Circle Flamer or a custom JSON
// definition). xLights never drives the units' arming channels: the model only
// covers the fire/angle/preset channels.
class DmxFlame : public DmxModel
{
public:
    DmxFlame(const ModelManager& manager);
    virtual ~DmxFlame();

    void Accept(BaseObjectVisitor& visitor) const override { return visitor.Visit(*this); }

    [[nodiscard]] const FlameFixture& GetFixture() const;
    // false when a custom fixture's definition didn't load: render nothing then
    [[nodiscard]] bool FixtureLoaded() const;
    [[nodiscard]] const std::string& GetFixtureId() const { return _fixtureId; }
    void SetFixtureId(const std::string& id);
    [[nodiscard]] const std::string& GetCustomFile() const { return _customFile; }
    void SetCustomFile(const std::string& file);
    [[nodiscard]] const std::string& GetCustomError() const { return _customError; }

    [[nodiscard]] int GetMinPosition() const { return _minPos; }
    [[nodiscard]] int GetMaxPosition() const { return _maxPos; }
    void SetMinPosition(int p) { _minPos = p; }
    void SetMaxPosition(int p) { _maxPos = p; }
    [[nodiscard]] bool GetFlip() const { return _flip; }
    void SetFlip(bool f) { _flip = f; }
    [[nodiscard]] int GetFlameSize() const { return _flameSize; }
    void SetFlameSize(int s) { _flameSize = s; }

    // Allowed angle range after the model's position limits.
    [[nodiscard]] float MinAllowedDeg() const;
    [[nodiscard]] float MaxAllowedDeg() const;
    [[nodiscard]] float ClampAngle(float deg) const;
    [[nodiscard]] bool IsPositionAllowed(int n) const;

    // Writes the "nothing is happening" values (e.g. Showven nozzle straight up)
    // into one frame of channel data.
    void WriteIdleValues(unsigned char* frame, size_t numChannels) const;
    // Idle values for every flame unit in the show, for frames no effect
    // writes. inRange (optional) limits which channels may be touched.
    static void WriteIdleValuesForAll(const ModelManager& models, unsigned char* frame, size_t numChannels,
                                      const std::function<bool(size_t)>& inRange = nullptr);

    void SetPreviewFrameContext(const SequenceData* data, int frame) override;

    [[nodiscard]] std::vector<std::string> GenerateNodeNames() const override;
    std::list<std::string> CheckModelSettings() override;

protected:
    virtual void InitModel() override;

    virtual void DisplayModelOnWindow(IModelPreview* preview, xlGraphicsContext* ctx,
                                      xlGraphicsProgram* solidProgram, xlGraphicsProgram* transparentProgram, bool is_3d = false,
                                      const xlColor* color = nullptr, bool allowSelected = false, bool wiring = false,
                                      bool highlightFirst = false, int highlightpixel = 0,
                                      float* boundingBox = nullptr) override;
    virtual void DisplayEffectOnWindow(IModelPreview* preview, double pointSize) override;
    void DrawModel(IModelPreview* preview, xlGraphicsContext* ctx, xlGraphicsProgram* sprogram, xlGraphicsProgram* tprogram, bool is3d, bool active, const xlColor* c);

private:
    void ReloadCustom();
    // Works out where the nozzle points and whether flame shows on this frame.
    void CurrentState(float& deg, bool& flame, float& size) const;
    int ChannelValueAt(int frame, int fixtureChannel) const;

    std::string _fixtureId = FlameFixtures::ShowvenId;
    std::string _customFile;
    FlameFixture _custom;
    std::string _customError;
    int _minPos = 1;
    int _maxPos = 15;
    bool _flip = false;
    int _flameSize = 100;

    const SequenceData* _previewData = nullptr;
    int _previewFrame = -1;
};
