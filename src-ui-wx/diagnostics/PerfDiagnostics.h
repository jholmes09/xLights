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

#include <chrono>
#include <string>
#include <vector>

// Opt-in performance diagnostics for slow-render / UI-freeze reports.
//
// Turned on by `perf_diagnostics=true` in special.options next to the
// executable. When on:
//   - a watchdog logs every main-thread stall over `perf_hang_ms` (default
//     2000) and, on Windows, samples the main thread's call stack while it is
//     stuck, with main-thread CPU and process disk I/O over the stall;
//   - the render engine's XL_RENDER_PROFILE / XL_RENDER_SETUP / XL_RENDER_MEM
//     reports are enabled and stderr goes to xLights_perfdiag.log beside the
//     spdlog file (a Windows GUI process has no stderr of its own);
//   - process memory is logged once a minute;
//   - opening a show folder or media file logs where it lives (Dropbox /
//     OneDrive / ...) and, on Windows, which files are online-only
//     placeholders that stall on first read.
// Everything lands in files Help > Package Log Files already collects.
namespace PerfDiagnostics {

// Call on the main thread once logging is up and before the render engine is
// created. Reads the special options and starts the watchdog if enabled.
void Start();
void Stop();
[[nodiscard]] bool Enabled();

// Extra files (full paths) for the debug-report zip.
[[nodiscard]] std::vector<std::string> ReportFiles();

void LogShowFolderStorage(const std::string& showDir);
void LogFileStorage(const std::string& what, const std::string& path);

// Logs "PERF <name>: N ms" on destruction when diagnostics are on and the
// scope took at least minMs.
class ScopedTimer {
public:
    explicit ScopedTimer(std::string name, long minMs = 100);
    ~ScopedTimer();
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string _name;
    long _minMs;
    std::chrono::steady_clock::time_point _start;
};

} // namespace PerfDiagnostics
