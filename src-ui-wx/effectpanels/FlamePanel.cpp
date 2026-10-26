/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "FlamePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "effects/FlameEffect.h"
#include "models/DMX/DmxFlame.h"
#include "models/DMX/FlameFixture.h"
#include "models/Model.h"
#include "models/ModelGroup.h"
#include "render/Effect.h"
#include "render/SequenceElements.h"
#include "xLightsApp.h"
#include "xLightsMain.h"

namespace {
    wxString Seconds(int ms) {
        if (ms % 100 == 0) {
            return wxString::Format("%.1f s", ms / 1000.0);
        }
        return wxString::Format("%.2f s", ms / 1000.0);
    }

    wxString Degrees(float d) {
        int v = (int)std::lround(d);
        if (v > 0) {
            return wxString::Format("+%d°", v);
        }
        return wxString::Format("%d°", v);
    }

    int AlignUp(int ms, int frameMs) {
        if (frameMs <= 0) return ms;
        return ((ms + frameMs - 1) / frameMs) * frameMs;
    }
}

FlamePanel::FlamePanel(wxWindow* parent, const nlohmann::json& metadata) :
    JsonEffectPanel(parent, metadata, /*deferBuild*/ true) {
    BuildFromJson(metadata);

    auto bindChoice = [this](const char* name, bool validate) {
        if (auto* c = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName(name, this))) {
            c->Bind(wxEVT_CHOICE, [this, validate](wxCommandEvent& e) {
                if (validate) {
                    ValidateWindow();
                }
                OnUserChange();
                e.Skip();
            });
        }
    };
    bindChoice("ID_CHOICE_Flame_Action", true);
    bindChoice("ID_CHOICE_Flame_Repeat", true);
    bindChoice("ID_CHOICE_Flame_TimingTrack", false);

    for (const char* name : { "ID_SPINCTRL_Flame_Preset", "ID_SPINCTRL_Flame_ShotMs", "ID_SPINCTRL_Flame_GapMs", "ID_SPINCTRL_Flame_ShotMaxMs" }) {
        if (auto* s = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName(name, this))) {
            s->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& e) {
                OnUserChange();
                e.Skip();
            });
            // Enter settles a typed number and applies it right away
            s->Bind(wxEVT_TEXT_ENTER, [this, s](wxCommandEvent& e) {
                long v = 0;
                if (s->GetTextValue().ToLong(&v)) {
                    s->SetValue((int)std::clamp(v, (long)s->GetMin(), (long)s->GetMax()));
                }
                s->SetValue(s->GetValue());
                FireChangeEvent();
                OnUserChange();
            });
        }
    }
    for (const char* name : { "ID_CHECKBOX_Flame_MoveEarly", "ID_CHECKBOX_Flame_FitLength" }) {
        if (auto* c = dynamic_cast<wxCheckBox*>(wxWindow::FindWindowByName(name, this))) {
            c->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
                OnUserChange();
                e.Skip();
            });
        }
    }
    for (const char* name : { "ID_SLIDER_Flame_Angle", "ID_SLIDER_Flame_EndAngle" }) {
        if (auto* s = dynamic_cast<wxSlider*>(wxWindow::FindWindowByName(name, this))) {
            // Resize only when the drag ends, so one drag is one undo step.
            s->Bind(wxEVT_SCROLL_CHANGED, [this](wxScrollEvent& e) {
                OnUserChange();
                e.Skip();
            });
            s->Bind(wxEVT_SLIDER, [this](wxCommandEvent& e) {
                UpdateInfo();
                e.Skip();
            });
        }
    }
    // Text boxes also change when an effect's settings are loaded; only react
    // to typing.
    if (auto* r = dynamic_cast<wxSlider*>(wxWindow::FindWindowByName("IDD_SLIDER_Flame_Rate", this))) {
        r->Bind(wxEVT_SLIDER, [this](wxCommandEvent& e) {
            UpdateInfo();
            e.Skip();
        });
    }
    for (const char* name : { "ID_TEXTCTRL_Flame_Pattern", "IDD_TEXTCTRL_Flame_Angle", "IDD_TEXTCTRL_Flame_EndAngle", "ID_TEXTCTRL_Flame_Rate" }) {
        if (auto* t = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName(name, this))) {
            t->Bind(wxEVT_TEXT, [this, t](wxCommandEvent& e) {
                if (t->HasFocus()) {
                    OnUserChange();
                }
                e.Skip();
            });
        }
    }

    ValidateWindow();
}

