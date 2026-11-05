/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include "PerfDiagnostics.h"

#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <log.h>
#include <spdlog/fmt/fmt.h>
#include <SpecialOptions.h>
#include "settings/XLightsConfigAdapter.h"

#ifdef __WXMSW__
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <io.h>
// Cloud-file placeholder flags (Windows 10 SDK); older headers lack them.
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif
#ifndef FILE_ATTRIBUTE_PINNED
#define FILE_ATTRIBUTE_PINNED 0x00080000
#endif
#ifndef FILE_ATTRIBUTE_UNPINNED
#define FILE_ATTRIBUTE_UNPINNED 0x00100000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#else
#include <unistd.h>
#endif

namespace PerfDiagnostics {

namespace {

std::atomic<bool> gEnabled{ false };
std::atomic<int64_t> gLastBeatMs{ 0 };
std::atomic<bool> gStop{ false };
std::mutex gStopMutex;
std::condition_variable gStopCv;
std::thread gWatchThread;
long gHangMs = 2000;
std::string gDiagLog;
std::string gDiagPrevLog;

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class BeatTimer : public wxTimer {
public:
    void Notify() override
    {
        gLastBeatMs.store(NowMs());
        // XL_PERF_SELFTEST_STALL_MS=n blocks the main thread once, n ms after
        // the event loop starts, to prove the watchdog end to end.
        static long selfTest = getenv("XL_PERF_SELFTEST_STALL_MS") ? std::strtol(getenv("XL_PERF_SELFTEST_STALL_MS"), nullptr, 10) : 0;
        if (selfTest > 0) {
            long ms = selfTest;
            selfTest = 0;
            spdlog::info("PERF: self-test, blocking the main thread for {} ms", ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }
};
BeatTimer* gBeat = nullptr;

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string CloudProvider(const std::string& path)
{
    const std::string p = Lower(path);
    if (p.find("dropbox") != std::string::npos) return "Dropbox";
    if (p.find("onedrive") != std::string::npos) return "OneDrive";
    if (p.find("google drive") != std::string::npos || p.find("googledrive") != std::string::npos || p.find("my drive") != std::string::npos) return "Google Drive";
    if (p.find("icloud") != std::string::npos || p.find("mobile documents") != std::string::npos) return "iCloud";
    if (p.find("cloudstorage") != std::string::npos) return "cloud storage";
    if (p.rfind("\\\\", 0) == 0 || p.rfind("//", 0) == 0) return "network share";
    return "";
}

void SetEnvIfUnset(const char* name, const char* value)
{
    if (getenv(name) != nullptr) {
        return;
    }
#ifdef __WXMSW__
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}

bool StderrIsTerminal()
{
#ifdef __WXMSW__
    int fd = _fileno(stderr);
    return fd >= 0 && _isatty(fd);
#else
    return isatty(fileno(stderr));
#endif
}

// ---------------------------------------------------------------------------
// Windows: main-thread stack sampling, CPU and I/O accounting
// ---------------------------------------------------------------------------
#if defined(__WXMSW__) && defined(_M_X64)
#define XL_PERF_STACKS 1

HANDLE gMainThread = nullptr;

constexpr int kMaxFrames = 64;
struct UnwindJob {
    CONTEXT ctx;
    DWORD64 pcs[kMaxFrames];
    int count;
};
UnwindJob gJob;
HANDLE gJobReady = nullptr;
HANDLE gJobDone = nullptr;
std::atomic<bool> gUnwinderBusy{ false };
std::thread gUnwinder;

// No C++ objects with destructors here: __try needs a frame without unwinding.
// The stack being read belongs to another thread, so a bad frame faults
// instead of returning garbage; the handler turns that into a short trace.
int UnwindFrom(CONTEXT* ctx, DWORD64* pcs, int maxFrames)
{
    int n = 0;
    __try {
        while (n < maxFrames && ctx->Rip != 0) {
            pcs[n++] = ctx->Rip;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx->Rip, &imageBase, nullptr);
            if (fn == nullptr) {
                ctx->Rip = *(DWORD64*)ctx->Rsp;
                ctx->Rsp += 8;
            } else {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx->Rip, fn, ctx, &handlerData, &establisher, nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// The unwind runs on its own thread so the watchdog can always resume the
// main thread after a bounded wait: RtlLookupFunctionEntry can take a loader
// lock the suspended main thread might be holding.
void UnwinderLoop()
{
    while (true) {
        WaitForSingleObject(gJobReady, INFINITE);
        if (gStop.load()) {
            return;
        }
        CONTEXT ctx = gJob.ctx;
        gJob.count = UnwindFrom(&ctx, gJob.pcs, kMaxFrames);
        gUnwinderBusy.store(false);
        SetEvent(gJobDone);
    }
}

struct MapSymbols {
    bool loaded = false;
    std::vector<std::pair<uint64_t, std::string>> syms;

    void Load()
    {
        loaded = true;
        wxFileName name(wxStandardPaths::Get().GetExecutablePath());
        name.SetExt("map");
        std::ifstream in(name.GetFullPath().ToStdString());
        if (!in.is_open()) {
            spdlog::info("PERF: no symbol map at {} - stacks will show offsets only", name.GetFullPath().ToStdString());
            return;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 18) {
                continue;
            }
            uint64_t rva = std::strtoull(line.substr(0, 16).c_str(), nullptr, 16);
            // "rva \t object \t symbol" - keep "symbol (object)"
            size_t t1 = line.find('\t');
            size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
            std::string obj = t1 == std::string::npos ? "" : line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
            std::string sym = t2 == std::string::npos ? obj : line.substr(t2 + 1);
            syms.emplace_back(rva, sym + " (" + obj + ")");
        }
        std::sort(syms.begin(), syms.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        spdlog::info("PERF: loaded {} symbols from {}", syms.size(), name.GetFullPath().ToStdString());
    }

    std::string Find(uint64_t rva) const
    {
        auto it = std::upper_bound(syms.begin(), syms.end(), rva, [](uint64_t v, const auto& e) { return v < e.first; });
        if (it == syms.begin()) {
            return "";
        }
        --it;
        return fmt::format("{}+0x{:x}", it->second, rva - it->first);
    }
};
MapSymbols gMap;

std::string Symbolize(DWORD64 pc)
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(uintptr_t)pc, &mod) || mod == nullptr) {
        return fmt::format("0x{:016x}", pc);
    }
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(mod, path, MAX_PATH);
    std::string base = path;
    size_t slash = base.find_last_of("\\/");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    uint64_t rva = (uint64_t)pc - (uint64_t)mod;
    std::string res = fmt::format("{}+0x{:x}", base, rva);
    if (mod == GetModuleHandleW(nullptr)) {
        if (!gMap.loaded) {
            gMap.Load();
        }
        std::string fn = gMap.Find(rva);
        if (!fn.empty()) {
            res += "  " + fn;
        }
    }
    return res;
}

// Suspends the main thread for as short a time as possible. Nothing between
// Suspend and Resume may allocate: the main thread could own the heap lock.
void SampleMainStack(int sampleNo, int64_t stalledMs)
{
    if (gMainThread == nullptr || gJobReady == nullptr) {
        return;
    }
    if (gUnwinderBusy.load()) {
        spdlog::warn("PERF:   stack sample {} skipped - previous walk still pending", sampleNo);
        return;
    }
    ResetEvent(gJobDone);
    if (SuspendThread(gMainThread) == (DWORD)-1) {
        spdlog::warn("PERF:   could not suspend main thread ({})", GetLastError());
        return;
    }
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    bool gotCtx = GetThreadContext(gMainThread, &ctx) != 0;
    DWORD wait = WAIT_FAILED;
    if (gotCtx) {
        gJob.ctx = ctx;
        gJob.count = 0;
        gUnwinderBusy.store(true);
        SetEvent(gJobReady);
        wait = WaitForSingleObject(gJobDone, 500);
    }
    ResumeThread(gMainThread);

    if (!gotCtx) {
        spdlog::warn("PERF:   could not read main thread context ({})", GetLastError());
        return;
    }
    if (wait != WAIT_OBJECT_0) {
        spdlog::warn("PERF:   stack sample {} timed out (main thread may be inside the loader)", sampleNo);
        return;
    }
    std::string trace = fmt::format("PERF:   main thread stack, sample {} at {} ms into stall:", sampleNo, stalledMs);
    for (int i = 0; i < gJob.count; ++i) {
        trace += fmt::format("\n    [{:02}] {}", i, Symbolize(gJob.pcs[i]));
    }
    spdlog::warn(trace);
}

uint64_t FileTimeMs(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000ULL;
}

uint64_t MainThreadCpuMs()
{
    FILETIME c, e, k, u;
    if (gMainThread == nullptr || !GetThreadTimes(gMainThread, &c, &e, &k, &u)) {
        return 0;
    }
    return FileTimeMs(k) + FileTimeMs(u);
}

struct IoSnap {
    IO_COUNTERS io{};
    uint64_t mainCpuMs = 0;
};

IoSnap TakeIoSnap()
{
    IoSnap s;
    GetProcessIoCounters(GetCurrentProcess(), &s.io);
    s.mainCpuMs = MainThreadCpuMs();
    return s;
}

std::string DescribeStall(const IoSnap& a, const IoSnap& b, int64_t wallMs)
{
    const double mb = 1024.0 * 1024.0;
    uint64_t cpu = b.mainCpuMs - a.mainCpuMs;
    return fmt::format("main thread CPU {} ms ({}% of the stall - {}), process read {:.1f} MB in {} ops, wrote {:.1f} MB in {} ops",
                       cpu, wallMs > 0 ? (int)(100 * cpu / wallMs) : 0,
                       wallMs > 0 && cpu * 2 > (uint64_t)wallMs ? "busy computing" : "mostly waiting (I/O, a lock, or another thread)",
                       (b.io.ReadTransferCount - a.io.ReadTransferCount) / mb, b.io.ReadOperationCount - a.io.ReadOperationCount,
                       (b.io.WriteTransferCount - a.io.WriteTransferCount) / mb, b.io.WriteOperationCount - a.io.WriteOperationCount);
}

void LogResources()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    FILETIME c, e, k, u;
    uint64_t cpu = 0;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        cpu = FileTimeMs(k) + FileTimeMs(u);
    }
    static uint64_t lastCpu = 0;
    static int64_t lastWall = 0;
    static DWORD lastFaults = 0;
    int64_t now = NowMs();
    double cores = (lastWall != 0 && now > lastWall) ? (double)(cpu - lastCpu) / (double)(now - lastWall) : 0.0;
    spdlog::info("PERF resources: working set {} MB, private {} MB, peak WS {} MB, page faults +{}, handles {}, GDI {}, USER {}, "
                 "CPU {:.1f} cores avg, system RAM {}% used ({} MB free of {} MB)",
                 pmc.WorkingSetSize >> 20, pmc.PrivateUsage >> 20, pmc.PeakWorkingSetSize >> 20,
                 pmc.PageFaultCount - lastFaults, handles,
                 GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
                 cores, ms.dwMemoryLoad, ms.ullAvailPhys >> 20, ms.ullTotalPhys >> 20);
    lastCpu = cpu;
    lastWall = now;
    lastFaults = pmc.PageFaultCount;
}

#else

struct IoSnap {};
IoSnap TakeIoSnap() { return {}; }
std::string DescribeStall(const IoSnap&, const IoSnap&, int64_t) { return "no CPU/I-O detail on this platform"; }
void SampleMainStack(int, int64_t) {}
void LogResources()
{
    wxMemorySize free = wxGetFreeMemory();
    if (free != -1) {
        spdlog::info("PERF resources: system free memory {} MB", (long long)(free.GetValue() >> 20));
    }
}

#endif

void WatchLoop()
{
    bool stalled = false;
    int64_t stallStart = 0;
    int64_t nextSample = 0;
    int samples = 0;
    IoSnap stallSnap;
    int64_t lastResources = NowMs();
    LogResources();

    while (true) {
        {
            std::unique_lock<std::mutex> lock(gStopMutex);
            if (gStopCv.wait_for(lock, std::chrono::milliseconds(250), [] { return gStop.load(); })) {
                return;
            }
        }
        int64_t now = NowMs();
        int64_t beat = gLastBeatMs.load();
        if (beat == 0) {
            continue;
        }
        int64_t gap = now - beat;
        if (!stalled && gap >= gHangMs) {
            stalled = true;
            stallStart = beat;
            samples = 0;
            nextSample = now;
            stallSnap = TakeIoSnap();
            spdlog::warn("PERF: UI not responding - main thread has not processed events for {} ms", gap);
        }
        if (stalled) {
            if (gap < gHangMs) {
                int64_t dur = beat - stallStart;
                spdlog::warn("PERF: UI responsive again after a {:.1f} s stall; {}", dur / 1000.0,
                             DescribeStall(stallSnap, TakeIoSnap(), now - stallStart));
                stalled = false;
            } else if (now >= nextSample && samples < 20) {
                ++samples;
                SampleMainStack(samples, now - stallStart);
                // Dense at first, then sparse: a long stall needs a few samples
                // to tell a loop from a single slow call, not hundreds.
                nextSample = now + (samples < 3 ? 1000 : 5000);
            }
        }
        if (now - lastResources >= 60000) {
            LogResources();
            lastResources = now;
        }
    }
}

void RedirectStderr()
{
    if (StderrIsTerminal()) {
        spdlog::info("PERF: stderr is a terminal, render reports stay there");
        return;
    }
    std::filesystem::path folder = GetLogFileFolder();
    std::filesystem::path log = folder / "xLights_perfdiag.log";
    std::filesystem::path prev = folder / "xLights_perfdiag.prev.log";
    std::error_code ec;
    // Keep the previous session: after a freeze the user relaunches before
    // packaging the logs, and that launch must not wipe the evidence.
    if (std::filesystem::exists(log, ec)) {
        std::filesystem::remove(prev, ec);
        std::filesystem::rename(log, prev, ec);
    }
#ifdef __WXMSW__
    FILE* f = _wfreopen(log.wstring().c_str(), L"w", stderr);
#else
    FILE* f = freopen(log.string().c_str(), "w", stderr);
#endif
    if (f == nullptr) {
        spdlog::warn("PERF: could not redirect stderr to {}", log.string());
        return;
    }
    setvbuf(stderr, nullptr, _IONBF, 0);
    gDiagLog = log.string();
    gDiagPrevLog = prev.string();
    spdlog::info("PERF: render reports (XL_RENDER_*) go to {}", gDiagLog);
}

} // namespace

bool Enabled()
{
    return gEnabled.load();
}

void Start()
{
    if (gEnabled.load()) {
        return;
    }
    if (SpecialOptions::GetOption("perf_diagnostics", "false") != "true") {
        return;
    }
    gEnabled.store(true);
    gHangMs = std::max(250L, std::strtol(SpecialOptions::GetOption("perf_hang_ms", "2000").c_str(), nullptr, 10));
    spdlog::info("PERF: performance diagnostics ON (UI stall threshold {} ms)", gHangMs);

    if (SpecialOptions::GetOption("perf_render_profile", "true") == "true") {
        SetEnvIfUnset("XL_RENDER_PROFILE", "1");
        SetEnvIfUnset("XL_RENDER_SETUP", "1");
        SetEnvIfUnset("XL_RENDER_MEM", "1");
        RedirectStderr();
    }

#ifdef XL_PERF_STACKS
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &gMainThread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
        gMainThread = nullptr;
        spdlog::warn("PERF: no main thread handle ({}) - stalls will be timed but not sampled", GetLastError());
    }
    gJobReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gJobDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gUnwinder = std::thread(UnwinderLoop);
#endif

