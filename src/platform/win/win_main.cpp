// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll, Windows main: wires the platform-independent daemon core (daemon_core.hpp) to the
// Windows platform layer (src/platform/win/): raw-HID input, SendInput output, foreground-window
// provider, named-event reload, %APPDATA%/%LOCALAPPDATA% paths, status and log files. GUI
// subsystem (no console window); a hidden message-only window receives the session-end messages.
// Mirrors src/dofscroll.cpp (the Linux main).
#ifndef _WIN32
#error "win_main.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include "../../common.hpp"
#include "../../config.hpp"
#include "../../daemon_core.hpp"
#include "../../modes.hpp"
#include "../../output.hpp"
#include "../fs.hpp"
#include "../platform.hpp"
#include "win_output.hpp"
#include "win_paths.hpp"

using namespace spn;

namespace {

const char *const VERSION = "dofscroll 2.0 (Windows)";
const char *const LOG = "[dofscroll] ";

DaemonCore *g_core = nullptr;
HWND g_hwnd = nullptr;

std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// ---- output for --help / --version in a GUI-subsystem program ---------------------------------------
// If stdout is redirected (pipe/file) the standard handle is used; otherwise the parent's console
// (cmd.exe, PowerShell) is attached; with neither, a message box shows the text.
void printText(const std::string &text) {
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    bool console = false;
    if (!h || h == INVALID_HANDLE_VALUE) {
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            h = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            console = true;
        }
    }
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        if (console) ::CloseHandle(h);
        return;
    }
    ::MessageBoxW(nullptr, widen(text).c_str(), L"dofscroll", MB_OK | MB_ICONINFORMATION);
}

void printHelp() {
    printText(
        "Usage: dofscroll [--debug] [--release-keys] [--help] [--version]\n"
        "\n"
        "Translates SpaceMouse motion (raw HID) into scroll, zoom, arrow-key and mouse-button\n"
        "events (SendInput). Config: %APPDATA%\\dofscroll\\dofscroll.conf, modes in\n"
        "%APPDATA%\\dofscroll\\modes. Status and log: %LOCALAPPDATA%\\dofscroll.\n"
        "\n"
        "  --debug         logs the evaluated channels about 10 times per second\n"
        "  --release-keys  releases Ctrl, arrow keys and mouse buttons and exits\n"
        "  --help          this help\n"
        "  --version       print the version\n"
        "\n"
        "The daemon has no window. Reload the configuration: the settings program signals the\n"
        "named event Local\\dofscroll-reload (also automatic when the files change). Quit: end the\n"
        "process in Task Manager or log off (held keys are released).\n");
}

// ---- log file mirror -----------------------------------------------------------------------------

// Mirrors everything written to std::cerr into %LOCALAPPDATA%\dofscroll\dofscroll.log (rolling,
// timestamped per line, like the Linux log). Written on flush; the reader thread logs too, hence
// the mutex. Any error only disables the mirror. Deliberately never destroyed (a detached
// reader thread may still log during exit).
class TeeLogBuf : public std::streambuf {
public:
    TeeLogBuf(std::streambuf *orig, const std::string &dir, size_t maxLines, size_t keepLines)
        : orig_(orig), max_lines_(maxLines), keep_lines_(keepLines) {
        if (dir.empty()) return;
        std::string err;
        if (!fs::mkdirsPrivate(dir, &err)) return;
        path_ = dir + "/dofscroll.log";
        std::string existing;
        const fs::ReadResult rr = fs::readFileBounded(path_, kLogMaxBytes * 2, fs::ReadOptions(), &existing);
        if (rr.ok()) {
            lines_ = splitLines(existing);
            size_ = existing.size();
            if (trimLogLines(&lines_, max_lines_, keep_lines_, size_)) rewrite();
        } else if (rr.status != fs::ReadStatus::NotFound) {
            lines_.clear();
            rewrite();  // unreadable/too large: start over
        }
        openAppend();
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        const std::streamsize r = orig_->sputn(s, n);
        std::lock_guard<std::mutex> lk(m_);
        if (file_ != INVALID_HANDLE_VALUE && pending_.size() < 64 * 1024)
            stampLines(std::string(s, static_cast<size_t>(n)), &at_line_start_, timestamp(), &pending_);
        return r;
    }
    int sync() override {
        const int r = orig_->pubsync();
        std::lock_guard<std::mutex> lk(m_);
        flushPending();
        return r;
    }

private:
    std::streambuf *orig_;
    size_t max_lines_, keep_lines_;
    std::mutex m_;
    std::string path_, pending_, partial_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    size_t size_ = 0;
    std::deque<std::string> lines_;
    bool at_line_start_ = true;

