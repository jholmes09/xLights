/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include <algorithm>
#include <cmath>

#include <spdlog/fmt/fmt.h>

#include "DmxFlame.h"
#include "../ModelScreenLocation.h"
#include "../../graphics/IModelPreview.h"
#include "../ModelManager.h"
#include "../../graphics/xlGraphicsContext.h"
#include "../../graphics/xlGraphicsAccumulators.h"
#include "../../render/SequenceData.h"
#include "utils/FileUtils.h"
#include "utils/ExternalHooks.h"

namespace {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

DmxFlame::DmxFlame(const ModelManager& manager) :
    DmxModel(manager)
{
    DisplayAs = DisplayAsType::DmxFlame;
}

DmxFlame::~DmxFlame()
{
}

const FlameFixture& DmxFlame::GetFixture() const
{
    if (const FlameFixture* f = FlameFixtures::BuiltIn(_fixtureId)) {
        return *f;
    }
    if (_custom.ChannelCount() > 0) {
        return _custom;
    }
    // A broken custom file still needs something to draw and render against.
    static const FlameFixture fallback = [] {
        FlameFixture f;
        f.id = FlameFixtures::CustomId;
        f.name = "Custom (not loaded)";
        f.channels = { "Flame" };
        return f;
    }();
    return fallback;
}

bool DmxFlame::FixtureLoaded() const
{
    return FlameFixtures::BuiltIn(_fixtureId) != nullptr || _custom.ChannelCount() > 0;
}

void DmxFlame::SetFixtureId(const std::string& id)
{
    _fixtureId = id;
    if (_fixtureId == FlameFixtures::CustomId) {
        ReloadCustom();
    }
    if (FixtureLoaded()) {
        SetDmxChannelCount(GetFixture().ChannelCount());
    }
}

void DmxFlame::SetCustomFile(const std::string& file)
{
    if (!file.empty()) {
        ObtainAccessToURL(file);
    }
    _customFile = file.empty() ? file : FileUtils::FixFile("", file);
    if (_fixtureId == FlameFixtures::CustomId) {
        ReloadCustom();
        if (FixtureLoaded()) {
            SetDmxChannelCount(GetFixture().ChannelCount());
        }
    }
}

void DmxFlame::ReloadCustom()
{
    _customError.clear();
    FlameFixture f;
    if (FlameFixtures::LoadFromJson(_customFile, f, _customError)) {
        _custom = std::move(f);
    } else {
        _custom = FlameFixture();
    }
}

namespace {
    // the allowed swing from the position limits, low <= high whatever order they were entered in
    void AllowedRange(const FlameFixture& fx, int minPos, int maxPos, float& lo, float& hi)
    {
        lo = fx.minDeg;
        hi = fx.maxDeg;
        if (fx.PositionCount() == 0) {
            return;
        }
        int a = std::clamp(minPos, 1, fx.PositionCount());
        int b = std::clamp(maxPos, 1, fx.PositionCount());
        float da = fx.PositionDeg(a);
        float db = fx.PositionDeg(b);
        lo = std::min(da, db);
        hi = std::max(da, db);
    }
}

float DmxFlame::MinAllowedDeg() const
{
    const auto& fx = GetFixture();
    if (!fx.HasAngle()) {
        return 0.0f;
    }
    float lo, hi;
    AllowedRange(fx, _minPos, _maxPos, lo, hi);
    return lo;
}

float DmxFlame::MaxAllowedDeg() const
{
    const auto& fx = GetFixture();
    if (!fx.HasAngle()) {
        return 0.0f;
    }
    float lo, hi;
    AllowedRange(fx, _minPos, _maxPos, lo, hi);
    return hi;
}

float DmxFlame::ClampAngle(float deg) const
{
    if (!GetFixture().HasAngle()) {
        return 0.0f;
    }
    return std::clamp(deg, MinAllowedDeg(), MaxAllowedDeg());
}

bool DmxFlame::IsPositionAllowed(int n) const
{
    const auto& fx = GetFixture();
    if (fx.PositionCount() == 0 || n < 1 || n > fx.PositionCount()) {
        return false;
    }
    int lo = std::clamp(std::min(_minPos, _maxPos), 1, fx.PositionCount());
    int hi = std::clamp(std::max(_minPos, _maxPos), 1, fx.PositionCount());
    return n >= lo && n <= hi;
}

void DmxFlame::WriteIdleValues(unsigned char* frame, size_t numChannels) const
{
    if (!CouldComputeStartChannel || !FixtureLoaded()) {
        return; // never write onto somebody else's channels
    }
    const auto& fx = GetFixture();
    for (const auto& [ch, value] : fx.idle) {
        if (ch < 1 || ch > (int)Nodes.size()) {
            continue;
        }
        int32_t start = NodeStartChannel(ch - 1);
        if (start >= 0 && (size_t)start < numChannels) {
            frame[start] = (unsigned char)value;
        }
    }
}

void DmxFlame::WriteIdleValuesForAll(const ModelManager& models, unsigned char* frame, size_t numChannels,
                                     const std::function<bool(size_t)>& inRange)
{
    for (const auto& [name, m] : models) {
        if (m == nullptr || m->GetDisplayAs() != DisplayAsType::DmxFlame) {
            continue;
        }
        const auto* flame = static_cast<const DmxFlame*>(m);
        if (!flame->CouldComputeStartChannel || !flame->FixtureLoaded()) {
            continue;
        }
        const auto& fx = flame->GetFixture();
        for (const auto& [ch, value] : fx.idle) {
            if (ch < 1 || ch > (int)flame->Nodes.size()) {
                continue;
            }
            int32_t start = flame->NodeStartChannel(ch - 1);
            if (start < 0 || (size_t)start >= numChannels || (inRange && !inRange((size_t)start))) {
                continue;
            }
            frame[start] = (unsigned char)value;
        }
    }
}

void DmxFlame::SetPreviewFrameContext(const SequenceData* data, int frame)
{
    _previewData = data;
    _previewFrame = frame;
}

std::vector<std::string> DmxFlame::GenerateNodeNames() const
{
    std::vector<std::string> names = GetFixture().channels;
    names.resize(_dmxChannelCount);
    return names;
}

std::list<std::string> DmxFlame::CheckModelSettings()
{
    std::list<std::string> res;
    if (_fixtureId != FlameFixtures::CustomId && FlameFixtures::BuiltIn(_fixtureId) == nullptr) {
        res.push_back(fmt::format("    ERR: Flame model '{}' has an unknown fixture '{}'; it won't fire.", GetName(), _fixtureId));
    }
    if (_fixtureId == FlameFixtures::CustomId && !_customError.empty()) {
        res.push_back(fmt::format("    ERR: Flame model '{}' custom fixture could not be loaded: {}", GetName(), _customError));
    }
    if (FixtureLoaded() && _dmxChannelCount != GetFixture().ChannelCount()) {
        res.push_back(fmt::format("    ERR: Flame model '{}' has {} channels but the fixture needs {}.", GetName(), _dmxChannelCount, GetFixture().ChannelCount()));
    }
    res.splice(res.end(), DmxModel::CheckModelSettings());
    return res;
}

void DmxFlame::InitModel()
{
    if (_fixtureId == FlameFixtures::CustomId && _custom.ChannelCount() == 0) {
        ReloadCustom();
    }
    if (FixtureLoaded()) {
        _dmxChannelCount = GetFixture().ChannelCount();
    } // else keep the saved channel count so following models don't shift
    DmxModel::InitModel();
    screenLocation.SetRenderSize(1, 1, 1);

    std::string names;
    for (const auto& n : GetFixture().channels) {
        if (!names.empty()) {
            names += ",";
        }
        names += n;
    }
    SetNodeNames(names, true);
}

int DmxFlame::ChannelValueAt(int frame, int fixtureChannel) const
{
    if (_previewData == nullptr || frame < 0 || frame >= (int)_previewData->NumFrames()) {
        return -1;
    }
    if (fixtureChannel < 1 || fixtureChannel > (int)Nodes.size()) {
        return -1;
    }
    int32_t start = NodeStartChannel(fixtureChannel - 1);
    if (start < 0 || (unsigned int)start >= _previewData->NumChannels()) {
        return -1;
    }
    // const FrameData::operator[] returns a pointer to the channel
    return *((*_previewData)[frame][start]);
}

void DmxFlame::CurrentState(float& deg, bool& flame, float& size) const
{
    const auto& fx = GetFixture();
    auto value = [this](int ch) -> int {
        if (ch < 1 || ch > (int)Nodes.size()) {
            return 0;
        }
        xlColor c;
        Nodes[ch - 1]->GetColor(c);
        return c.red;
    };

    deg = fx.HasAngle() ? fx.ValueToAngle(value(fx.angleChannel)) : 0.0f;
    int fire = value(fx.fireChannel);
    flame = fire >= fx.fireThreshold;
    size = 1.0f;
    if (!flame) {
        return;
    }

    // How long has fire been held? Read back through the rendered frames so a
    // scrubbed frame draws exactly what playback would.
    int heldMs = -1;
    int presetValue = fx.HasPresets() ? value(fx.presetChannel) : 0;
    if (_previewData != nullptr && _previewFrame >= 0) {
        int ft = (int)_previewData->FrameTime();
        int maxBack = ft > 0 ? 12000 / ft : 0;
        int start = _previewFrame;
        for (int f = _previewFrame - 1; f >= 0 && _previewFrame - f <= maxBack; --f) {
            int v = ChannelValueAt(f, fx.fireChannel);
            if (v < fx.fireThreshold) {
                break;
            }
            if (fx.HasPresets() && ChannelValueAt(f, fx.presetChannel) != presetValue) {
                break;
            }
            start = f;
        }
        heldMs = (_previewFrame - start) * ft;
    }

    const FlamePreset* preset = (fx.HasPresets() && presetValue > fx.presetNone + 2) ? fx.PresetFromValue(presetValue) : nullptr;
    if (preset != nullptr) {
        size = preset->longFlame ? 1.0f : 0.65f;
        if (heldMs >= 0) {
            float pdeg;
            bool pflame;
            fx.PresetStateAt(*preset, heldMs, pdeg, pflame);
            deg = pdeg;
            flame = pflame;
        } else {
            deg = fx.PositionDeg(preset->positions.empty() ? 8 : preset->positions.front());
        }
        return;
    }
    if (fx.HasDuration() && heldMs >= 0) {
        int d = value(fx.durationChannel);
        int limitMs = (d == 0 || d == 255) ? 10000 : d * fx.durationMsPerStep;
        if (heldMs >= limitMs) {
            flame = false;
        }
    }
}

void DmxFlame::DrawModel(IModelPreview* preview, xlGraphicsContext* ctx, xlGraphicsProgram* sprogram, xlGraphicsProgram* tprogram, bool is3d, bool active, const xlColor* c)
{
    const auto& fx = GetFixture();
    if ((int)Nodes.size() < fx.ChannelCount() || fx.ChannelCount() == 0) {
        DmxModel::DrawInvalid(sprogram, &(GetModelScreenLocation()), false, false);
        return;
    }

    xlColor color;
    if (c != nullptr) {
        color = *c;
    }
    int trans = color == xlBLACK ? blackTransparency : transparency;

    float deg = 0.0f;
    bool flame = false;
    float size = 1.0f;
    CurrentState(deg, flame, size);
    if (_flip) {
        deg = -deg;
    }

    auto vac = sprogram->getAccumulator();
    int startVert = vac->getCount();
    auto tvac = tprogram->getAccumulator();
    int tStart = tvac->getCount();

    xlColor body(80, 80, 80);
    xlColor nozzle(150, 150, 150);
    if (c != nullptr && color != xlBLACK) {
        nozzle = color;
    }
    ApplyTransparency(body, trans, trans);
    ApplyTransparency(nozzle, trans, trans);

    const float baseY = -0.5f;
    const float nozzleY = -0.36f;
    vac->AddRectAsTriangles(-0.2f, baseY, 0.2f, baseY + 0.1f, body);
    vac->AddCircleAsTriangles(0.0f, nozzleY, 0.06f, nozzle);

    if (fx.HasAngle()) {
        // faint marks for the allowed swing, plus the nozzle direction
        float lo = MinAllowedDeg();
        float hi = MaxAllowedDeg();
        if (_flip) {
            std::swap(lo, hi);
            lo = -lo;
            hi = -hi;
        }
        xlColor arc(120, 120, 120, 90);
        for (float a : { lo, hi }) {
            float r = (float)(a * kDegToRad);
            float dx = std::sin(r), dy = std::cos(r);
            float px = -dy * 0.006f, py = dx * 0.006f;
            float x0 = dx * 0.09f, y0 = nozzleY + dy * 0.09f;
            float x1 = dx * 0.2f, y1 = nozzleY + dy * 0.2f;
            tvac->AddVertex(x0 - px, y0 - py, arc);
            tvac->AddVertex(x0 + px, y0 + py, arc);
            tvac->AddVertex(x1 + px, y1 + py, arc);
            tvac->AddVertex(x0 - px, y0 - py, arc);
            tvac->AddVertex(x1 + px, y1 + py, arc);
            tvac->AddVertex(x1 - px, y1 - py, arc);
        }
        float r = (float)(deg * kDegToRad);
        float dx = std::sin(r), dy = std::cos(r);
        float px = -dy * 0.018f, py = dx * 0.018f;
        float x1 = dx * 0.13f, y1 = nozzleY + dy * 0.13f;
        vac->AddVertex(-px, nozzleY - py, nozzle);
        vac->AddVertex(px, nozzleY + py, nozzle);
        vac->AddVertex(x1 + px, y1 + py, nozzle);
        vac->AddVertex(-px, nozzleY - py, nozzle);
        vac->AddVertex(x1 + px, y1 + py, nozzle);
        vac->AddVertex(x1 - px, y1 - py, nozzle);
    }

    if (flame) {
        float r = (float)(deg * kDegToRad);
        float dx = std::sin(r), dy = std::cos(r);
        float nx = -dy, ny = dx;
        float length = 0.9f * size * (float)std::clamp(_flameSize, 10, 400) / 100.0f;
        // Flicker is keyed to the frame so a scrubbed frame looks like playback.
        float phase = _previewFrame >= 0 ? (float)_previewFrame * 0.9f : 0.0f;
        const int segs = 10;
        auto colorAt = [trans](float t) {
            xlColor col;
            if (t < 0.2f) {
                float f = t / 0.2f;
                col = xlColor(255, (uint8_t)(245 - 70 * f), (uint8_t)(190 - 150 * f), 235);
            } else {
                float f = (t - 0.2f) / 0.8f;
                col = xlColor(255, (uint8_t)(175 - 135 * f), (uint8_t)(40 - 40 * f), (uint8_t)(235 * (1.0f - f)));
            }
            uint8_t a = col.alpha;
            ApplyTransparency(col, trans, trans);
            col.alpha = std::min(a, col.alpha);
            return col;
        };
        auto pointAt = [&](float t, float side, float& x, float& y) {
            // a narrow jet that only flares near the tip
            float w = 0.008f + 0.035f * std::pow(t, 1.6f) * (1.0f + 0.15f * std::sin(phase + t * 7.0f));
            float wobble = 0.012f * t * t * std::sin(phase * 1.3f + t * 5.0f);
            float along = t * length;
            x = dx * along + nx * (side * w + wobble);
            y = nozzleY + dy * along + ny * (side * w + wobble);
        };
        for (int i = 0; i < segs; ++i) {
            float t0 = (float)i / segs;
            float t1 = (float)(i + 1) / segs;
            float ax, ay, bx, by, cx, cy, ex, ey;
            pointAt(t0, -1.0f, ax, ay);
            pointAt(t0, 1.0f, bx, by);
            pointAt(t1, 1.0f, cx, cy);
            pointAt(t1, -1.0f, ex, ey);
            xlColor c0 = colorAt(t0);
            xlColor c1 = colorAt(t1);
            tvac->AddVertex(ax, ay, c0);
            tvac->AddVertex(bx, by, c0);
            tvac->AddVertex(cx, cy, c1);
            tvac->AddVertex(ax, ay, c0);
            tvac->AddVertex(cx, cy, c1);
            tvac->AddVertex(ex, ey, c1);
        }
    }

    int end = vac->getCount();
    sprogram->addStep([=](xlGraphicsContext* ctx) {
        ctx->drawTriangles(vac, startVert, end - startVert);
    });
    int tEnd = tvac->getCount();
    tprogram->addStep([=](xlGraphicsContext* ctx) {
        ctx->drawTriangles(tvac, tStart, tEnd - tStart);
    });
}

void DmxFlame::DisplayModelOnWindow(IModelPreview* preview, xlGraphicsContext* ctx,
                                    xlGraphicsProgram* sprogram, xlGraphicsProgram* tprogram, bool is_3d,
                                    const xlColor* c, bool allowSelected, bool wiring,
                                    bool highlightFirst, int highlightpixel,
                                    float* boundingBox)
{
    if (!IsActive()) {
        return;
    }

    screenLocation.PrepareToDraw(is_3d, allowSelected);
    screenLocation.UpdateBoundingBox(Nodes);
    if (boundingBox) {
        boundingBox[0] = -0.5;
        boundingBox[1] = -0.5;
        boundingBox[2] = -0.5;
        boundingBox[3] = 0.5;
        boundingBox[4] = 0.5;
        boundingBox[5] = 0.5;
    }

    sprogram->addStep([=, this](xlGraphicsContext* ctx) {
        ctx->PushMatrix();
        if (!is_3d) {
            ctx->Translate(0, 0, 0.5f);
            ctx->ScaleViewMatrix(1.0f, 1.0f, 0.001f);
        }
        GetModelScreenLocation().ApplyModelViewMatrices(ctx);
    });
    tprogram->addStep([=, this](xlGraphicsContext* ctx) {
        ctx->PushMatrix();
        if (!is_3d) {
            ctx->Translate(0, 0, 0.5f);
            ctx->ScaleViewMatrix(1.0f, 1.0f, 0.001f);
        }
        GetModelScreenLocation().ApplyModelViewMatrices(ctx);
    });
    DrawModel(preview, ctx, sprogram, tprogram, is_3d, !allowSelected, c);
    sprogram->addStep([=](xlGraphicsContext* ctx) {
        ctx->PopMatrix();
    });
    tprogram->addStep([=](xlGraphicsContext* ctx) {
        ctx->PopMatrix();
    });
    if ((Selected() || (Highlighted() && is_3d)) && c != nullptr && allowSelected) {
        if (is_3d) {
            GetModelScreenLocation().DrawHandles(tprogram, preview->GetCameraZoomForHandles(), preview->GetHandleScale(), Highlighted(), IsFromBase());
        } else {
            GetModelScreenLocation().DrawHandles(tprogram, preview->GetCameraZoomForHandles(), preview->GetHandleScale(), IsFromBase());
        }
    }
}

void DmxFlame::DisplayEffectOnWindow(IModelPreview* preview, double pointSize)
{
    if (!IsActive() && preview->IsNoCurrentModel()) {
        return;
    }

    bool mustEnd = false;
    xlGraphicsContext* ctx = preview->getCurrentGraphicsContext();
    if (ctx == nullptr) {
        bool success = preview->StartDrawing(pointSize);
        if (success) {
            ctx = preview->getCurrentGraphicsContext();
            mustEnd = true;
        }
    }
    if (ctx) {
        int w = preview->getWidth();
        int h = preview->getHeight();
        float scale = std::min(float(w), float(h)) * 0.95f;
        preview->getCurrentTransparentProgram()->addStep([=](xlGraphicsContext* ctx) {
            ctx->PushMatrix();
            ctx->Translate(w / 2.0f, h / 2.0f, 0.5f);
            ctx->Scale(scale, scale, 0.001f);
        });
        preview->getCurrentSolidProgram()->addStep([=](xlGraphicsContext* ctx) {
            ctx->PushMatrix();
            ctx->Translate(w / 2.0f, h / 2.0f, 0.5f);
            ctx->Scale(scale, scale, 0.001f);
        });
        DrawModel(preview, ctx, preview->getCurrentSolidProgram(), preview->getCurrentTransparentProgram(), false, true, nullptr);
        preview->getCurrentTransparentProgram()->addStep([=](xlGraphicsContext* ctx) {
            ctx->PopMatrix();
        });
        preview->getCurrentSolidProgram()->addStep([=](xlGraphicsContext* ctx) {
            ctx->PopMatrix();
        });
    }
    if (mustEnd) {
        preview->EndDrawing();
    }
}