    // Count from now, not from the first timer tick: the frame constructor
    // loads the show folder and often a sequence before the event loop runs,
    // and a stall there is exactly what a slow startup report needs.
    gLastBeatMs.store(NowMs());
    gBeat = new BeatTimer();
    gBeat->Start(200);
    gWatchThread = std::thread(WatchLoop);
}

void Stop()
{
    if (!gEnabled.load()) {
        return;
    }
    if (gBeat != nullptr) {
        gBeat->Stop();
        delete gBeat;
        gBeat = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(gStopMutex);
        gStop.store(true);
    }
    gStopCv.notify_all();
    if (gWatchThread.joinable()) {
        gWatchThread.join();
    }
#ifdef XL_PERF_STACKS
    if (gUnwinder.joinable()) {
        SetEvent(gJobReady);
        // A walk abandoned on timeout may still be blocked on a loader lock;
        // never hang shutdown on it.
        gUnwinder.detach();
    }
    if (gMainThread != nullptr) {
        CloseHandle(gMainThread);
        gMainThread = nullptr;
    }
#endif
    gEnabled.store(false);
}

std::vector<std::string> ReportFiles()
{
    std::vector<std::string> res;
    std::error_code ec;
    std::filesystem::path folder = GetLogFileFolder();
    for (const char* n : { "xLights_perfdiag.log", "xLights_perfdiag.prev.log" }) {
        std::filesystem::path p = folder / n;
        if (std::filesystem::exists(p, ec)) {
            res.push_back(p.string());
        }
    }
    return res;
}