wxWindow* FlamePanel::CreateCustomControl(wxWindow* parentWin, wxSizer* sizer,
                                          const nlohmann::json& prop, int cols) {
    std::string id = prop.value("id", "");
    if (id == "Flame_PresetPicker") {
        return BuildPresetPicker(parentWin, sizer);
    }
    if (id == "Flame_FanButtons") {
        return BuildFanButtons(parentWin, sizer);
    }
    if (id == "Flame_ShotButtons") {
        return BuildShotButtons(parentWin, sizer);
    }
    if (id == "Flame_Info") {
        return BuildInfo(parentWin, sizer);
    }
    return nullptr;
}

wxWindow* FlamePanel::BuildPresetPicker(wxWindow* parentWin, wxSizer* sizer) {
    _presetPicker = new wxChoice(parentWin, wxNewId(), wxDefaultPosition, wxDefaultSize, 0, nullptr, 0,
                                 wxDefaultValidator, _T("IDD_CHOICE_Flame_PresetPicker"));
    // narrow: the full preset name is in the list, not the panel width
    _presetPicker->SetMinSize(wxSize(200, -1));
    _presetPicker->SetToolTip(_("Pick a preset by name. Its length is shown, and the bar fits it when Fit length is on."));
    sizer->Add(_presetPicker, 1, wxALL | wxEXPAND, 2);
    _presetPicker->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) {
        OnPresetPicked();
        e.Skip();
    });
    return _presetPicker;
}

wxWindow* FlamePanel::BuildFanButtons(wxWindow* parentWin, wxSizer* sizer) {
    _rowParent = parentWin;
    _fanContainer = sizer;
    _fanSizer = new wxBoxSizer(wxVERTICAL);
    _fanGrid = new wxGridSizer(0, 5, 2, 2);
    _fanSizer->Add(_fanGrid, 0, wxALL, 0);
    auto* opts = new wxBoxSizer(wxVERTICAL);
    _setEnd = new wxCheckBox(parentWin, wxNewId(), _("Buttons set end"), wxDefaultPosition, wxDefaultSize, 0,
                             wxDefaultValidator, _T("IDD_CHECKBOX_Flame_SetEnd"));
    opts->Add(_setEnd, 0, wxALL, 3);
    _fanSizer->Add(opts, 0, wxALL, 0);
    sizer->Add(_fanSizer, 1, wxALL | wxEXPAND, 2);
    return _setEnd;
}

wxWindow* FlamePanel::BuildShotButtons(wxWindow* parentWin, wxSizer* sizer) {
    _rowParent = parentWin;
    _shotContainer = sizer;
    _shotSizer = new wxGridSizer(0, 2, 2, 2);
    sizer->Add(_shotSizer, 1, wxALL | wxEXPAND, 2);
    return nullptr;
}

wxWindow* FlamePanel::BuildInfo(wxWindow* parentWin, wxSizer* sizer) {
    _info = new wxStaticText(parentWin, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0,
                             _T("IDD_STATICTEXT_Flame_Info"));
    sizer->Add(_info, 1, wxALL | wxEXPAND, 3);
    return _info;
}

const DmxFlame* FlamePanel::GetFlameModel() const {
    if (_modelName.empty()) return nullptr;
    xLightsFrame* frame = xLightsApp::GetFrame();
    if (frame == nullptr) return nullptr;
    Model* m = frame->AllModels[_modelName];
    if (const DmxFlame* f = FlameEffect::FlameModelFor(m)) {
        return f;
    }
    // on a group, the first flame unit stands in for presets and angles
    if (m != nullptr && m->GetDisplayAs() == DisplayAsType::ModelGroup) {
        for (const Model* member : static_cast<ModelGroup*>(m)->GetFlatModels(true, false)) {
            if (const DmxFlame* f = FlameEffect::FlameModelFor(member)) {
                return f;
            }
        }
    }
    return nullptr;
}

int FlamePanel::FlameUnitCount() const {
    if (_modelName.empty()) return 0;
    xLightsFrame* frame = xLightsApp::GetFrame();
    if (frame == nullptr) return 0;
    Model* m = frame->AllModels[_modelName];
    if (FlameEffect::FlameModelFor(m) != nullptr) return 1;
    int n = 0;
    if (m != nullptr && m->GetDisplayAs() == DisplayAsType::ModelGroup) {
        for (const Model* member : static_cast<ModelGroup*>(m)->GetFlatModels(true, false)) {
            if (FlameEffect::FlameModelFor(member) != nullptr) ++n;
        }
    }
    return n;
}

