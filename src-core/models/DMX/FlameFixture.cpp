/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "FlameFixture.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "utils/ExternalHooks.h"

int FlameFixture::AngleToValue(float deg) const {
    if (!HasAngle()) {
        return centerValue;
    }
    for (const auto& p : positions) {
        if (std::fabs(p.deg - deg) < 0.01f) {
            return p.value;
        }
    }
    float v = (deg - minDeg) / (maxDeg - minDeg) * 255.0f;
    int iv = (int)std::floor(v + 0.5f);
    return std::clamp(iv, 0, 255);
}

float FlameFixture::ValueToAngle(int value) const {
    if (!HasAngle()) {
        return 0.0f;
    }
    for (const auto& p : positions) {
        if (p.value == value) {
            return p.deg;
        }
    }
    return minDeg + (float)std::clamp(value, 0, 255) / 255.0f * (maxDeg - minDeg);
}

float FlameFixture::PositionDeg(int n) const {
    for (const auto& p : positions) {
        if (p.n == n) {
            return p.deg;
        }
    }
    return 0.0f;
}

int FlameFixture::PositionValue(int n) const {
    for (const auto& p : positions) {
        if (p.n == n) {
            return p.value;
        }
    }
    return centerValue;
}

int FlameFixture::NearestPosition(float deg) const {
    int best = 0;
    float bestDiff = 1000000.0f;
    for (const auto& p : positions) {
        float d = std::fabs(p.deg - deg);
        if (d < bestDiff) {
            bestDiff = d;
            best = p.n;
        }
    }
    return best;
}

int FlameFixture::TravelMs(float fromDeg, float toDeg) const {
    if (!HasAngle() || travelMs.empty() || positions.size() < 2) {
        return 0;
    }
    float stepDeg = (maxDeg - minDeg) / (float)(positions.size() - 1);
    float steps = std::fabs(toDeg - fromDeg) / stepDeg;
    if (steps < 0.001f) {
        return 0;
    }
    int last = (int)travelMs.size() - 1;
    if (steps <= (float)last) {
        int lo = (int)std::floor(steps);
        int hi = std::min(lo + 1, last);
        float f = steps - (float)lo;
        return (int)std::lround(travelMs[lo] + (travelMs[hi] - travelMs[lo]) * f);
    }
    // Beyond the manual's table: keep the table's last per-step increase.
    float perStep = last > 0 ? (float)(travelMs[last] - travelMs[last - 1]) : 0.0f;
    return (int)std::lround(travelMs[last] + perStep * (steps - (float)last));
}

const FlamePreset* FlameFixture::FindPreset(int n) const {
    for (const auto& p : presets) {
        if (p.n == n) {
            return &p;
        }
    }
    return nullptr;
}

const FlamePreset* FlameFixture::PresetFromValue(int value) const {
    // Each preset owns a small band of values; pick the band holding `value`.
    const FlamePreset* best = nullptr;
    int bestDiff = 1000;
    for (const auto& p : presets) {
        int d = std::abs(p.value - value);
        if (d < bestDiff) {
            bestDiff = d;
            best = &p;
        }
    }
    return bestDiff <= 2 ? best : nullptr;
}

int FlameFixture::IdleValue(int channel) const {
    for (const auto& [ch, v] : idle) {
        if (ch == channel) {
            return v;
        }
    }
    return 0;
}

void FlameFixture::PresetStateAt(const FlamePreset& p, int ms, float& deg, bool& flame) const {
    flame = false;
    deg = 0.0f;
    if (p.positions.empty() || p.lengthMs <= 0) {
        return;
    }
    ms = std::max(0, ms);
    if (ms >= p.lengthMs) {
        deg = PositionDeg(p.positions.back());
        return;
    }
    switch (p.path) {
    case FlamePathType::Static:
        deg = PositionDeg(p.positions.front());
        flame = true;
        break;
    case FlamePathType::Wave: {
        float a = PositionDeg(p.positions.front());
        float b = PositionDeg(p.positions.back());
        float f = (float)ms / (float)p.lengthMs;
        deg = a + (b - a) * f;
        flame = true;
        break;
    }
    case FlamePathType::Step: {
        int n = (int)p.positions.size();
        float slot = (float)p.lengthMs / (float)n;
        int idx = std::min(n - 1, (int)((float)ms / slot));
        deg = PositionDeg(p.positions[idx]);
        float burn = std::min(slot * 0.85f, p.longFlame ? 560.0f : 190.0f);
        flame = ((float)ms - (float)idx * slot) < burn;
        break;
    }
    }
}