void LogFileStorage(const std::string& what, const std::string& path)
{
    if (!Enabled() || path.empty()) {
        return;
    }
    std::string provider = CloudProvider(path);
    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);
    std::string detail = exists ? "" : " (not found)";
#ifdef __WXMSW__
    if (exists) {
        DWORD a = GetFileAttributesW(std::filesystem::path(path).wstring().c_str());
        if (a != INVALID_FILE_ATTRIBUTES) {
            if (a & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE)) {
                detail += " ONLINE-ONLY placeholder - first read downloads it";
            }
            if (a & FILE_ATTRIBUTE_PINNED) {
                detail += " pinned (always keep on device)";
            }
        }
    }
#endif
    spdlog::info("PERF storage: {} {} is on {}{}", what, path, provider.empty() ? "a local/unknown drive" : provider, detail);
}

void LogShowFolderStorage(const std::string& showDir)
{
    if (!Enabled() || showDir.empty()) {
        return;
    }
    std::string provider = CloudProvider(showDir);
    spdlog::info("PERF storage: show folder {} is on {}", showDir, provider.empty() ? "a local/unknown drive" : provider);
#ifdef __WXMSW__
    // Off the main thread: a big show folder takes a while to walk and the
    // scan must not cause the very stall it is looking for.
    std::thread([showDir]() {
        auto start = std::chrono::steady_clock::now();
        size_t files = 0, placeholders = 0, unpinned = 0;
        uint64_t bytes = 0, placeholderBytes = 0;
        std::vector<std::string> examples;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(showDir, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 3) {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            if (++files > 50000) {
                break;
            }
            uint64_t sz = it->file_size(ec);
            bytes += sz;
            DWORD a = GetFileAttributesW(it->path().wstring().c_str());
            if (a == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            if (a & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE)) {
                ++placeholders;
                placeholderBytes += sz;
                if (examples.size() < 20) {
                    examples.push_back(it->path().string());
                }
            }
            if (a & FILE_ATTRIBUTE_UNPINNED) {
                ++unpinned;
            }
        }
        long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::string list;
        for (const auto& e : examples) {
            list += "\n    " + e;
        }
        spdlog::info("PERF storage: scanned {} files ({:.1f} MB) in {} ms; {} are online-only placeholders ({:.1f} MB), {} marked free-up-space{}",
                     files, bytes / (1024.0 * 1024.0), ms, placeholders, placeholderBytes / (1024.0 * 1024.0), unpinned, list);
    }).detach();
#endif
}

ScopedTimer::ScopedTimer(std::string name, long minMs) :
    _name(std::move(name)), _minMs(minMs), _start(std::chrono::steady_clock::now())
{
    if (Enabled()) {
        spdlog::info("PERF {}: start", _name);
    }
}

ScopedTimer::~ScopedTimer()
{
    if (!Enabled()) {
        return;
    }
    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _start).count();
    if (ms >= _minMs) {
        spdlog::info("PERF {}: {} ms", _name, ms);
    }
}

} // namespace PerfDiagnostics
