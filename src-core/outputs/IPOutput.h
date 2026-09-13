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

#include <map>
#include <string>

#include "Output.h"

class IPOutput : public Output
{
protected:

    #pragma region Private Functions
    virtual void SaveAttr(pugi::xml_node node) override;
    #pragma endregion

public:

    #pragma region Constructors and Destructors
    IPOutput(pugi::xml_node node, bool isActive);
    IPOutput();
    IPOutput(const IPOutput& from);
    virtual ~IPOutput();
    virtual pugi::xml_node Save(pugi::xml_node parent) override;
    #pragma endregion 

    #pragma region Static Functions
    static Output::PINGSTATE Ping(const std::string& ip, const std::string& proxy);
    #pragma endregion 

    #pragma region Getters and Setters
    virtual void SetIP(const std::string& ip, bool isActive, bool resolve = true) override;

    virtual bool IsIpOutput() const override { return true; }
    virtual bool IsSerialOutput() const override { return false; }

    virtual std::string GetSortName() const override { return GetIP(); }
    #pragma endregion 

    #pragma region Operators
    bool operator==(const IPOutput& output) const;
    #pragma endregion 
    
    #pragma region Start and Stop
    virtual bool Open() override { return Output::Open(); }
    #pragma endregion 

protected:
    #pragma region Socket recovery
    // A UDP socket bound to a local address that later goes away - a DHCP
    // renewal, a sleep/wake, an interface change - fails every send from then
    // on.  Nothing used to notice: the result of SendTo was discarded, and the
    // reopen guard OutputManager::IsRetryOpen() has no caller anywhere in the
    // tree, so _isRetryOpen is false for the life of the process and the retry
    // in each StartFrame is unreachable.  Output stayed dead until the user
    // toggled it off and on.  These three helpers give the IP outputs a way to
    // notice and recover on their own.

    // A single failure is usually transient (ENOBUFS on a heavy frame), so only
    // a run of them is taken to mean the socket is gone.
    static constexpr int SEND_FAILURES_BEFORE_CLOSE = 3;
    // Reopening costs a socket() plus a bind() per universe.  On a large show
    // that is thousands of syscalls a second while the network is down, so an
    // attempt is allowed only once a second.
    static constexpr long REOPEN_BACKOFF_MS = 1000;

    int _consecutiveSendFailures = 0;
    long _lastReopenAttemptMs = -1;

    // Returns true when the caller should Close() its socket.
    bool NoteSendResult(bool ok) {
        if (ok) {
            _consecutiveSendFailures = 0;
            return false;
        }
        ++_consecutiveSendFailures;
        return _consecutiveSendFailures >= SEND_FAILURES_BEFORE_CLOSE;
    }

    bool ShouldAttemptReopen(long msec) {
        // The frame clock restarts at zero every time output is enabled, so a
        // timestamp left over from a previous run must not hold the retry off.
        if (_lastReopenAttemptMs >= 0 && msec < _lastReopenAttemptMs) {
            _lastReopenAttemptMs = -1;
        }
        if (_lastReopenAttemptMs >= 0 && msec - _lastReopenAttemptMs < REOPEN_BACKOFF_MS) {
            return false;
        }
        _lastReopenAttemptMs = msec;
        return true;
    }

    // A reopened socket starts with a full allowance again.  Without this the
    // old run of failures carries over and the first failed send after a
    // recovery closes it immediately, which thrashes.
    void ResetSendRecovery() {
        _consecutiveSendFailures = 0;
        _lastReopenAttemptMs = -1;
        _suppressedOpenFailures = 0;
        _lastOpenFailureLogMs = -1;
    }

    // A failed reopen is attempted once a second per universe. On a large show
    // that is dozens of identical error lines every second for as long as the
    // network is down, which buries everything else in the log. Log the first
    // one immediately, then at most one every OPEN_FAILURE_LOG_INTERVAL_MS,
    // carrying a count of what went unlogged in between.
    static constexpr long OPEN_FAILURE_LOG_INTERVAL_MS = 30000;

    int _suppressedOpenFailures = 0;
    long _lastOpenFailureLogMs = -1;

    // True when this open failure should be logged. `suppressed` receives how
    // many were skipped since the last one that was.
    bool ShouldLogOpenFailure(int& suppressed) {
        suppressed = 0;
        // Opens outside the retry path (startup, an explicit enable) always
        // log: there is no storm to damp and the user needs to see them.
        const long msec = _lastReopenAttemptMs;
        if (msec < 0) {
            _suppressedOpenFailures = 0;
            _lastOpenFailureLogMs = -1;
            return true;
        }
        if (_lastOpenFailureLogMs >= 0 && msec < _lastOpenFailureLogMs) {
            _lastOpenFailureLogMs = -1;   // frame clock restarted
        }
        if (_lastOpenFailureLogMs >= 0 &&
            msec - _lastOpenFailureLogMs < OPEN_FAILURE_LOG_INTERVAL_MS) {
            ++_suppressedOpenFailures;
            return false;
        }
        suppressed = _suppressedOpenFailures;
        _suppressedOpenFailures = 0;
        _lastOpenFailureLogMs = msec;
        return true;
    }

    std::string OpenFailureSuffix(int suppressed) const {
        return suppressed > 0
            ? " (" + std::to_string(suppressed) + " further attempts not logged)"
            : std::string();
    }
    #pragma endregion
};