namespace {

    std::vector<int> Range(int a, int b) {
        std::vector<int> r;
        if (a <= b) {
            for (int i = a; i <= b; ++i) r.push_back(i);
        } else {
            for (int i = a; i >= b; --i) r.push_back(i);
        }
        return r;
    }

    // Manual: "DMX Value = 2 + Sequence No.*2.55 (ROUND OFF)" is the top of each
    // preset's band; the band starts one above the previous preset's top. The
    // middle of the band (rounded down) is used so a +-1 error still lands.
    int ShowvenPresetValue(int n) {
        auto top = [](int k) { return (200 + 255 * k + 50) / 100; };
        int hi = top(n);
        int lo = top(n - 1) + 1;
        return (lo + hi) / 2;
    }

    std::string DegLabel(float d) {
        int v = (int)std::lround(d);
        if (v > 0) {
            return "+" + std::to_string(v) + "\xC2\xB0";
        }
        if (v < 0) {
            return "\xE2\x88\x92" + std::to_string(-v) + "\xC2\xB0";
        }
        return "0\xC2\xB0";
    }

    std::string PathLabel(const std::vector<int>& pos, bool wave) {
        std::string s;
        if (pos.size() > 3 && !wave) {
            bool contiguous = true;
            for (size_t i = 1; i < pos.size(); ++i) {
                if (std::abs(pos[i] - pos[i - 1]) != 1) contiguous = false;
            }
            if (contiguous) {
                return std::to_string(pos.front()) + "\xE2\x86\x92" + std::to_string(pos.back());
            }
        }
        for (size_t i = 0; i < pos.size(); ++i) {
            if (i) s += wave ? "\xE2\x86\x92" : ">";
            s += std::to_string(pos[i]);
        }
        return s;
    }