FlameParams FlamePanel::ReadParams() const {
    SettingsMap m;
    auto choice = [this](const char* name) -> std::string {
        auto* c = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName(name, const_cast<FlamePanel*>(this)));
        return c ? c->GetStringSelection().ToStdString() : std::string();
    };
    auto spin = [this](const char* name, int def) -> int {
        auto* s = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName(name, const_cast<FlamePanel*>(this)));
        return s ? s->GetValue() : def;
    };
    auto slider = [this](const char* name) -> int {
        auto* s = dynamic_cast<wxSlider*>(wxWindow::FindWindowByName(name, const_cast<FlamePanel*>(this)));
        return s ? s->GetValue() : 0;
    };
    auto check = [this](const char* name, bool def) -> bool {
        auto* c = dynamic_cast<wxCheckBox*>(wxWindow::FindWindowByName(name, const_cast<FlamePanel*>(this)));
        return c ? c->GetValue() : def;
    };
    auto text = [this](const char* name) -> std::string {
        auto* t = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName(name, const_cast<FlamePanel*>(this)));
        return t ? t->GetValue().ToStdString() : std::string();
    };
    m["E_CHOICE_Flame_Action"] = choice("ID_CHOICE_Flame_Action");
    m["E_CHOICE_Flame_Repeat"] = choice("ID_CHOICE_Flame_Repeat");
    m["E_CHOICE_Flame_TimingTrack"] = choice("ID_CHOICE_Flame_TimingTrack");
    m["E_SPINCTRL_Flame_Preset"] = std::to_string(spin("ID_SPINCTRL_Flame_Preset", 8));
    m["E_SLIDER_Flame_Angle"] = std::to_string(slider("ID_SLIDER_Flame_Angle"));
    m["E_SLIDER_Flame_EndAngle"] = std::to_string(slider("ID_SLIDER_Flame_EndAngle"));
    m["E_TEXTCTRL_Flame_Pattern"] = text("ID_TEXTCTRL_Flame_Pattern");
    m["E_SPINCTRL_Flame_ShotMs"] = std::to_string(spin("ID_SPINCTRL_Flame_ShotMs", 500));
    m["E_SPINCTRL_Flame_GapMs"] = std::to_string(spin("ID_SPINCTRL_Flame_GapMs", 0));
    m["E_SPINCTRL_Flame_ShotMaxMs"] = std::to_string(spin("ID_SPINCTRL_Flame_ShotMaxMs", 0));
    m["E_TEXTCTRL_Flame_Rate"] = text("ID_TEXTCTRL_Flame_Rate");
    m["E_CHECKBOX_Flame_MoveEarly"] = check("ID_CHECKBOX_Flame_MoveEarly", false) ? "1" : "0";
    m["E_CHECKBOX_Flame_FitLength"] = check("ID_CHECKBOX_Flame_FitLength", true) ? "1" : "0";
    FlameParams p = FlameParams::FromSettings(m, "E_");
    // A saved "Preset" (the default) on a unit without presets, or "Sweep" on
    // one that can't move, is really a positional shot, same as the render.
    if (const DmxFlame* model = GetFlameModel()) {
        const auto& fx = model->GetFixture();
        if ((p.action == FlameParams::Action::Preset && !fx.HasPresets()) ||
            (p.action == FlameParams::Action::Sweep && !fx.HasAngle())) {
            p.action = FlameParams::Action::Shot;
        }
    }
    return p;
}

void FlamePanel::ShowProperty(const std::string& id, bool show) {
    if (auto* info = GetPropertyInfo(id)) {
        for (wxWindow* w : std::initializer_list<wxWindow*>{ info->slider, info->textCtrl, info->checkBox, info->choice, info->spinCtrl,
                                                             (wxWindow*)info->valueCurveBtn, info->buddySlider, info->buddyText, info->extraRow }) {
            if (w != nullptr) w->Show(show);
        }
    }
    if (wxWindow* label = wxWindow::FindWindowByName(wxString::Format("ID_STATICTEXT_%s", id), this)) {
        label->Show(show);
    }
}

