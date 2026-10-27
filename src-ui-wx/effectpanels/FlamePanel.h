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

#include "JsonEffectPanel.h"

#include <vector>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxStaticText;
class wxTextCtrl;
class DmxFlame;
struct FlameParams;

class FlamePanel : public JsonEffectPanel {
public:
    FlamePanel(wxWindow* parent, const nlohmann::json& metadata);
    ~FlamePanel() override = default;

    void ValidateWindow() override;
    void SetDefaultParameters() override;
    void SetPanelStatus(Model* cls) override;
    void SetEffectTimeRange(int startTimeMs, int endTimeMs) override;

protected:
    wxWindow* CreateCustomControl(wxWindow* parentWin, wxSizer* sizer,
                                  const nlohmann::json& prop, int cols) override;

private:
    wxWindow* BuildPresetPicker(wxWindow* parentWin, wxSizer* sizer);
    wxWindow* BuildFanButtons(wxWindow* parentWin, wxSizer* sizer);
    wxWindow* BuildShotButtons(wxWindow* parentWin, wxSizer* sizer);
    wxWindow* BuildInfo(wxWindow* parentWin, wxSizer* sizer);

    const DmxFlame* GetFlameModel() const;
    int FlameUnitCount() const;
    FlameParams ReadParams() const;
    void RefreshForModel();
    void RebuildPresetPicker();
    void RefreshFanButtons();
    void RefreshShotButtons();
    void UpdateInfo();
    void ShowProperty(const std::string& id, bool show);

    void OnUserChange();
    void ScheduleApplyLength();
    void ApplyLength();

    void OnFanButton(int position, bool addToPattern);
    void OnShotButton(int ms);
    void OnPresetPicked();

    void SetAngleSlider(const char* id, int deg);
    int CurrentLead() const;
    int SavedLead() const;
    void RelayoutRows();

    std::string _modelName;
    wxChoice* _presetPicker = nullptr;
    wxStaticText* _presetLabel = nullptr;
    std::vector<int> _presetPickerNumbers;
    wxCheckBox* _setEnd = nullptr;
    // Button rows are built straight into the panel's own sizers (a nested
    // wxPanel never got a size, so its buttons drew over other controls).
    wxWindow* _rowParent = nullptr;
    wxSizer* _fanContainer = nullptr;
    wxSizer* _fanSizer = nullptr;
    wxSizer* _fanGrid = nullptr;
    std::vector<wxButton*> _fanButtons;
    wxSizer* _shotContainer = nullptr;
    wxSizer* _shotSizer = nullptr;
    std::vector<wxButton*> _shotButtons;
    wxStaticText* _info = nullptr;
    std::string _note;
    // Lead the effect had before the current edit (from its saved settings),
    // so an edit shifts the start by exactly the change in lead.
    int _pendingOldLead = -1;
    bool _loading = false;
    int _applyGen = 0;

    bool _applyPending = false;
    bool _selfResizing = false;
    const void* _trackedEffect = nullptr;
    int _trackedStart = -1;
    int _trackedEnd = -1;
};
