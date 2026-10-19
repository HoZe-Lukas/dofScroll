// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed transport for Windows: outbound-only named pipe \\.\pipe\dofscroll-live-<user sid>.
//  - DACL: full access for the current user only (no SYSTEM, no Administrators entry, no network).
//  - PIPE_REJECT_REMOTE_CLIENTS; the first instance is created with FILE_FLAG_FIRST_PIPE_INSTANCE
//    so a process that squats the name before the daemon makes the daemon fail instead of serving.
//  - PIPE_ACCESS_OUTBOUND: the server cannot read, so no client can ever send it anything.
//  - Non-blocking (PIPE_NOWAIT) so the tick thread never stalls; the client uses PeekNamedPipe so
//    its reads never block either, and verifies that the pipe server runs as the same user.
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <vector>

#include "live_feed.hpp"

namespace spn {

namespace {

constexpr DWORD kPipeBuffer = 4096;

std::vector<unsigned char> tokenUser(HANDLE tok) {
    DWORD len = 0;
    ::GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::vector<unsigned char> buf(len);
    if (!len || !::GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) buf.clear();
    return buf;
}

std::vector<unsigned char> currentUser() {
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &tok)) return {};
    std::vector<unsigned char> u = tokenUser(tok);
    ::CloseHandle(tok);
    return u;
}

std::wstring sidString(const std::vector<unsigned char> &user) {
    if (user.empty()) return L"";
    LPWSTR s = nullptr;
    if (!::ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid, &s)) return L"";
    std::wstring r = s;
    ::LocalFree(s);
    return r;
}

std::wstring pipeName(const std::vector<unsigned char> &user) {
    const std::wstring sid = sidString(user);
    return sid.empty() ? std::wstring() : L"\\\\.\\pipe\\dofscroll-live-" + sid;
}

class PipeConn : public FeedConn {
public:
    explicit PipeConn(HANDLE h, bool server) : h_(h), server_(server) {}
    ~PipeConn() override {
        if (server_) ::DisconnectNamedPipe(h_);
        ::CloseHandle(h_);
    }
    long read(uint8_t *buf, size_t n) override {
        DWORD avail = 0;
        if (!::PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) return -1;
        if (avail == 0) return 0;
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(n, avail));
        if (!::ReadFile(h_, buf, want, &got, nullptr)) return -1;
        return static_cast<long>(got);
    }
    long write(const uint8_t *buf, size_t n) override {
        DWORD w = 0;
        if (!::WriteFile(h_, buf, static_cast<DWORD>(n), &w, nullptr)) return -1;  // ERROR_NO_DATA: client gone
        return static_cast<long>(w);
    }

private:
    HANDLE h_;
    bool server_;
};

class PipeListener : public FeedListener {
public:
    ~PipeListener() override {
        if (pending_ != INVALID_HANDLE_VALUE) ::CloseHandle(pending_);
    }
    bool open(std::string *err) override {
        std::string dummy;
        if (!err) err = &dummy;
        const std::vector<unsigned char> user = currentUser();
        name_ = pipeName(user);
        sddl_ = L"D:P(A;;GA;;;" + sidString(user) + L")";
        if (name_.empty()) {
            *err = "cannot determine the user SID for the live feed pipe";
            return false;
        }
        pending_ = newInstance(true, err);
        return pending_ != INVALID_HANDLE_VALUE;
    }
    std::unique_ptr<FeedConn> accept() override {
        if (pending_ == INVALID_HANDLE_VALUE) {
            std::string e;
            pending_ = newInstance(false, &e);
            if (pending_ == INVALID_HANDLE_VALUE) return nullptr;
        }
        const BOOL ok = ::ConnectNamedPipe(pending_, nullptr);
        const DWORD e = ok ? ERROR_PIPE_CONNECTED : ::GetLastError();
        if (e == ERROR_PIPE_CONNECTED) {
            HANDLE h = pending_;
            pending_ = INVALID_HANDLE_VALUE;
            return std::make_unique<PipeConn>(h, true);
        }
        if (e == ERROR_NO_DATA) ::DisconnectNamedPipe(pending_);  // a client came and went: listen again
        return nullptr;                                            // ERROR_PIPE_LISTENING: nobody yet
    }

private:
    std::wstring name_, sddl_;
    HANDLE pending_ = INVALID_HANDLE_VALUE;

    HANDLE newInstance(bool first, std::string *err) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof sa;
        sa.bInheritHandle = FALSE;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl_.c_str(), SDDL_REVISION_1,
                                                                    &sa.lpSecurityDescriptor, nullptr)) {
            *err = "live feed pipe: cannot build the security descriptor";
            return INVALID_HANDLE_VALUE;
        }
        const DWORD openMode = PIPE_ACCESS_OUTBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
        const DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS;
        HANDLE h = ::CreateNamedPipeW(name_.c_str(), openMode, pipeMode, PIPE_UNLIMITED_INSTANCES, kPipeBuffer, 0, 0,
                                      &sa);
        const DWORD le = ::GetLastError();
        ::LocalFree(sa.lpSecurityDescriptor);
        if (h == INVALID_HANDLE_VALUE) *err = "live feed pipe: CreateNamedPipe failed (error " + std::to_string(le) + ")";
        return h;
    }
};

// True if the process behind `h` runs as the same user as this process.
bool serverIsSameUser(HANDLE pipe, const std::vector<unsigned char> &me) {
    ULONG pid = 0;
    if (!::GetNamedPipeServerProcessId(pipe, &pid) || pid == 0 || me.empty()) return false;
    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    HANDLE tok = nullptr;
    bool same = false;
    if (::OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
        const std::vector<unsigned char> other = tokenUser(tok);
        same = !other.empty() && ::EqualSid(reinterpret_cast<const TOKEN_USER *>(other.data())->User.Sid,
                                            reinterpret_cast<const TOKEN_USER *>(me.data())->User.Sid);
        ::CloseHandle(tok);
    }
    ::CloseHandle(proc);
    return same;
}

}  // namespace

std::unique_ptr<FeedListener> makeFeedListener(const std::string &) { return std::make_unique<PipeListener>(); }

std::unique_ptr<FeedConn> connectFeed(const std::string &, std::string *err) {
    std::string dummy;
    if (!err) err = &dummy;
    const std::vector<unsigned char> me = currentUser();
    const std::wstring name = pipeName(me);
    if (name.empty()) {
        *err = "cannot determine the user SID for the live feed pipe";
        return nullptr;
    }
    // SQOS_PRESENT | IDENTIFICATION: the server may learn who we are but cannot act as us.
    HANDLE h = ::CreateFileW(name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                             SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        *err = "live feed pipe: not available (daemon not running?)";
        return nullptr;
    }
    if (!serverIsSameUser(h, me)) {
        ::CloseHandle(h);
        *err = "live feed pipe: server runs as another user - refusing";
        return nullptr;
    }
    return std::make_unique<PipeConn>(h, false);
}

}  // namespace spn
