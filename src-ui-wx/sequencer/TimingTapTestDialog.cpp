/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "TimingTapTestDialog.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>

#include "xLightsMain.h"
#include "sequencer/MainSequencer.h"
#include "sequencer/TimeLine.h"
#include "media/AudioManager.h"
#include "render/Effect.h"
#include "render/EffectLayer.h"
#include "render/Element.h"
#include "render/SequenceElements.h"
#include "render/SequenceFile.h"
#include "settings/XLightsConfigAdapter.h"
#include "xLightsVersion.h"

#include <spdlog/fmt/fmt.h>

namespace {

constexpr int kBeatIntervalMS = 600; // 100 BPM
constexpr int kCountIn = 4;          // beats before scoring starts
constexpr int kScoredBeats = 16;
constexpr int kFirstBeatMS = 1000;
constexpr int kFlashOnMS = 90;
constexpr int kMatchWindowMS = 80;   // a tap further than this from any beat is a miss
constexpr int kMinTaps = 6;
constexpr double kTwoPi = 6.283185307179586;

// A WAV of short clicks on a steady beat; the count-in clicks are lower pitched.
bool WriteClickWav(const std::string& path, const std::vector<long>& clickTimesMS, long lengthMS) {
    const int rate = 44100;
    const size_t samples = (size_t)lengthMS * rate / 1000;
    std::vector<int16_t> pcm(samples, 0);
    for (size_t i = 0; i < clickTimesMS.size(); i++) {
        const double freq = i < (size_t)kCountIn ? 1000.0 : 1800.0;
        const size_t start = (size_t)clickTimesMS[i] * rate / 1000;
        const size_t len = rate * 30 / 1000;
        for (size_t s = 0; s < len && start + s < samples; s++) {
            const double t = (double)s / rate;
            const double v = std::sin(kTwoPi * freq * t) * std::exp(-t / 0.006) * 0.8;
            pcm[start + s] = (int16_t)std::lround(v * 32767.0);
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;
    }
    auto put32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto put16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = (uint32_t)(samples * 2);
    f.write("RIFF", 4);
    put32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    put32(16);
    put16(1);        // PCM
    put16(1);        // mono
    put32(rate);
    put32(rate * 2); // byte rate
    put16(2);        // block align
    put16(16);       // bits
    f.write("data", 4);
    put32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return (bool)f;
}

long NearestBeat(const std::vector<long>& beats, long t) {
    auto it = std::lower_bound(beats.begin(), beats.end(), t);
    if (it == beats.end()) {
        return beats.back();
    }
    if (it == beats.begin()) {
        return *it;
    }
    return (t - *(it - 1) <= *it - t) ? *(it - 1) : *it;
}

int Median(std::vector<int> v) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Finds the shift that best lines the taps up with the beats. Each tap's cost is its
// distance to the nearest shifted beat, capped so stray taps can't drag the fit.
TimingTapTestDialog::Result Score(const std::vector<long>& beats, const std::vector<long>& taps, int loMS, int hiMS, bool checkAmbiguous) {
    TimingTapTestDialog::Result r;
    if (beats.empty() || taps.empty()) {
        return r;
    }
    std::vector<std::pair<int, long>> costs;
    for (int d = loMS; d <= hiMS; d += 5) {
        long cost = 0;
        for (long t : taps) {
            cost += std::min<long>(std::labs(t - d - NearestBeat(beats, t - d)), kMatchWindowMS);
        }
        costs.emplace_back(d, cost);
    }
    auto best = *std::min_element(costs.begin(), costs.end(), [](const auto& a, const auto& b) {
        return a.second < b.second || (a.second == b.second && std::abs(a.first) < std::abs(b.first));
    });

    std::vector<int> resid;
    for (long t : taps) {
        const long rr = t - best.first - NearestBeat(beats, t - best.first);
        if (std::labs(rr) < kMatchWindowMS) {
            resid.push_back((int)rr);
        }
    }
    r.taps = (int)resid.size();
    if ((int)resid.size() < kMinTaps) {
        return r;
    }
    const int mid = Median(resid);
    r.offsetMS = best.first + mid;
    std::vector<int> dev;
    for (int x : resid) {
        dev.push_back(std::abs(x - mid));
        r.lags.push_back(best.first + x);
    }
    r.spreadMS = Median(dev);
    r.taps = (int)resid.size();
    r.done = true;

    if (checkAmbiguous) {
        // Evenly spaced marks fit almost as well one beat over; say so instead of guessing.
        const long tolerance = best.second + best.second / 10 + 2 * (long)taps.size();
        const std::pair<int, long>* alt = nullptr;
        for (const auto& c : costs) {
            if (std::abs(c.first - best.first) >= 120 && c.second <= tolerance && (alt == nullptr || c.second < alt->second)) {
                alt = &c;
            }
        }
        if (alt != nullptr) {
            r.ambiguous = true;
            r.altOffsetMS = alt->first;
        }
    }
    return r;
}

std::string SignedMS(int ms) {
    return fmt::format("{}{} ms", ms > 0 ? "+" : "", ms);
}

std::string LateEarly(int ms) {
    if (ms == 0) {
        return "right on the beat";
    }
    return fmt::format("{} ms {}", std::abs(ms), ms > 0 ? "late" : "early");
}

} // namespace

class TapTestPanel : public wxPanel {
public:
    explicit TapTestPanel(wxWindow* parent) :
        wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 220), wxFULL_REPAINT_ON_RESIZE) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &TapTestPanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onTap) {
                onTap();
            }
        });
    }

    void SetLit(bool lit) {
        if (_lit != lit) {
            _lit = lit;
            Refresh();
            Update();
        }
    }
    void SetText(const wxString& headline, const wxString& detail) {
        if (headline != _big || detail != _small) {
            _big = headline;
            _small = detail;
            Refresh();
        }
    }

    const wxString& BigText() const { return _big; }

    std::function<void()> onTap;

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize sz = GetClientSize();
        dc.SetBackground(wxBrush(wxColour(28, 28, 32)));
        dc.Clear();

        const int radius = std::max(10, std::min(sz.x, sz.y) / 4);
        const wxPoint centre(sz.x / 2, radius + 16);
        dc.SetPen(wxPen(wxColour(90, 90, 100), 2));
        dc.SetBrush(wxBrush(_lit ? wxColour(255, 214, 60) : wxColour(48, 48, 56)));
        dc.DrawCircle(centre, radius);

        dc.SetTextForeground(*wxWHITE);
        wxFont f = GetFont();
        f.SetPointSize(f.GetPointSize() + 3);
        f.MakeBold();
        dc.SetFont(f);
        wxSize ts = dc.GetTextExtent(_big);
        int y = centre.y + radius + 12;
        dc.DrawText(_big, (sz.x - ts.x) / 2, y);
        dc.SetFont(GetFont());
        dc.SetTextForeground(wxColour(190, 190, 200));
        ts = dc.GetTextExtent(_small);
        dc.DrawText(_small, (sz.x - ts.x) / 2, y + ts.y + 10);
    }

    bool _lit = false;
    wxString _big;
    wxString _small;
};