    FlameFixture BuildShowven() {
        FlameFixture f;
        f.id = FlameFixtures::ShowvenId;
        f.name = "Showven Circle Flamer X-F1800";
        f.channels = { "Angle", "Speed", "Fire", "Duration", "Preset" };
        f.fireChannel = 3;
        f.fireOn = 255;
        f.fireOff = 0;
        f.fireThreshold = 254;
        f.rearmBelow = 254;
        f.angleChannel = 1;
        f.minDeg = -105.0f;
        f.maxDeg = 105.0f;
        f.centerValue = 128;
        const int values[15] = { 0, 18, 36, 54, 73, 91, 109, 128, 146, 165, 183, 201, 219, 237, 255 };
        for (int i = 0; i < 15; ++i) {
            f.positions.push_back({ i + 1, -105.0f + 15.0f * i, values[i] });
        }
        f.travelMs = { 0, 50, 70, 90, 110, 130, 150, 170 };
        f.speedChannel = 2;
        // Manual: 0 and 255 are both max speed; Jeff's working 2024 tilt
        // sweeps all used 255, so that's what's sent.
        f.speedMax = 255;
        f.durationChannel = 4;
        f.durationMsPerStep = 10;
        f.durationMin = 1;
        f.durationMax = 254;
        f.presetChannel = 5;
        f.presetNone = 0;
        f.idle = { { 1, 128 } };
        f.maxBurnMs = 10000; // manual: "permanent fire (10s is limit)"

        auto add = [&f](int n, const std::string& group, FlamePathType path, std::vector<int> pos, bool isLong, int ms) {
            FlamePreset p;
            p.n = n;
            p.group = group;
            p.path = path;
            p.positions = std::move(pos);
            p.longFlame = isLong;
            p.lengthMs = ms;
            p.value = ShowvenPresetValue(n);
            std::string kind = isLong ? "long" : "short";
            switch (path) {
            case FlamePathType::Static:
                p.name = "Single " + kind + " " + DegLabel(f.PositionDeg(p.positions.front()));
                break;
            case FlamePathType::Step:
                p.name = "Step " + PathLabel(p.positions, false) + " " + kind;
                break;
            case FlamePathType::Wave:
                p.name = "Wave " + PathLabel(p.positions, true);
                break;
            }
            f.presets.push_back(std::move(p));
        };

        for (int i = 1; i <= 15; ++i) {
            add(i, "Single short", FlamePathType::Static, { i }, false, 190);
        }
        for (int i = 1; i <= 15; ++i) {
            add(15 + i, "Single long", FlamePathType::Static, { i }, true, 560);
        }
        const std::vector<std::vector<int>> stepPatterns = {
            Range(1, 15), Range(15, 1), { 5, 8, 11 }, { 11, 8, 5 }, { 6, 10 }, { 10, 6 },
            { 4, 6, 8, 10, 12 }, { 12, 10, 8, 6, 4 }, { 8, 6, 10, 4, 12 }, { 8, 10, 6, 12, 4 }
        };
        const int shortMs[10] = { 2570, 2570, 1000, 1000, 900, 900, 1300, 1300, 1350, 1350 };
        const int longMs[10] = { 8900, 8900, 2010, 2010, 1350, 1350, 3010, 3010, 2670, 2670 };
        for (int i = 0; i < 10; ++i) {
            add(31 + i, "Step short", FlamePathType::Step, stepPatterns[i], false, shortMs[i]);
        }
        for (int i = 0; i < 10; ++i) {
            add(41 + i, "Step long", FlamePathType::Step, stepPatterns[i], true, longMs[i]);
        }
        add(51, "Wave", FlamePathType::Wave, { 5, 11 }, false, 1990);
        add(52, "Wave", FlamePathType::Wave, { 11, 5 }, false, 1990);
        add(53, "Wave", FlamePathType::Wave, { 1, 15 }, true, 4140);
        add(54, "Wave", FlamePathType::Wave, { 15, 1 }, true, 4140);
        add(55, "Wave", FlamePathType::Wave, { 8, 1 }, false, 2180);
        add(56, "Wave", FlamePathType::Wave, { 8, 15 }, false, 2180);
        add(57, "Wave", FlamePathType::Wave, { 1, 8 }, false, 2160);
        add(58, "Wave", FlamePathType::Wave, { 15, 8 }, false, 2160);
        add(59, "Wave", FlamePathType::Wave, { 8, 11 }, false, 1120);
        add(60, "Wave", FlamePathType::Wave, { 8, 5 }, false, 1120);
        add(61, "Wave", FlamePathType::Wave, { 5, 8 }, false, 1240);
        add(62, "Wave", FlamePathType::Wave, { 11, 8 }, false, 1240);
        add(63, "Step short", FlamePathType::Step, { 2, 14 }, false, 600);
        add(64, "Step short", FlamePathType::Step, { 14, 2 }, false, 600);
        add(65, "Step long", FlamePathType::Step, { 2, 14 }, true, 1380);
        add(66, "Step long", FlamePathType::Step, { 14, 2 }, true, 1380);
        add(67, "Step short", FlamePathType::Step, { 8, 14 }, false, 1850);
        add(68, "Step short", FlamePathType::Step, { 8, 2 }, false, 1850);
        add(69, "Step long", FlamePathType::Step, { 8, 14 }, true, 4180);
        add(70, "Step long", FlamePathType::Step, { 8, 2 }, true, 4180);
        return f;
    }

    FlameFixture BuildGFlame() {
        FlameFixture f;
        f.id = FlameFixtures::GFlameId;
        f.name = "Galaxis G-Flame";
        f.channels = { "Flame" };
        f.fireChannel = 1;
        f.fireOn = 255;
        f.fireOff = 0;
        f.fireThreshold = 229;
        f.rearmBelow = 15;
        f.shotPresetsMs = { 300, 500, 1000, 1500 };
        f.maxBurnMs = 25000; // manual: 25 s maximum, then 2 minutes off
        return f;
    }

    bool GetInt(const nlohmann::json& j, const char* key, int& out) {
        auto it = j.find(key);
        if (it == j.end()) return false;
        if (it->is_number()) {
            out = (int)std::lround(it->get<double>());
            return true;
        }
        if (it->is_string()) {
            out = (int)std::strtol(it->get<std::string>().c_str(), nullptr, 10);
            return true;
        }
        return false;
    }