    static std::string timestamp() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
        localtime_s(&tmv, &t);
        char ts[32];
        return std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S ", &tmv) > 0 ? std::string(ts) : std::string();
    }
    void openAppend() {
        file_ = ::CreateFileW(widen(path_).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    }
    void closeFile() {
        if (file_ != INVALID_HANDLE_VALUE) ::CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    void rewrite() {
        closeFile();
        const std::string text = joinLines(lines_);
        std::string err;
        if (!fs::writeFileReplace(path_, text, &err)) {
            pending_.clear();
            return;  // mirror stays off
        }
        size_ = text.size();
        openAppend();
    }
    void flushPending() {
        if (file_ == INVALID_HANDLE_VALUE || pending_.empty()) {
            pending_.clear();
            return;
        }
        DWORD w = 0;
        if (!::WriteFile(file_, pending_.data(), static_cast<DWORD>(pending_.size()), &w, nullptr) || w != pending_.size()) {
            closeFile();
            pending_.clear();
            return;
        }
        size_ += pending_.size();
        partial_ += pending_;
        pending_.clear();
        size_t pos = 0, nl;
        while ((nl = partial_.find('\n', pos)) != std::string::npos) {
            lines_.push_back(partial_.substr(pos, nl - pos));
            pos = nl + 1;
        }
        partial_.erase(0, pos);
        if (partial_.size() > 8192) partial_.clear();
        if (trimLogLines(&lines_, max_lines_, keep_lines_, size_)) rewrite();
    }
};

// ---- quit handling ----------------------------------------------------------------------------------

BOOL WINAPI onConsoleCtrl(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_core) g_core->requestQuit();
        return TRUE;
    default:
        return FALSE;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp && g_core) g_core->requestQuit();
        return 0;
    case WM_CLOSE:
    case WM_DESTROY:
        if (g_core) g_core->requestQuit();
        if (msg == WM_CLOSE) ::DestroyWindow(hwnd);
        return 0;
    default:
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

HWND createMessageWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"dofscroll_hidden_window";
    if (!::RegisterClassW(&wc)) return nullptr;
    return ::CreateWindowExW(0, wc.lpszClassName, L"dofscroll", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance,
                             nullptr);
}

// --release-keys: open an own output, release everything, exit.
int releaseKeysAndExit() {
    setWinBlanketRelease(true);
    std::string log;
    std::unique_ptr<Output> out = makeOutput("auto", &log);
    if (!out) {
        printText(std::string(LOG) + "--release-keys: no output possible" + (log.empty() ? "" : " (" + log + ")") + "\n");
        return 1;
    }
    out->forceReleaseAll();
    out->flush();
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool debug = false;
    {
        int argc = 0;
        wchar_t **argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            const std::string a = narrow(argv[i]);
            if (a == "--debug") {
                debug = true;
            } else if (a == "--help" || a == "-h") {
                printHelp();
                ::LocalFree(argv);
                return 0;
            } else if (a == "--version") {
                printText(std::string(VERSION) + "\n");
                ::LocalFree(argv);
                return 0;
            } else if (a == "--release-keys") {
                ::LocalFree(argv);
                return releaseKeysAndExit();
            } else {
                printText("dofscroll: unknown option '" + a + "' (see --help)\n");
                ::LocalFree(argv);
                return 2;
            }
        }
        if (argv) ::LocalFree(argv);
    }

    const PlatformPaths paths = platformPaths();

    // Also mirror log lines into the log file (deliberately never destroyed).
    std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), paths.runtimeDir, debug ? kLogMaxLinesDebug : kLogMaxLines,
                                  debug ? kLogKeepLinesDebug : kLogKeepLines));

    // One daemon per logon session (two would send every event twice).
    HANDLE single = ::CreateMutexW(nullptr, FALSE, widen(singleInstanceMutexName()).c_str());
    if (single && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << LOG << "another dofscroll instance is already running - exiting" << std::endl;
        return 1;
    }

    // Deliberately never destroyed: the detached reader thread accesses these until exit.
    std::string ilog;
    std::unique_ptr<InputSource> input = makeInputSource("auto", &ilog);
    if (!input) {
        std::cerr << LOG << "ERROR: no input source (" << ilog << ")" << std::endl;
        return 1;
    }
    if (!ilog.empty()) std::cerr << LOG << ilog << std::endl;
    InputSource *input_p = input.release();
    ActiveWindowProvider *win_p = makeActiveWindowProvider().release();
    ReloadListener *reload_p = makeReloadListener().release();
    StatusThrottle *throttle = new StatusThrottle();
    const std::string statusPath = paths.runtimeDir.empty() ? std::string() : paths.runtimeDir + "/dofscroll-status";

    DaemonDeps deps;
    deps.input = input_p;
    deps.activeWindow = win_p;
    deps.reload = reload_p;
    deps.paths = paths;  // config: <configDir>/dofscroll.conf, modes: <configDir>/modes (core defaults)
    deps.makeOutput = [](const std::string &b, std::string *l) { return makeOutput(b, l); };
    if (!statusPath.empty()) {
        deps.writeStatus = [throttle, statusPath](const std::string &body, int64_t nowNs) {
            if (!throttle->due(body, nowNs)) return;
            const std::string text = statusFileText(body, static_cast<long long>(std::time(nullptr)));
            std::string err;
            if (!text.empty() && !fs::writeFileReplace(statusPath, text, &err)) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    std::cerr << LOG << "WARNING: status file not writable (" << err << ")" << std::endl;
                }
            }
        };
        deps.removeStatus = [statusPath] { fs::removeFile(statusPath); };
    }
    deps.debug = debug;

    DaemonCore *core = new DaemonCore(std::move(deps));
    g_core = core;
    if (!core->initialize()) return 1;

    ::SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    g_hwnd = createMessageWindow();

    // The core ticks on its own thread; this thread pumps messages (session end, close).
    std::atomic<bool> finished{false};
    const DWORD mainThread = ::GetCurrentThreadId();
    std::thread runner([core, &finished, mainThread] {
        core->run();
        finished = true;
        ::PostThreadMessageW(mainThread, WM_QUIT, 0, 0);  // wake the pump
    });
    if (g_hwnd) {
        MSG msg;
        while (!finished && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    if (g_core) g_core->requestQuit();
    runner.join();
    return 0;
}
