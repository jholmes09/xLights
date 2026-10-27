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

#include <string>
#include <utility>
#include <vector>

// Describes what a DMX flame unit can do: its channels, named angle
// positions, built-in presets (with their real lengths) and idle values.
// Channel numbers are 1-based within the fixture; 0 means "not present".

struct FlamePosition {
    int n = 0;
    float deg = 0.0f;
    int value = 0;
};

enum class FlamePathType {
    Static,
    Step,
    Wave
};

struct FlamePreset {
    int n = 0;
    std::string name;
    std::string group;
    int value = 0;
    int lengthMs = 0;
    FlamePathType path = FlamePathType::Static;
    std::vector<int> positions;
    bool longFlame = false;
};

struct FlameFixture {
    std::string id;
    std::string name;
    std::vector<std::string> channels;

    int fireChannel = 1;
    int fireOn = 255;
    int fireOff = 0;
    // Fire is "on" at or above this value; it must drop below rearmBelow
    // before the unit will fire again.
    int fireThreshold = 255;
    int rearmBelow = 15;

    int angleChannel = 0;
    float minDeg = 0.0f;
    float maxDeg = 0.0f;
    int centerValue = 128;
    std::vector<FlamePosition> positions;
    std::vector<int> travelMs;

    int speedChannel = 0;
    int speedMax = 0;

    int durationChannel = 0;
    int durationMsPerStep = 10;
    int durationMin = 1;
    int durationMax = 254;
    // Send the shot length on the duration channel as a stop-by-itself
    // backstop. Off by default: on the Showven a timed shot appears to hold
    // the nozzle still, and the proven way is duration 0 with fire held.
    bool durationBackstop = false;

    int presetChannel = 0;
    int presetNone = 0;
    std::vector<FlamePreset> presets;

    std::vector<std::pair<int, int>> idle;
    std::vector<int> shotPresetsMs;
    // longest burn the unit allows on its own (0 = unknown)
    int maxBurnMs = 0;

    [[nodiscard]] int ChannelCount() const { return (int)channels.size(); }
    [[nodiscard]] bool HasAngle() const { return angleChannel > 0 && maxDeg > minDeg; }
    [[nodiscard]] bool HasPresets() const { return presetChannel > 0 && !presets.empty(); }
    [[nodiscard]] bool HasDuration() const { return durationChannel > 0; }
    [[nodiscard]] bool HasSpeed() const { return speedChannel > 0; }
    [[nodiscard]] int PositionCount() const { return (int)positions.size(); }

    [[nodiscard]] int AngleToValue(float deg) const;
    [[nodiscard]] float ValueToAngle(int value) const;
    [[nodiscard]] float PositionDeg(int n) const;
    [[nodiscard]] int PositionValue(int n) const;
    [[nodiscard]] int NearestPosition(float deg) const;
    // Motor travel time between two angles at max speed.
    [[nodiscard]] int TravelMs(float fromDeg, float toDeg) const;
    [[nodiscard]] const FlamePreset* FindPreset(int n) const;
    [[nodiscard]] const FlamePreset* PresetFromValue(int value) const;
    [[nodiscard]] int IdleValue(int channel) const;

    // Where the nozzle points and whether flame is showing, `ms` into a preset.
    void PresetStateAt(const FlamePreset& p, int ms, float& deg, bool& flame) const;
};

namespace FlameFixtures {
    constexpr auto GFlameId = "GFlame";
    constexpr auto ShowvenId = "ShowvenCircle";
    constexpr auto CustomId = "Custom";

    const FlameFixture& GFlame();
    const FlameFixture& ShowvenCircle();
    // Nullptr for Custom or an unknown id.
    const FlameFixture* BuiltIn(const std::string& id);

    bool LoadFromJson(const std::string& path, FlameFixture& out, std::string& error);
    bool ParseJson(const std::string& text, FlameFixture& out, std::string& error);
}