    bool GetFloat(const nlohmann::json& j, const char* key, float& out) {
        auto it = j.find(key);
        if (it == j.end()) return false;
        if (it->is_number()) {
            out = (float)it->get<double>();
            return true;
        }
        if (it->is_string()) {
            out = (float)std::strtod(it->get<std::string>().c_str(), nullptr);
            return true;
        }
        return false;
    }

    std::string GetString(const nlohmann::json& j, const char* key, const std::string& def = "") {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) return it->get<std::string>();
        return def;
    }

    std::vector<int> GetIntArray(const nlohmann::json& j, const char* key) {
        std::vector<int> r;
        auto it = j.find(key);
        if (it != j.end() && it->is_array()) {
            for (const auto& v : *it) {
                if (v.is_number()) r.push_back((int)std::lround(v.get<double>()));
            }
        }
        return r;
    }
}

namespace FlameFixtures {

    const FlameFixture& GFlame() {
        static const FlameFixture f = BuildGFlame();
        return f;
    }

    const FlameFixture& ShowvenCircle() {
        static const FlameFixture f = BuildShowven();
        return f;
    }

    const FlameFixture* BuiltIn(const std::string& id) {
        if (id == GFlameId) return &GFlame();
        if (id == ShowvenId) return &ShowvenCircle();
        return nullptr;
    }

    bool ParseJson(const std::string& text, FlameFixture& out, std::string& error) {
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            error = "Not valid JSON";
            return false;
        }
        FlameFixture f;
        f.id = CustomId;
        f.name = GetString(j, "name", "Custom flame");

        auto ch = j.find("channels");
        if (ch == j.end() || !ch->is_array() || ch->empty()) {
            error = "\"channels\" must list at least one channel name";
            return false;
        }
        for (const auto& c : *ch) {
            f.channels.push_back(c.is_string() ? c.get<std::string>() : std::string("Channel"));
        }
        int count = (int)f.channels.size();
        auto chanOk = [count](int c) { return c >= 0 && c <= count; };

        auto fire = j.find("fire");
        if (fire == j.end() || !fire->is_object() || !GetInt(*fire, "channel", f.fireChannel) || f.fireChannel < 1 || f.fireChannel > count) {
            error = "\"fire.channel\" is missing or outside the channel list";
            return false;
        }
        GetInt(*fire, "on", f.fireOn);
        GetInt(*fire, "off", f.fireOff);
        if (!GetInt(*fire, "threshold", f.fireThreshold)) {
            f.fireThreshold = f.fireOn;
        }
        if (!GetInt(*fire, "rearmBelow", f.rearmBelow)) {
            f.rearmBelow = f.fireThreshold;
        }