void FlamePanel::ValidateWindow() {
    JsonEffectPanel::ValidateWindow();

    const DmxFlame* model = GetFlameModel();
    const FlameFixture* fx = model != nullptr ? &model->GetFixture() : nullptr;
    const bool hasAngle = fx == nullptr || fx->HasAngle();
    const bool hasPresets = fx == nullptr || fx->HasPresets();
    FlameParams p = ReadParams();
    const bool presetAction = p.action == FlameParams::Action::Preset && hasPresets;

    if (!hasAngle) {
        for (const char* id : { "Flame_Angle", "Flame_EndAngle", "Flame_Pattern", "Flame_MoveEarly" }) {
            ShowProperty(id, false);
        }
    }
    if (!hasAngle && !hasPresets) {
        ShowProperty("Flame_Action", false);
    }
    if (!hasPresets) {
        ShowProperty("Flame_Preset", false);
        if (_presetPicker) _presetPicker->Show(false);
        if (wxWindow* l = wxWindow::FindWindowByName("ID_STATICTEXT_Flame_PresetPicker", this)) l->Show(false);
    }
    if (_fanContainer && _fanSizer) _fanContainer->Show(_fanSizer, hasAngle, true);
    if (wxWindow* l = wxWindow::FindWindowByName("ID_STATICTEXT_Flame_FanButtons", this)) l->Show(hasAngle);
    if (_presetPicker) _presetPicker->Enable(presetAction);
    // positional shots: the user sets the length
    ShowProperty("Flame_ShotMs", !presetAction);
    if (presetAction) ShowProperty("Flame_ShotMaxMs", false);
    if (_setEnd) _setEnd->Show(hasAngle && p.action == FlameParams::Action::Sweep);
    // quick length buttons only for a single shot; repeats use the number box
    const bool shotButtons = !presetAction && p.repeat == FlameParams::Repeat::Once;
    if (_shotContainer && _shotSizer) _shotContainer->Show(_shotSizer, shotButtons, true);
    if (wxWindow* l = wxWindow::FindWindowByName("ID_STATICTEXT_Flame_ShotButtons", this)) l->Show(shotButtons);

    UpdateInfo();
    RelayoutRows();
}

void FlamePanel::SetDefaultParameters() {
    JsonEffectPanel::SetDefaultParameters();
    _cachedLead = 0;
    if (_setEnd) _setEnd->SetValue(false);
    _note.clear();
    RefreshForModel();
}

void FlamePanel::SetPanelStatus(Model* cls) {
    JsonEffectPanel::SetPanelStatus(cls);
    if (cls != nullptr) {
        _modelName = cls->GetFullName();
    }
    RefreshForModel();
}

void FlamePanel::RefreshForModel() {
    RebuildPresetPicker();
    RefreshFanButtons();
    RefreshShotButtons();
    const DmxFlame* model = GetFlameModel();
    if (model != nullptr && !model->GetFixture().HasPresets()) {
        auto* action = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("ID_CHOICE_Flame_Action", this));
        if (action != nullptr && action->GetStringSelection() == "Preset") {
            action->SetStringSelection("Shot");
        }
    }
    ValidateWindow();
}

void FlamePanel::RebuildPresetPicker() {
    if (_presetPicker == nullptr) return;
    _presetPicker->Clear();
    _presetPickerNumbers.clear();
    const DmxFlame* model = GetFlameModel();
    if (model == nullptr) return;
    for (const auto& pr : model->GetFixture().presets) {
        _presetPicker->Append(wxString::Format("%d  %s  (%s)", pr.n, wxString::FromUTF8(pr.name), Seconds(pr.lengthMs)));
        _presetPickerNumbers.push_back(pr.n);
    }
}

void FlamePanel::RelayoutRows() {
    if (_rowParent != nullptr) {
        _rowParent->Layout();
        if (auto* sw = dynamic_cast<wxScrolledWindow*>(_rowParent)) {
            sw->FitInside();
        }
    }
    Layout();
}