TimingTapTestDialog::TimingTapTestDialog(wxWindow* parent, xLightsFrame* frame) :
    wxDialog(parent, wxID_ANY, _("Timing Tap Test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    _frame(frame), _timer(this) {
    // Sequence playback would fight the test for the audio device.
    _frame->StopPlayback();

    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* intro = new wxStaticText(this, wxID_ANY,
        _("Use the speakers or headphones you sequence with. In each test, tap the Space bar (or click the circle) "
          "in time with the beat. The first four beats are a count-in and are not scored. Esc stops a test."));
    intro->Wrap(560);
    top->Add(intro, 0, wxALL | wxEXPAND, 10);

    _panel = new TapTestPanel(this);
    _panel->onTap = [this]() { RegisterTap(); };
    _panel->SetText(_("Choose a test"), _("1 and 2 take about 13 seconds each"));
    top->Add(_panel, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

    auto* testRow = new wxBoxSizer(wxHORIZONTAL);
    _clicksButton = new wxButton(this, wxID_ANY, _("1. Steady clicks"));
    _clicksButton->SetToolTip(_("Clicks through xLights' sound output, timed with the same clock that tapped timing marks use."));
    _flashesButton = new wxButton(this, wxID_ANY, _("2. Steady flashes"));
    _flashesButton->SetToolTip(_("The same beat as a flashing circle, with no sound. Compared with test 1, it shows how late this computer's sound is."));
    testRow->Add(_clicksButton, 1, wxALL, 5);
    testRow->Add(_flashesButton, 1, wxALL, 5);
    top->Add(testRow, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 5);

    auto* songBox = new wxStaticBoxSizer(wxVERTICAL, this, _("3. Tap along to the song"));
    auto* songHelp = new wxStaticText(songBox->GetStaticBox(), wxID_ANY,
        _("Taps along to the open sequence's audio and compares them with a timing track you trust "
          "(one already checked against the waveform). Tap the beats that track marks."));
    songHelp->Wrap(540);
    songBox->Add(songHelp, 0, wxALL | wxEXPAND, 5);
    auto* songGrid = new wxFlexGridSizer(2, 5, 5);
    songGrid->AddGrowableCol(1);
    songGrid->Add(new wxStaticText(songBox->GetStaticBox(), wxID_ANY, _("Reference timing track")), 0, wxALIGN_CENTER_VERTICAL);
    _trackChoice = new wxChoice(songBox->GetStaticBox(), wxID_ANY);
    songGrid->Add(_trackChoice, 1, wxEXPAND);
    songGrid->Add(new wxStaticText(songBox->GetStaticBox(), wxID_ANY, _("Start at (seconds)")), 0, wxALIGN_CENTER_VERTICAL);
    _startSpin = new wxSpinCtrl(songBox->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 3600, 0);
    songGrid->Add(_startSpin, 0);
    songGrid->Add(new wxStaticText(songBox->GetStaticBox(), wxID_ANY, _("Length (seconds)")), 0, wxALIGN_CENTER_VERTICAL);
    _lengthSpin = new wxSpinCtrl(songBox->GetStaticBox(), wxID_ANY, "20", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 60, 20);
    songGrid->Add(_lengthSpin, 0);
    songBox->Add(songGrid, 0, wxALL | wxEXPAND, 5);
    _songButton = new wxButton(songBox->GetStaticBox(), wxID_ANY, _("Start song test"));
    songBox->Add(_songButton, 0, wxALL | wxALIGN_LEFT, 5);
    top->Add(songBox, 0, wxALL | wxEXPAND, 10);

    _resultsText = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 220), wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    top->Add(_resultsText, 1, wxLEFT | wxRIGHT | wxEXPAND, 10);

    _currentOffsetLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(_currentOffsetLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    _copyButton = new wxButton(this, wxID_ANY, _("Copy Results"));
    _applyButton = new wxButton(this, wxID_ANY, _("Use Measured Offset"));
    _applyButton->SetToolTip(_("Sets Preferences > Effects Grid > Timing tap offset, which is taken off timing marks tapped during playback."));
    bottom->Add(_copyButton, 0, wxALL, 5);
    bottom->Add(_applyButton, 0, wxALL, 5);
    bottom->AddStretchSpacer();
    bottom->Add(new wxButton(this, wxID_CLOSE, _("Close")), 0, wxALL, 5);
    top->Add(bottom, 0, wxALL | wxEXPAND, 5);

    // Song test inputs from the open sequence.
    if (xLightsFrame::CurrentSeqXmlFile != nullptr) {
        auto& se = _frame->GetSequenceElements();
        for (int i = 0; i < se.GetNumberOfTimingElements(); i++) {
            TimingElement* te = se.GetTimingElement(i);
            if (te != nullptr) {
                _trackChoice->Append(te->GetName());
            }
        }
        int startMS = 0;
        if (_frame->GetMainSequencer() != nullptr && _frame->GetMainSequencer()->PanelTimeLine != nullptr) {
            startMS = std::max(0, _frame->GetMainSequencer()->PanelTimeLine->GetCurrentPlayMarkerMS());
        }
        _startSpin->SetRange(0, std::max(1, xLightsFrame::CurrentSeqXmlFile->GetSequenceDurationMS() / 1000));
        _startSpin->SetValue(startMS / 1000);
    }
    if (_trackChoice->GetCount() > 0) {
        _trackChoice->SetSelection(0);
    }

    _clicksButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartTest(Test::Clicks); });
    _flashesButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartTest(Test::Flashes); });
    _songButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartTest(Test::Song); });
    _copyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(BuildReport()));
            wxTheClipboard->Close();
        }
    });
    _applyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const int ms = RecommendedOffset();
        _frame->SetTimingTapOffsetMS(ms);
        if (auto* config = GetXLightsConfig(); config != nullptr) {
            config->Write("xLightsTimingTapOffsetMS", ms);
        }
        UpdateResults();
    });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, wxID_CLOSE);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        if (_running != Test::None) {
            FinishTest(true);
        }
        e.Skip();
    });
    Bind(wxEVT_TIMER, &TimingTapTestDialog::OnTimer, this);
    Bind(wxEVT_CHAR_HOOK, &TimingTapTestDialog::OnCharHook, this);

    SetSizerAndFit(top);
    SetMinSize(GetSize());
    UpdateResults();
    UpdateButtons();
    CentreOnParent();
}