        auto angle = j.find("angle");
        if (angle != j.end() && angle->is_object()) {
            GetInt(*angle, "channel", f.angleChannel);
            GetFloat(*angle, "minDeg", f.minDeg);
            GetFloat(*angle, "maxDeg", f.maxDeg);
            GetInt(*angle, "centerValue", f.centerValue);
            auto pos = angle->find("positions");
            if (pos != angle->end() && pos->is_array()) {
                for (const auto& p : *pos) {
                    if (!p.is_object()) continue;
                    FlamePosition fp;
                    GetInt(p, "n", fp.n);
                    GetFloat(p, "deg", fp.deg);
                    GetInt(p, "value", fp.value);
                    f.positions.push_back(fp);
                }
            }
            f.travelMs = GetIntArray(*angle, "travelMs");
            if (!chanOk(f.angleChannel)) {
                error = "\"angle.channel\" is outside the channel list";
                return false;
            }
        }
        auto speed = j.find("speed");
        if (speed != j.end() && speed->is_object()) {
            GetInt(*speed, "channel", f.speedChannel);
            GetInt(*speed, "max", f.speedMax);
            if (!chanOk(f.speedChannel)) {
                error = "\"speed.channel\" is outside the channel list";
                return false;
            }
        }
        auto dur = j.find("duration");
        if (dur != j.end() && dur->is_object()) {
            GetInt(*dur, "channel", f.durationChannel);
            GetInt(*dur, "msPerStep", f.durationMsPerStep);
            GetInt(*dur, "min", f.durationMin);
            GetInt(*dur, "max", f.durationMax);
            auto bs = dur->find("backstop");
            f.durationBackstop = bs != dur->end() && bs->is_boolean() && bs->get<bool>();
            if (!chanOk(f.durationChannel) || f.durationMsPerStep <= 0) {
                error = "\"duration\" block is invalid";
                return false;
            }
        }
        auto pre = j.find("preset");
        if (pre != j.end() && pre->is_object()) {
            GetInt(*pre, "channel", f.presetChannel);
            GetInt(*pre, "none", f.presetNone);
            if (!chanOk(f.presetChannel)) {
                error = "\"preset.channel\" is outside the channel list";
                return false;
            }
            auto list = pre->find("list");
            if (list != pre->end() && list->is_array()) {
                for (const auto& p : *list) {
                    if (!p.is_object()) continue;
                    FlamePreset fp;
                    GetInt(p, "n", fp.n);
                    GetInt(p, "value", fp.value);
                    GetInt(p, "lengthMs", fp.lengthMs);
                    fp.name = GetString(p, "name", "Preset " + std::to_string(fp.n));
                    fp.group = GetString(p, "group", "Presets");
                    fp.longFlame = GetString(p, "size", "short") == "long";
                    auto path = p.find("path");
                    if (path != p.end() && path->is_object()) {
                        std::string t = GetString(*path, "type", "static");
                        fp.path = t == "step" ? FlamePathType::Step : (t == "wave" ? FlamePathType::Wave : FlamePathType::Static);
                        fp.positions = GetIntArray(*path, "positions");
                    }
                    f.presets.push_back(std::move(fp));
                }
            }
        }
        auto idle = j.find("idle");
        if (idle != j.end() && idle->is_object()) {
            for (auto it = idle->begin(); it != idle->end(); ++it) {
                int c = (int)std::strtol(it.key().c_str(), nullptr, 10);
                if (c >= 1 && c <= count && it.value().is_number()) {
                    f.idle.emplace_back(c, std::clamp((int)std::lround(it.value().get<double>()), 0, 255));
                }
            }
        }
        f.shotPresetsMs = GetIntArray(j, "shotPresetsMs");
        GetInt(j, "maxBurnMs", f.maxBurnMs);
        // reject definitions that could fire by themselves or never stop
        if (f.fireOn < f.fireThreshold) {
            error = "\"fire.on\" is below \"fire.threshold\", so the unit would never fire";
            return false;
        }
        if (f.fireOff >= f.rearmBelow || f.fireOff >= f.fireThreshold) {
            error = "\"fire.off\" must be below \"fire.rearmBelow\" and \"fire.threshold\"";
            return false;
        }
        for (const auto& [ch, v] : f.idle) {
            if (ch == f.fireChannel && v >= f.rearmBelow) {
                error = "\"idle\" sets the fire channel to a firing value";
                return false;
            }
        }
        if (f.HasAngle() && f.IdleValue(f.angleChannel) == 0 && std::none_of(f.idle.begin(), f.idle.end(), [&](const auto& kv) { return kv.first == f.angleChannel; })) {
            f.idle.emplace_back(f.angleChannel, f.centerValue); // park at center, not at the end stop
        }
        for (const auto& pr : f.presets) {
            if (pr.value <= f.presetNone || pr.lengthMs <= 0) {
                error = "preset " + std::to_string(pr.n) + " needs a \"value\" and a \"lengthMs\"";
                return false;
            }
        }
        if (f.angleChannel == f.fireChannel || (f.presetChannel > 0 && f.presetChannel == f.fireChannel)) {
            error = "the fire channel can't also be the angle or preset channel";
            return false;
        }
        out = std::move(f);
        return true;
    }

    bool LoadFromJson(const std::string& path, FlameFixture& out, std::string& error) {
        if (path.empty()) {
            error = "No definition file chosen";
            return false;
        }
        if (!FileExists(path)) {
            error = "File not found: " + path;
            return false;
        }
        std::ifstream in(path);
        if (!in.good()) {
            error = "Could not read " + path;
            return false;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        return ParseJson(ss.str(), out, error);
    }
}
