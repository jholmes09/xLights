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

#include <wx/dialog.h>
#include <wx/timer.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxSpinCtrl;
class wxTextCtrl;
class wxStaticText;
class AudioManager;
class xLightsFrame;
class TapTestPanel;

// Measures where timing marks tapped during playback land, and splits the lag into
// the computer's sound delay and the tapper's own habit:
//  - Steady clicks: taps against clicks played through xLights' own audio output,
//    timed with the same audio clock that tapped timing marks use.
//  - Steady flashes: the same beat shown on screen with no sound.
//  - Song: taps along to the open sequence's audio, scored against a timing track
//    the user trusts.
class TimingTapTestDialog : public wxDialog {
public:
    TimingTapTestDialog(wxWindow* parent, xLightsFrame* frame);
    ~TimingTapTestDialog() override;

    struct Result {
        bool done = false;
        int taps = 0;     // taps that matched a beat
        int offsetMS = 0; // median lag, positive = late
        int spreadMS = 0; // median absolute deviation around the offset
        bool ambiguous = false;
        int altOffsetMS = 0;
        std::vector<int> lags; // per matched tap, tap minus beat
    };

private:
    enum class Test { None, Clicks, Flashes, Song };

    void StartTest(Test t);
    void FinishTest(bool cancelled);
    void OnTimer(wxTimerEvent& event);
    void OnCharHook(wxKeyEvent& event);
    void RegisterTap();
    void UpdateResults();
    void UpdateButtons();
    std::string BuildReport() const;
    int RecommendedOffset() const;
    long NowMS() const;

    xLightsFrame* _frame = nullptr;
    TapTestPanel* _panel = nullptr;
    wxButton* _clicksButton = nullptr;
    wxButton* _flashesButton = nullptr;
    wxButton* _songButton = nullptr;
    wxButton* _applyButton = nullptr;
    wxButton* _copyButton = nullptr;
    wxChoice* _trackChoice = nullptr;
    wxSpinCtrl* _startSpin = nullptr;
    wxSpinCtrl* _lengthSpin = nullptr;
    wxTextCtrl* _resultsText = nullptr;
    wxStaticText* _currentOffsetLabel = nullptr;

    wxTimer _timer;
    Test _running = Test::None;
    std::chrono::steady_clock::time_point _t0;
    long _endMS = 0;        // wall time (since _t0) at which the test ends
    long _lastTapWall = -1000;

    std::unique_ptr<AudioManager> _clickAudio;
    std::string _clickFile;
    AudioManager* _songAudio = nullptr;

    std::vector<long> _beats;     // stimulus times: audio-clock ms (clicks, song) or wall ms (flashes)
    std::vector<long> _taps;      // tap times on the same clock as _beats
    std::vector<long> _scoredBeats; // beats that count (count-in excluded)
    size_t _nextFlash = 0;
    long _flashOffAt = -1;
    std::string _songTrack;
    int _songStartMS = 0;

    Result _clicks;
    Result _flashes;
    Result _song;
};