void FlamePanel::RefreshFanButtons() {
    if (_fanGrid == nullptr || _rowParent == nullptr) return;
    _fanGrid->Clear(true);
    _fanButtons.clear();

    const DmxFlame* model = GetFlameModel();
    if (model != nullptr && model->GetFixture().HasAngle()) {
        const auto& fx = model->GetFixture();
        int count = std::min(fx.PositionCount(), 31);
        for (int i = 0; i < count; ++i) {
            const auto& pos = fx.positions[model->GetFlip() ? count - 1 - i : i];
            auto* b = new wxButton(_rowParent, wxNewId(), wxString::Format("%d", pos.n), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            b->SetMinSize(wxSize(b->GetTextExtent("888").GetWidth() + 12, -1));
            b->SetToolTip(wxString::Format(_("Position %d (%s). Click to set the angle. Shift-click adds it to the angle pattern."), pos.n, Degrees(pos.deg)));
            b->Enable(model->IsPositionAllowed(pos.n));
            int n = pos.n;
            b->Bind(wxEVT_BUTTON, [this, n](wxCommandEvent&) {
                OnFanButton(n, wxGetKeyState(WXK_SHIFT));
            });
            _fanGrid->Add(b, 0, wxEXPAND, 0);
            _fanButtons.push_back(b);
        }
    }
    RelayoutRows();
}

void FlamePanel::RefreshShotButtons() {
    if (_shotSizer == nullptr || _rowParent == nullptr) return;
    _shotSizer->Clear(true);
    _shotButtons.clear();
    const DmxFlame* model = GetFlameModel();
    std::vector<int> list;
    if (model != nullptr) {
        list = model->GetFixture().shotPresetsMs;
    }
    if (list.empty()) {
        list = { 250, 500, 1000, 2000 };
    }
    for (int ms : list) {
        auto* b = new wxButton(_rowParent, wxNewId(), Seconds(ms), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        b->SetToolTip(_("Single shot: makes the effect this long. Repeated shots: sets each shot's length."));
        b->Bind(wxEVT_BUTTON, [this, ms](wxCommandEvent&) {
            OnShotButton(ms);
        });
        _shotSizer->Add(b, 0, wxEXPAND, 0);
        _shotButtons.push_back(b);
    }
    RelayoutRows();
}

void FlamePanel::SetAngleSlider(const char* id, int deg) {
    deg = std::clamp(deg, FLAME_ANGLE_MIN, FLAME_ANGLE_MAX);
    if (auto* s = dynamic_cast<wxSlider*>(wxWindow::FindWindowByName(wxString("ID_SLIDER_") + id, this))) {
        s->SetValue(deg);
    }
    if (auto* t = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName(wxString("IDD_TEXTCTRL_") + id, this))) {
        t->ChangeValue(wxString::Format("%d", deg));
    }
}

void FlamePanel::OnFanButton(int position, bool addToPattern) {
    const DmxFlame* model = GetFlameModel();
    if (model == nullptr) return;
    const auto& fx = model->GetFixture();
    FlameParams p = ReadParams();

    if (addToPattern) {
        if (auto* t = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName("ID_TEXTCTRL_Flame_Pattern", this))) {
            wxString v = t->GetValue().Trim().Trim(false);
            t->ChangeValue(v.empty() ? wxString::Format("%d", position) : v + wxString::Format(",%d", position));
        }
    } else {
        if (p.action == FlameParams::Action::Preset) {
            // an angle means a positional shot, not a preset
            if (auto* a = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("ID_CHOICE_Flame_Action", this))) {
                a->SetStringSelection("Shot");
            }
            ValidateWindow();
        }
        int deg = (int)std::lround(fx.PositionDeg(position));
        bool end = p.action == FlameParams::Action::Sweep && _setEnd != nullptr && _setEnd->GetValue();
        SetAngleSlider(end ? "Flame_EndAngle" : "Flame_Angle", deg);
    }
    FireChangeEvent();
    OnUserChange();
}

void FlamePanel::OnShotButton(int ms) {
    if (auto* sp = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("ID_SPINCTRL_Flame_ShotMs", this))) {
        sp->SetValue(ms);
    }
    FireChangeEvent();
    OnUserChange();
}

void FlamePanel::OnPresetPicked() {
    if (_presetPicker == nullptr) return;
    int sel = _presetPicker->GetSelection();
    if (sel < 0 || sel >= (int)_presetPickerNumbers.size()) return;
    if (auto* s = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("ID_SPINCTRL_Flame_Preset", this))) {
        s->SetValue(_presetPickerNumbers[sel]);
    }
    FireChangeEvent();
    OnUserChange();
}

int FlamePanel::CurrentLead() const {
    const DmxFlame* model = GetFlameModel();
    if (model == nullptr) return 0;
    FlameParams p = ReadParams();
    if (!p.moveEarly || p.repeat == FlameParams::Repeat::Marks) return 0;
    int ft = mSequenceElements != nullptr ? mSequenceElements->GetFrameMS() : 25;
    return AlignUp(FlameEffect::FirstLeadMs(p, model), ft);
}

