/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "OutputSettingsPanel.h"

//(*InternalHeaders(OutputSettingsPanel)
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/gbsizer.h>
#include <wx/intl.h>
#include <wx/stattext.h>
#include <wx/string.h>
//*)

#include <wx/preferences.h>
#include <wx/textctrl.h>
#include <wx/config.h>
#include "outputs/ArtNetTimecode.h"
#include "xLightsMain.h"
#include "utils/ip_utils.h"

//(*IdInit(OutputSettingsPanel)
const long OutputSettingsPanel::ID_CHECKBOX1 = wxNewId();
const long OutputSettingsPanel::ID_STATICTEXT1 = wxNewId();
const long OutputSettingsPanel::ID_CHOICE1 = wxNewId();
const long OutputSettingsPanel::ID_CHOICE2 = wxNewId();
const long OutputSettingsPanel::ID_CHOICE3 = wxNewId();
//*)

BEGIN_EVENT_TABLE(OutputSettingsPanel,wxPanel)
	//(*EventTable(OutputSettingsPanel)
	//*)
END_EVENT_TABLE()

OutputSettingsPanel::OutputSettingsPanel(wxWindow* parent,xLightsFrame *f,wxWindowID id,const wxPoint& pos,const wxSize& size) : frame(f)
{
	//(*Initialize(OutputSettingsPanel)
	wxGridBagSizer* GridBagSizer1;
	wxStaticText* StaticText2;
	wxStaticText* StaticText3;

	Create(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, _T("id"));
	GridBagSizer1 = new wxGridBagSizer(0, 0);
	FrameSyncCheckBox = new wxCheckBox(this, ID_CHECKBOX1, _("Use Frame Sync"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_CHECKBOX1"));
	FrameSyncCheckBox->SetValue(false);
	GridBagSizer1->Add(FrameSyncCheckBox, wxGBPosition(0, 0), wxGBSpan(1, 2), wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	StaticText1 = new wxStaticText(this, ID_STATICTEXT1, _("Force Local IP"), wxDefaultPosition, wxDefaultSize, 0, _T("ID_STATICTEXT1"));
	GridBagSizer1->Add(StaticText1, wxGBPosition(1, 0), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	StaticText2 = new wxStaticText(this, wxID_ANY, _("Duplicate Frames to Suppress"), wxDefaultPosition, wxDefaultSize, 0, _T("wxID_ANY"));
	GridBagSizer1->Add(StaticText2, wxGBPosition(2, 0), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	StaticText3 = new wxStaticText(this, wxID_ANY, _("API Port"), wxDefaultPosition, wxDefaultSize, 0, _T("wxID_ANY"));
	GridBagSizer1->Add(StaticText3, wxGBPosition(3, 0), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	ForceLocalIPChoice = new wxChoice(this, ID_CHOICE1, wxDefaultPosition, wxDefaultSize, 0, 0, 0, wxDefaultValidator, _T("ID_CHOICE1"));
	ForceLocalIPChoice->SetSelection( ForceLocalIPChoice->Append(wxEmptyString) );
	GridBagSizer1->Add(ForceLocalIPChoice, wxGBPosition(1, 1), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	DuplicateSuppressChoice = new wxChoice(this, ID_CHOICE2, wxDefaultPosition, wxDefaultSize, 0, 0, 0, wxDefaultValidator, _T("ID_CHOICE2"));
	DuplicateSuppressChoice->SetSelection( DuplicateSuppressChoice->Append(_("None")) );
	DuplicateSuppressChoice->Append(_("10"));
	DuplicateSuppressChoice->Append(_("20"));
	DuplicateSuppressChoice->Append(_("40"));
	GridBagSizer1->Add(DuplicateSuppressChoice, wxGBPosition(2, 1), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	xFadexScheduleChoice = new wxChoice(this, ID_CHOICE3, wxDefaultPosition, wxDefaultSize, 0, 0, 0, wxDefaultValidator, _T("ID_CHOICE3"));
	xFadexScheduleChoice->SetSelection( xFadexScheduleChoice->Append(_("Disabled")) );
	xFadexScheduleChoice->Append(_("Port A (49913)"));
	xFadexScheduleChoice->Append(_("Port B (49914)"));
	GridBagSizer1->Add(xFadexScheduleChoice, wxGBPosition(3, 1), wxDefaultSpan, wxALL|wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL, 5);
	SetSizer(GridBagSizer1);
	GridBagSizer1->Fit(this);
	GridBagSizer1->SetSizeHints(this);

	Connect(ID_CHECKBOX1,wxEVT_COMMAND_CHECKBOX_CLICKED,(wxObjectEventFunction)&OutputSettingsPanel::OnFrameSyncCheckBoxClick);
	Connect(ID_CHOICE1,wxEVT_COMMAND_CHOICE_SELECTED,(wxObjectEventFunction)&OutputSettingsPanel::OnForceLocalIPChoiceSelect);
	Connect(ID_CHOICE2,wxEVT_COMMAND_CHOICE_SELECTED,(wxObjectEventFunction)&OutputSettingsPanel::OnDuplicateSuppressChoiceSelect);
	Connect(ID_CHOICE3,wxEVT_COMMAND_CHOICE_SELECTED,(wxObjectEventFunction)&OutputSettingsPanel::OnxFadexScheduleChoiceSelect);
	//*)
    
    std::string localIP = frame->_outputManager.GetGlobalForceLocalIP();
    auto ips = ip_utils::GetLocalIPs();

    if (ips.size() == 0) {
        if (localIP != "") {
           localIP = "";
        }
    }
    wxArrayString choices;
    choices.push_back("");
    int sel = -1;
    int i = 0;
    for (auto it: ips) {
        if (it == localIP) {
            sel = i+1;
        }
        i++;
        choices.push_back(it);
    }
    if (sel == -1) {
        sel = 0;
    }
    ForceLocalIPChoice->Set(choices);
    ForceLocalIPChoice->SetSelection(sel);

    // Art-Net timecode: lets video software (e.g. MadMapper) follow playback
    _tcEnable = new wxCheckBox(this, wxID_ANY, _("Send Art-Net Timecode"));
    _tcEnable->SetToolTip(_("While a sequence plays or you scrub, send its position as Art-Net timecode (UDP port 6454)."));
    GridBagSizer1->Add(_tcEnable, wxGBPosition(4, 0), wxGBSpan(1, 2), wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    GridBagSizer1->Add(new wxStaticText(this, wxID_ANY, _("Timecode Target IP")), wxGBPosition(5, 0), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    _tcIP = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1));
    _tcIP->SetHint(_("blank = broadcast"));
    _tcIP->SetToolTip(_("IP address of the computer running the video software. Leave blank to broadcast to the whole network."));
    GridBagSizer1->Add(_tcIP, wxGBPosition(5, 1), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    GridBagSizer1->Add(new wxStaticText(this, wxID_ANY, _("Timecode Frame Rate")), wxGBPosition(6, 0), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    _tcRate = new wxChoice(this, wxID_ANY);
    _tcRate->Append(_("24 fps (Film)"));
    _tcRate->Append(_("25 fps (EBU)"));
    _tcRate->Append(_("29.97 fps drop-frame"));
    _tcRate->Append(_("30 fps (SMPTE)"));
    GridBagSizer1->Add(_tcRate, wxGBPosition(6, 1), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    GridBagSizer1->Add(new wxStaticText(this, wxID_ANY, _("Timecode Offset (s)")), wxGBPosition(7, 0), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    _tcOffset = new wxTextCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1));
    _tcOffset->SetToolTip(_("Added to the sequence time, e.g. 3600 makes the sequence start at 01:00:00:00."));
    GridBagSizer1->Add(_tcOffset, wxGBPosition(7, 1), wxDefaultSpan, wxALL | wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL, 5);
    _tcEnable->Bind(wxEVT_CHECKBOX, &OutputSettingsPanel::OnTimecodeChange, this);
    _tcRate->Bind(wxEVT_CHOICE, &OutputSettingsPanel::OnTimecodeChange, this);
    _tcIP->Bind(wxEVT_TEXT, &OutputSettingsPanel::OnTimecodeChange, this);
    _tcOffset->Bind(wxEVT_TEXT, &OutputSettingsPanel::OnTimecodeChange, this);

    GridBagSizer1->Layout();
    GridBagSizer1->Fit(this);
    GridBagSizer1->SetSizeHints(this);
}

OutputSettingsPanel::~OutputSettingsPanel()
{
	//(*Destroy(OutputSettingsPanel)
	//*)
}

void OutputSettingsPanel::ApplyArtNetTimecodeConfig() {
    wxConfigBase* config = wxConfigBase::Get();
    bool en = false;
    int rate = 3;
    double offsetS = 0;
    wxString ip;
    config->Read("ArtNetTimecodeEnabled", &en, false);
    config->Read("ArtNetTimecodeIP", &ip, "");
    config->Read("ArtNetTimecodeRate", &rate, 3);
    config->Read("ArtNetTimecodeOffset", &offsetS, 0.0);
    if (rate < 0 || rate > 3) rate = 3;
    ArtNetTimecode::Configure(en, ip.Trim().Trim(false).ToStdString(), (ArtNetTimecode::Rate)rate, (int)(offsetS * 1000.0));
}

void OutputSettingsPanel::OnTimecodeChange(wxCommandEvent& event) {
    if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        TransferDataFromWindow();
    }
}

bool OutputSettingsPanel::TransferDataFromWindow() {
    if (_tcEnable != nullptr) {
        wxConfigBase* config = wxConfigBase::Get();
        double offsetS = 0;
        _tcOffset->GetValue().ToDouble(&offsetS);
        config->Write("ArtNetTimecodeEnabled", _tcEnable->IsChecked());
        config->Write("ArtNetTimecodeIP", _tcIP->GetValue().Trim().Trim(false));
        config->Write("ArtNetTimecodeRate", std::max(0, _tcRate->GetSelection()));
        config->Write("ArtNetTimecodeOffset", offsetS);
        config->Flush();
        ApplyArtNetTimecodeConfig();
    }
    frame->SetXFadePort(xFadexScheduleChoice->GetSelection());
    frame->SetE131Sync(FrameSyncCheckBox->IsChecked());
    frame->_outputManager.SetGlobalForceLocalIP(ForceLocalIPChoice->GetStringSelection());
    switch (DuplicateSuppressChoice->GetSelection()) {
        case 3:
            frame->SetSuppressDuplicateFrames(40);
            break;
        case 2:
            frame->SetSuppressDuplicateFrames(20);
            break;
        case 1:
            frame->SetSuppressDuplicateFrames(10);
            break;
        default:
            frame->SetSuppressDuplicateFrames(0);
            break;
    }
    return true;
}
bool OutputSettingsPanel::TransferDataToWindow() {
    if (_tcEnable != nullptr) {
        wxConfigBase* config = wxConfigBase::Get();
        bool en = false;
        int rate = 3;
        double offsetS = 0;
        wxString ip;
        config->Read("ArtNetTimecodeEnabled", &en, false);
        config->Read("ArtNetTimecodeIP", &ip, "");
        config->Read("ArtNetTimecodeRate", &rate, 3);
        config->Read("ArtNetTimecodeOffset", &offsetS, 0.0);
        _tcEnable->SetValue(en);
        _tcIP->ChangeValue(ip);
        _tcRate->SetSelection(rate >= 0 && rate <= 3 ? rate : 3);
        _tcOffset->ChangeValue(wxString::Format("%g", offsetS));
    }
    xFadexScheduleChoice->SetSelection(frame->XFadePort());
    FrameSyncCheckBox->SetValue(frame->E131Sync());
    switch (frame->SuppressDuplicateFrames()) {
        case 40:
            DuplicateSuppressChoice->SetSelection(3);
            break;
        case 20:
            DuplicateSuppressChoice->SetSelection(2);
            break;
        case 10:
            DuplicateSuppressChoice->SetSelection(1);
            break;
        default:
            DuplicateSuppressChoice->SetSelection(0);
            break;
    }
    return true;
}

void OutputSettingsPanel::OnFrameSyncCheckBoxClick(wxCommandEvent& event)
{
    if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        TransferDataFromWindow();
    }
}

void OutputSettingsPanel::OnForceLocalIPChoiceSelect(wxCommandEvent& event)
{
    if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        TransferDataFromWindow();
    }
}

void OutputSettingsPanel::OnDuplicateSuppressChoiceSelect(wxCommandEvent& event)
{
    if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        TransferDataFromWindow();
    }
}

void OutputSettingsPanel::OnxFadexScheduleChoiceSelect(wxCommandEvent& event)
{
    if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
        TransferDataFromWindow();
    }
}