TimingTapTestDialog::~TimingTapTestDialog() {
    _timer.Stop();
    if (_clickAudio) {
        _clickAudio->AbsoluteStop();
        _clickAudio.reset();
    }
    if (_songAudio != nullptr) {
        _songAudio->Stop();
    }
    AudioManager::SetPlaybackRate((float)_frame->GetPlaySpeed());
    if (!_clickFile.empty()) {
        std::remove(_clickFile.c_str());
    }
}

long TimingTapTestDialog::NowMS() const {
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t0).count();
}

void TimingTapTestDialog::StartTest(Test t) {
    if (_running != Test::None) {
        return;
    }
    _beats.clear();
    _taps.clear();
    _scoredBeats.clear();
    _nextFlash = 0;
    _flashOffAt = -1;
    _lastTapWall = -1000;
    _songAudio = nullptr;

    if (t == Test::Clicks || t == Test::Flashes) {
        for (int i = 0; i < kCountIn + kScoredBeats; i++) {
            _beats.push_back(kFirstBeatMS + i * kBeatIntervalMS);
        }
    }

    // Tapped marks are always taken at normal speed here so the numbers match the setting.
    AudioManager::SetPlaybackRate(1.0f);

    if (t == Test::Clicks) {
        if (AudioManager::GetGlobalVolume() == 0) {
            wxMessageBox(_("xLights' volume is turned all the way down. Turn it up and try again."), _("Timing Tap Test"), wxOK | wxICON_WARNING, this);
            return;
        }
        const long lengthMS = _beats.back() + 800;
        if (_clickFile.empty()) {
            _clickFile = (wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + "xLightsTapTestClicks.wav").ToStdString();
        }
        if (!_clickAudio) {
            if (!WriteClickWav(_clickFile, _beats, lengthMS)) {
                wxMessageBox(_("Could not create the click track."), _("Timing Tap Test"), wxOK | wxICON_ERROR, this);
                return;
            }
            _clickAudio = std::make_unique<AudioManager>(_clickFile);
            for (int i = 0; i < 300 && _clickAudio->IsOk() && !_clickAudio->IsDataLoaded(); i++) {
                wxMilliSleep(10);
            }
            if (!_clickAudio->IsOk()) {
                _clickAudio.reset();
                wxMessageBox(_("Could not play the click track on this computer's sound output."), _("Timing Tap Test"), wxOK | wxICON_ERROR, this);
                return;
            }
        }
        _scoredBeats.assign(_beats.begin() + kCountIn, _beats.end());
        _panel->SetLit(false);
        _panel->SetText(_("Tap with each click"), _("Four low clicks to get the beat, then keep tapping"));
        // Seek + Play, as sequencer playback does: on SDL a length-limited Play
        // leaves Tell() reporting the wrong position.
        _clickAudio->Seek(0);
        _clickAudio->Play();
        _endMS = lengthMS + 300;
    } else if (t == Test::Flashes) {
        _scoredBeats.assign(_beats.begin() + kCountIn, _beats.end());
        _panel->SetText(_("Tap each time the circle lights"), _("No sound in this test"));
        _endMS = _beats.back() + 800;
    } else if (t == Test::Song) {
        if (xLightsFrame::CurrentSeqXmlFile == nullptr || _frame->GetPlaybackAudio() == nullptr) {
            wxMessageBox(_("Open a sequence that has audio first."), _("Timing Tap Test"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        if (_trackChoice->GetSelection() == wxNOT_FOUND) {
            wxMessageBox(_("This sequence has no timing tracks to compare against."), _("Timing Tap Test"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        _songTrack = _trackChoice->GetStringSelection().ToStdString();
        _songStartMS = _startSpin->GetValue() * 1000;
        const int lengthMS = _lengthSpin->GetValue() * 1000;
        TimingElement* te = _frame->GetSequenceElements().GetTimingElement(_songTrack);
        if (te != nullptr && te->GetEffectLayerCount() > 0) {
            EffectLayer* layer = te->GetEffectLayer(0);
            for (int i = 0; i < layer->GetEffectCount(); i++) {
                const int s = layer->GetEffect(i)->GetStartTimeMS();
                if (s >= _songStartMS && s <= _songStartMS + lengthMS) {
                    _beats.push_back(s);
                }
            }
        }
        std::sort(_beats.begin(), _beats.end());
        if (_beats.size() < (size_t)kMinTaps + 2) {
            wxMessageBox(wxString::Format(_("'%s' has only %d marks between %d and %d seconds. Pick a busier stretch or another track."),
                                          _songTrack, (int)_beats.size(), _songStartMS / 1000, (_songStartMS + lengthMS) / 1000),
                         _("Timing Tap Test"), wxOK | wxICON_INFORMATION, this);
            _beats.clear();
            return;
        }
        _scoredBeats = _beats;
        _songAudio = _frame->GetPlaybackAudio();
        _panel->SetLit(false);
        _panel->SetText(wxString::Format(_("Tap the beats of '%s'"), _songTrack), _("Playing the song"));
        _songAudio->Seek(_songStartMS);
        _songAudio->Play();
        _endMS = lengthMS + 300;
    }

    _running = t;
    _t0 = std::chrono::steady_clock::now();
    UpdateButtons();
    _panel->SetFocus();
    _timer.Start(4);
}

void TimingTapTestDialog::OnTimer(wxTimerEvent&) {
    const long now = NowMS();
    if (_running == Test::Flashes) {
        if (_flashOffAt >= 0 && now >= _flashOffAt) {
            _panel->SetLit(false);
            _flashOffAt = -1;
        }
        if (_nextFlash < _beats.size() && now >= _beats[_nextFlash]) {
            _panel->SetLit(true);
            // Score against when the circle was actually drawn, not when it was due.
            const long drawn = NowMS();
            if (_nextFlash >= (size_t)kCountIn) {
                _scoredBeats[_nextFlash - kCountIn] = drawn;
            }
            _beats[_nextFlash] = drawn;
            _flashOffAt = drawn + kFlashOnMS;
            _nextFlash++;
        }
    }
    if (now >= _endMS) {
        FinishTest(false);
    }
}

void TimingTapTestDialog::OnCharHook(wxKeyEvent& event) {
    if (_running == Test::None) {
        event.Skip();
        return;
    }
    const int k = event.GetKeyCode();
    if (k == WXK_ESCAPE) {
        FinishTest(true);
        return;
    }
    if (k == WXK_SHIFT || k == WXK_CONTROL || k == WXK_ALT || k == WXK_RAW_CONTROL || k == WXK_COMMAND) {
        return;
    }
    if (!event.IsAutoRepeat()) {
        RegisterTap();
    }
}

void TimingTapTestDialog::RegisterTap() {
    if (_running == Test::None) {
        return;
    }
    const long wall = NowMS();
    if (wall - _lastTapWall < 90) {
        return;
    }
    _lastTapWall = wall;

    long t = wall;
    if (_running == Test::Clicks && _clickAudio) {
        t = _clickAudio->Tell();
    } else if (_running == Test::Song && _songAudio != nullptr) {
        t = _songAudio->Tell();
    }
    _taps.push_back(t);
    if (_running != Test::Flashes) {
        _panel->SetText(_panel->BigText(), wxString::Format(_("%d taps"), (int)_taps.size()));
    }
}

void TimingTapTestDialog::FinishTest(bool cancelled) {
    _timer.Stop();
    const Test t = _running;
    _running = Test::None;
    if (_clickAudio) {
        _clickAudio->Stop();
    }
    if (_songAudio != nullptr) {
        _songAudio->Stop();
        _songAudio = nullptr;
    }
    _panel->SetLit(false);

    if (cancelled) {
        _panel->SetText(_("Test stopped"), wxEmptyString);
        UpdateButtons();
        return;
    }

    std::vector<long> taps;
    const long firstCounted = _scoredBeats.empty() ? 0 : _scoredBeats.front() - kBeatIntervalMS / 2;
    for (long tap : _taps) {
        if (t == Test::Song || tap >= firstCounted) {
            taps.push_back(tap);
        }
    }

    Result r;
    if (t == Test::Song) {
        // Hand-tapped marks are never far early; allow a little either way for anticipation.
        r = Score(_scoredBeats, taps, -150, 450, true);
        _song = r;
    } else {
        r = Score(_scoredBeats, taps, -250, 290, false);
        (t == Test::Clicks ? _clicks : _flashes) = r;
    }
    if (r.done) {
        _panel->SetText(wxString::Format(_("Taps landed %s"), LateEarly(r.offsetMS)),
                        wxString::Format(_("%d taps counted, typically within %d ms of each other"), r.taps, r.spreadMS));
    } else {
        _panel->SetText(_("Not enough taps on the beat"),
                        wxString::Format(_("%d taps heard, %d close to a beat. Try again, tapping steadily all the way through"),
                                         (int)taps.size(), r.taps));
    }
    UpdateResults();
    UpdateButtons();
}

void TimingTapTestDialog::UpdateButtons() {
    const bool idle = _running == Test::None;
    _clicksButton->Enable(idle);
    _flashesButton->Enable(idle);
    _songButton->Enable(idle && _trackChoice->GetCount() > 0);
    _trackChoice->Enable(idle);
    _startSpin->Enable(idle);
    _lengthSpin->Enable(idle);
    _copyButton->Enable(idle && (_clicks.done || _flashes.done || _song.done));
    const int rec = RecommendedOffset();
    _applyButton->Enable(idle && (_clicks.done || _song.done) && rec != _frame->TimingTapOffsetMS());
    _applyButton->SetLabel(wxString::Format(_("Use Measured Offset (%d ms)"), rec));
    Layout();
}

int TimingTapTestDialog::RecommendedOffset() const {
    // The song test is what tapping marks actually looks like; the click test is the fallback.
    if (_song.done && !_song.ambiguous) {
        return _song.offsetMS;
    }
    if (_clicks.done) {
        return _clicks.offsetMS;
    }
    return 0;
}

std::string TimingTapTestDialog::BuildReport() const {
    std::string s;
    const int frameMS = xLightsFrame::CurrentSeqXmlFile != nullptr ? xLightsFrame::CurrentSeqXmlFile->GetFrameMS() : 0;
    auto frames = [frameMS](int ms) -> std::string {
        if (frameMS <= 0) {
            return "";
        }
        return fmt::format(" (about {} frames at {} ms)", (int)std::lround(std::abs(ms) / (double)frameMS), frameMS);
    };
    auto line = [](const char* name, const Result& r) -> std::string {
        if (!r.done) {
            return fmt::format("  {:<16}not run\n", name);
        }
        return fmt::format("  {:<16}{:>8}   spread ±{} ms   {} taps\n", name, SignedMS(r.offsetMS), r.spreadMS, r.taps);
    };

    s += "TIMING TAP TEST RESULTS\n";
    s += "(+ means the tap came after the beat)\n\n";
    s += line("Steady clicks", _clicks);
    s += line("Steady flashes", _flashes);
    s += line("Song", _song);
    if (_song.done) {
        s += fmt::format("  Song test used '{}' from {}:{:02d}.\n", _songTrack, _songStartMS / 60000, (_songStartMS / 1000) % 60);
    }
    s += "\nWHAT IT MEANS\n";

    bool any = false;
    int soundDelay = 0;
    bool haveSound = _clicks.done && _flashes.done;
    if (haveSound) {
        any = true;
        soundDelay = _clicks.offsetMS - _flashes.offsetMS;
        if (soundDelay >= 100) {
            s += fmt::format("- TECHNICAL: this computer's sound is about {} ms late (the click test landed {} ms later than the flash test).\n"
                             "  Common causes: Bluetooth headphones or speakers, sound going through a TV or receiver over HDMI,\n"
                             "  or audio 'enhancements' in the sound settings. Wired headphones usually fix it.\n",
                             soundDelay, soundDelay);
        } else if (soundDelay >= 50) {
            s += fmt::format("- Sound is a little late on this computer: about {} ms. Worth cancelling with the offset setting.\n", soundDelay);
        } else {
            s += "- Sound output is fine: no meaningful delay between hearing a click and seeing a flash.\n";
        }
    }
    if (_clicks.done && !haveSound) {
        any = true;
        s += fmt::format("- On a steady click, taps land {}{}. Run the flash test to tell whether that is the computer or the tapping.\n",
                         LateEarly(_clicks.offsetMS), frames(_clicks.offsetMS));
    }
    if (_flashes.done && !_clicks.done) {
        any = true;
        s += fmt::format("- On a steady flash, taps land {}. Run the click test to compare with sound.\n", LateEarly(_flashes.offsetMS));
    }
    if (_song.done) {
        any = true;
        if (_song.ambiguous) {
            s += fmt::format("- Song test is UNCLEAR: '{}' is so evenly spaced that the taps fit about as well at {} as at {}.\n"
                             "  Try a track with uneven gaps between marks (accents, vocal cues, drum fills).\n",
                             _songTrack, SignedMS(_song.offsetMS), SignedMS(_song.altOffsetMS));
        } else {
            s += fmt::format("- Along to the song, taps land {}{}.\n", LateEarly(_song.offsetMS), frames(_song.offsetMS));
            if (_clicks.done) {
                const int extra = _song.offsetMS - _clicks.offsetMS;
                if (extra >= 80) {
                    s += fmt::format("- HUMAN: that is {} ms later than on the steady click. The tapper is reacting to each beat after hearing it\n"
                                     "  instead of tapping ahead of it. Reaction lag is late, never early, and changes from song to song\n"
                                     "  and track to track, so a fixed offset only partly cancels it.\n",
                                     extra);
                } else if (extra <= -80) {
                    s += fmt::format("- Along to the song, taps land {} ms earlier than on the click: rushing the beat.\n", -extra);
                } else {
                    s += "- Tapping to the song matches the click test, so the lag is steady and the offset setting cancels it.\n";
                }
            } else if (_song.offsetMS >= 100) {
                s += "- Run the click and flash tests to see how much of this is the computer and how much is the tapping.\n";
            }
            if (_song.spreadMS >= 40) {
                s += fmt::format("- Taps scatter by about ±{} ms. Even with the offset, check tapped marks against the waveform, or tap at\n"
                                 "  50% playback speed (the offset adjusts for speed, and reaction lag counts for half as much).\n",
                                 _song.spreadMS);
            }
        }
    }
    if (!any) {
        s += "- Run a test first.\n";
    }

    s += "\nOFFSET\n";
    s += fmt::format("- Current timing tap offset: {} ms.\n", _frame->TimingTapOffsetMS());
    if (_clicks.done || (_song.done && !_song.ambiguous)) {
        s += fmt::format("- Measured offset: {} ms ({} test). With it set, marks tapped during playback move this much earlier.\n",
                         RecommendedOffset(), (_song.done && !_song.ambiguous) ? "song" : "click");
        s += "- Marks already tapped are not changed. Shift those tracks earlier, or check them against the waveform.\n";
    }

    auto lags = [&s](const char* name, const Result& r) {
        if (!r.done) {
            return;
        }
        s += fmt::format("{}:", name);
        for (int l : r.lags) {
            s += fmt::format(" {}", l);
        }
        s += "\n";
    };
    s += "\nPER-TAP LAG (ms)\n";
    lags("Clicks", _clicks);
    lags("Flashes", _flashes);
    lags("Song", _song);
    s += fmt::format("\n{}  xLights {}\n", wxGetOsDescription().ToStdString(), GetDisplayVersionString());
    return s;
}

void TimingTapTestDialog::UpdateResults() {
    _resultsText->SetValue(BuildReport());
    _resultsText->ShowPosition(0);
    _currentOffsetLabel->SetLabel(wxString::Format(_("Timing tap offset now: %d ms"), _frame->TimingTapOffsetMS()));
    UpdateButtons();
}