void FlamePanel::OnUserChange() {
    _note.clear();
    UpdateInfo();
    ScheduleApplyLength();
}

void FlamePanel::ScheduleApplyLength() {
    if (_applyPending) return;
    _applyPending = true;
    // After the change has reached the effect's settings.
    CallAfter([this]() {
        _applyPending = false;
        ApplyLength();
    });
}

void FlamePanel::ApplyLength() {
    xLightsFrame* frame = xLightsApp::GetFrame();
    if (frame == nullptr) return;
    Effect* eff = frame->GetSelectedEffectForPanel();
    if (eff == nullptr || eff->GetEffectName() != "Flame") return;
    const DmxFlame* model = GetFlameModel();
    if (model == nullptr) return;

    FlameParams p = ReadParams();
    int ft = mSequenceElements != nullptr ? mSequenceElements->GetFrameMS() : 25;
    int newLead = CurrentLead();
    int s0 = eff->GetStartTimeMS();
    int e0 = eff->GetEndTimeMS();

    // keep the moment the flame fires where it was
    int s = std::max(0, s0 + _cachedLead - newLead);
    int e = e0;
    int fit = p.fitLength ? FlameEffect::FitLengthMs(p, model, ft) : 0;
    if (fit > 0) {
        e = s + fit;
    } else if (p.action != FlameParams::Action::Preset && p.repeat == FlameParams::Repeat::Once) {
        // a single positional shot: the length box sets the bar
        e = s + newLead + AlignUp(p.shotMs, ft);
    }

    if (s != s0 || e != e0) {
        std::string note;
        _selfResizing = true;
        bool moved = frame->SetSelectedEffectTimes("Flame", s, e, note);
        _selfResizing = false;
        _note = note;
        if (moved) {
            _trackedEffect = eff;
            _trackedStart = eff->GetStartTimeMS();
            _trackedEnd = eff->GetEndTimeMS();
            _cachedLead = newLead;
        }
    } else {
        _cachedLead = newLead;
    }
    UpdateInfo();
}

void FlamePanel::SetEffectTimeRange(int startTimeMs, int endTimeMs) {
    if (startTimeMs == 0 && endTimeMs == 0) {
        return;
    }
    // The effect's settings are loaded into the controls after this call.
    CallAfter([this, startTimeMs, endTimeMs]() {
        xLightsFrame* frame = xLightsApp::GetFrame();
        Effect* eff = frame != nullptr ? frame->GetSelectedEffectForPanel() : nullptr;
        if (eff == nullptr || eff->GetEffectName() != "Flame") {
            return;
        }
        if (const DmxFlame* m = GetFlameModel(); m != nullptr && !m->GetFixture().HasPresets()) {
            auto* action = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("ID_CHOICE_Flame_Action", this));
            if (action != nullptr && action->GetStringSelection() == "Preset") {
                action->SetStringSelection("Shot");
                FireChangeEvent();
                ValidateWindow();
            }
        }
        // Selecting an effect never moves it: whatever lead it was saved with
        // is taken as already applied.
        _cachedLead = CurrentLead();

        FlameParams p = ReadParams();
        const DmxFlame* model = GetFlameModel();
        int ft = mSequenceElements != nullptr ? mSequenceElements->GetFrameMS() : 25;
        int fit = (model != nullptr && p.fitLength) ? FlameEffect::FitLengthMs(p, model, ft) : 0;
        int len = endTimeMs - startTimeMs;

        if (eff == _trackedEffect && !_selfResizing && fit > 0 && len != (_trackedEnd - _trackedStart) && len != fit) {
            // resized by hand: stop fitting this effect
            if (auto* c = dynamic_cast<wxCheckBox*>(wxWindow::FindWindowByName("ID_CHECKBOX_Flame_FitLength", this))) {
                c->SetValue(false);
                FireChangeEvent();
                _note = "Fit length turned off because the effect was resized by hand.";
            }
        } else if (eff != _trackedEffect && fit > 0 && len != fit && !_selfResizing) {
            // e.g. a preset just dropped from the palette: stretch it to the
            // preset's length (its start stays put)
            std::string note;
            _selfResizing = true;
            frame->SetSelectedEffectTimes("Flame", startTimeMs, startTimeMs + fit, note);
            _selfResizing = false;
            _note = note;
        }
        if (p.action != FlameParams::Action::Preset && p.repeat == FlameParams::Repeat::Once) {
            if (auto* sp = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("ID_SPINCTRL_Flame_ShotMs", this))) {
                int ms = std::max(sp->GetMin(), eff->GetEndTimeMS() - eff->GetStartTimeMS() - _cachedLead);
                if (sp->GetValue() != ms) {
                    sp->SetValue(std::min(ms, sp->GetMax()));
                    FireChangeEvent();
                }
            }
        }
        _trackedEffect = eff;
        _trackedStart = eff->GetStartTimeMS();
        _trackedEnd = eff->GetEndTimeMS();
        UpdateInfo();
    });
}

void FlamePanel::UpdateInfo() {
    if (_info == nullptr) return;
    const DmxFlame* model = GetFlameModel();
    if (model == nullptr) {
        _info->SetLabel(_("Put this effect on a DMX Flame model to pick its presets and angles."));
        return;
    }
    const auto& fx = model->GetFixture();
    FlameParams p = ReadParams();
    wxString s;
    bool preset = p.action == FlameParams::Action::Preset && fx.HasPresets();
    if (preset) {
        const FlamePreset* pr = fx.FindPreset(p.preset);
        if (pr == nullptr) {
            s = wxString::Format(_("Preset %d isn't on the %s."), p.preset, wxString::FromUTF8(fx.name));
        } else {
            s = wxString::Format(_("Preset %d: %s, %s."), pr->n, wxString::FromUTF8(pr->name), Seconds(pr->lengthMs));
        }
        if (_presetPicker != nullptr) {
            auto it = std::find(_presetPickerNumbers.begin(), _presetPickerNumbers.end(), p.preset);
            _presetPicker->SetSelection(it == _presetPickerNumbers.end() ? wxNOT_FOUND : (int)(it - _presetPickerNumbers.begin()));
        }
    } else if (p.action == FlameParams::Action::Sweep && fx.HasAngle()) {
        s = wxString::Format(_("Sweep %s to %s while firing."), Degrees(model->ClampAngle(p.angleDeg)), Degrees(model->ClampAngle(p.endAngleDeg)));
    } else {
        wxString len = p.repeat == FlameParams::Repeat::Once ? wxString::Format(_("%s shot"), Seconds(p.shotMs)) : wxString::Format(_("%s shots"), Seconds(p.shotMs));
        s = fx.HasAngle() ? wxString::Format(_("%s at %s."), len, Degrees(model->ClampAngle(p.angleDeg))) : len + ".";
    }

    if (p.repeat == FlameParams::Repeat::Random) {
        wxString len = preset ? wxString(_("preset length")) :
            (p.shotMaxMs > p.shotMs ? wxString::Format("%s\u2013%s", Seconds(p.shotMs), Seconds(p.shotMaxMs)) : Seconds(p.shotMs));
        s += " " + wxString::Format(_("Random: about %.1f shots/s per unit, %s each."), p.rate, len);
    }
    int units = FlameUnitCount();
    if (units > 1) {
        s += " " + wxString::Format(_("Group of %d flame units; each gets its own channels%s."), units,
                                    p.repeat == FlameParams::Repeat::Random ? wxString(_(" and its own random pattern")) : wxString());
    }
    int ft = mSequenceElements != nullptr ? mSequenceElements->GetFrameMS() : 25;
    int lead = CurrentLead();
    int fit = p.fitLength ? FlameEffect::FitLengthMs(p, model, ft) : 0;
    if (fit > 0) {
        s += " " + wxString::Format(_("Bar fits %s"), Seconds(fit));
        s += lead > 0 ? wxString::Format(_(" (includes %d ms nozzle move)."), lead) : wxString(".");
    } else if (lead > 0) {
        s += " " + wxString::Format(_("Starts %d ms early to move the nozzle."), lead);
    }
    if (fx.id == FlameFixtures::ShowvenId) {
        s += "\n" + wxString(_("The pump arm (CH6) is left to your flame panel."));
    } else if (fx.id == FlameFixtures::GFlameId) {
        s += "\n" + wxString(_("The safety channel is left to your flame panel."));
    }
    if (!_note.empty()) {
        s += "\n" + wxString::FromUTF8(_note);
    }
    _info->SetLabel(s);
    // wrap narrow so the info text never widens the panel
    _info->Wrap(std::clamp(GetClientSize().GetWidth() - 20, 180, 240));
}
