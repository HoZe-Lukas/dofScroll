// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows implementation of the file-system layer (MSVC and MinGW), see fs.hpp. Security
// equivalents of the POSIX code: reparse points (symlinks, junctions) are refused with
// FILE_FLAG_OPEN_REPARSE_POINT + tag check; the "owned by the current user" check compares the
// owner SID; "0600/0700" is an explicit protected DACL (current user + SYSTEM). Gaps are listed in
// docs/security.md.
#ifndef _WIN32
#error "fs_win.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "fs.hpp"
#include "path_rules.hpp"

namespace spn::fs {

namespace {

std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// UTF-8 path with '/' -> native wide path with '\'.
std::wstring native(const std::string &p) {
    std::wstring w = widen(p);
    for (wchar_t &c : w)
        if (c == L'/') c = L'\\';
    return w;
}

std::string winError(DWORD code) {
    wchar_t *buf = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string s;
    if (n && buf) {
        s = narrow(std::wstring(buf, n));
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    }
    if (buf) ::LocalFree(buf);
    if (s.empty()) s = "error " + std::to_string(static_cast<unsigned long>(code));
    return s;
}

bool setMsg(std::string *err, const std::string &m) {
    if (err) *err = m;
    return false;
}

// Rejects names and forms Windows resolves to something else (reserved device names, alternate
// data streams, trailing dots/spaces, \\?\ and \\.\ prefixes, drive-relative paths); rules and tests
// in path_rules.hpp / tests/test_path_rules.cpp. Our own code never builds such paths.
bool pathOk(const std::string &p, std::string *why = nullptr) {
    const PathHazard h = windowsPathHazard(p);
    if (h == PathHazard::None) return true;
    if (why) *why = std::string("unsafe path (") + pathHazardName(h) + "): " + p;
    return false;
}

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE x) : h(x) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h); }
    bool valid() const { return h != INVALID_HANDLE_VALUE; }
};

int64_t fileTimeNs(const FILETIME &ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // 100 ns ticks since 1601 -> ns since 1970
    return (static_cast<int64_t>(u.QuadPart) - 116444736000000000LL) * 100;
}

// Reparse tag of an open handle (0 when it is not a reparse point). Every reparse point other than
// a plain file or directory is treated as a link: symlinks, junctions, mount points, cloud
// placeholders, dedup/WOF stubs, ... They are refused instead of being allowed by a tag whitelist.
DWORD reparseTag(HANDLE h) {
    FILE_ATTRIBUTE_TAG_INFO ti;
    if (!::GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof ti)) return 0;
    return (ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? ti.ReparseTag : 0;
}

bool statImpl(const std::string &path, bool follow, FileInfo *out) {
    if (!pathOk(path)) return false;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    Handle h(::CreateFileW(native(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, flags, nullptr));
    if (!h.valid()) return false;
    BY_HANDLE_FILE_INFORMATION bi;
    if (!::GetFileInformationByHandle(h.h, &bi)) return false;
    if (out) {
        const bool link = !follow && reparseTag(h.h) != 0;
        const bool dir = (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out->isLink = link;
        out->isDir = !link && dir;
        out->isFile = !link && !dir && ::GetFileType(h.h) == FILE_TYPE_DISK;
        out->size = (static_cast<int64_t>(bi.nFileSizeHigh) << 32) | bi.nFileSizeLow;
        out->mtimeNs = fileTimeNs(bi.ftLastWriteTime);
        out->fileId = (static_cast<uint64_t>(bi.nFileIndexHigh) << 32) | bi.nFileIndexLow;
    }
    return true;
}

// ---- current user and private security attributes ----------------------------------------------

std::vector<unsigned char> currentUserSid() {
    std::vector<unsigned char> sid;
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &tok)) return sid;
    DWORD len = 0;
    ::GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    if (len) {
        std::vector<unsigned char> buf(len);
        if (::GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
            const TOKEN_USER *tu = reinterpret_cast<const TOKEN_USER *>(buf.data());
            const DWORD sl = ::GetLengthSid(tu->User.Sid);
            sid.assign(reinterpret_cast<const unsigned char *>(tu->User.Sid),
                       reinterpret_cast<const unsigned char *>(tu->User.Sid) + sl);
        }
    }
    ::CloseHandle(tok);
    return sid;
}

// Protected DACL: full access for the current user and SYSTEM only, nothing inherited.
class PrivateSecurity {
public:
    explicit PrivateSecurity(bool directory) {
        const std::vector<unsigned char> sid = currentUserSid();
        if (sid.empty()) return;
        LPWSTR sidStr = nullptr;
        if (!::ConvertSidToStringSidW(const_cast<PSID>(reinterpret_cast<const void *>(sid.data())), &sidStr)) return;
        std::wstring sddl = L"D:P(A;";
        sddl += directory ? L"OICI" : L"";
        sddl += L";FA;;;";
        sddl += sidStr;
        sddl += L")(A;";
        sddl += directory ? L"OICI" : L"";
        sddl += L";FA;;;SY)";
        ::LocalFree(sidStr);
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return;
        sd_ = sd;
        sa_.nLength = sizeof sa_;
        sa_.lpSecurityDescriptor = sd_;
        sa_.bInheritHandle = FALSE;
    }
    ~PrivateSecurity() { if (sd_) ::LocalFree(sd_); }
    PrivateSecurity(const PrivateSecurity &) = delete;
    PrivateSecurity &operator=(const PrivateSecurity &) = delete;
    // nullptr when the descriptor could not be built: the object then inherits the parent's ACL
    // (inside %APPDATA% that is already limited to the user, SYSTEM and Administrators).
    SECURITY_ATTRIBUTES *get() { return sd_ ? &sa_ : nullptr; }

private:
    PSECURITY_DESCRIPTOR sd_ = nullptr;
    SECURITY_ATTRIBUTES sa_{};
};

bool ownedByUs(HANDLE h) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (::GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) !=
            ERROR_SUCCESS ||
        !owner) {
        if (sd) ::LocalFree(sd);
        return false;
    }
    bool ok = false;
    const std::vector<unsigned char> me = currentUserSid();
    if (!me.empty() && ::EqualSid(owner, const_cast<PSID>(reinterpret_cast<const void *>(me.data())))) ok = true;
    if (!ok) {  // files created by an elevated process are owned by the Administrators group
        unsigned char adm[SECURITY_MAX_SID_SIZE];
        DWORD sz = sizeof adm;
        if (::CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adm, &sz) && ::EqualSid(owner, adm)) ok = true;
    }
    ::LocalFree(sd);
    return ok;
}

ReadResult readNative(const std::wstring &wpath, size_t cap, const ReadOptions &opt, std::string *out) {
    ReadResult r;
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
    if (opt.refuseLinks) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    Handle h(::CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, flags, nullptr));
    if (!h.valid()) {
        const DWORD e = ::GetLastError();
        r.status = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ReadStatus::NotFound
                                                                            : ReadStatus::OpenFailed;
        r.sysError = winError(e);
        return r;
    }
    if (opt.refuseLinks) {
        const DWORD tag = reparseTag(h.h);
        if (tag != 0) {
            char hex[32];
            std::snprintf(hex, sizeof hex, "0x%08lX", static_cast<unsigned long>(tag));
            r.status = ReadStatus::OpenFailed;
            r.sysError = std::string("reparse point refused (tag ") + hex + ")";
            return r;
        }
    }
    BY_HANDLE_FILE_INFORMATION bi;
    if (!::GetFileInformationByHandle(h.h, &bi) || (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        ::GetFileType(h.h) != FILE_TYPE_DISK) {
        r.status = ReadStatus::NotRegular;
        return r;
    }
    if (opt.requireOwner && !ownedByUs(h.h)) {
        r.status = ReadStatus::NotOwned;
        return r;
    }
    const uint64_t size = (static_cast<uint64_t>(bi.nFileSizeHigh) << 32) | bi.nFileSizeLow;
    if (size > 0 && size > cap) {
        r.status = ReadStatus::TooLargeBySize;
        return r;
    }
    out->clear();
    char buf[16384];
    for (;;) {
        DWORD n = 0;
        if (!::ReadFile(h.h, buf, sizeof buf, &n, nullptr)) {
            r.status = ReadStatus::ReadFailed;
            r.sysError = winError(::GetLastError());
            break;
        }
        if (n == 0) break;
        if (out->size() + n > cap) {
            r.status = ReadStatus::TooLargeWhileReading;
            break;
        }
        out->append(buf, n);
    }
    return r;
}

bool writeAll(HANDLE h, const std::string &data, std::string *why) {
    size_t off = 0;
    while (off < data.size()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 20));
        if (!::WriteFile(h, data.data() + off, chunk, &n, nullptr) || n == 0) {
            *why = winError(::GetLastError());
            return false;
        }
        off += n;
    }
    return true;
}

// 0 if `w` is a directory, otherwise the error code that says why not (captured right at the
// failing call, never read later).
DWORD dirError(const std::wstring &w) {
    const DWORD a = ::GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return ::GetLastError();
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? 0 : ERROR_DIRECTORY;
}
bool dirExists(const std::wstring &w) { return dirError(w) == 0; }

}  // namespace

bool statPath(const std::string &path, FileInfo *out) { return statImpl(path, true, out); }
bool lstatPath(const std::string &path, FileInfo *out) { return statImpl(path, false, out); }

ReadResult unsafeRead(const std::string &why) {
    ReadResult r;
    r.status = ReadStatus::OpenFailed;
    r.sysError = why;
    return r;
}

ReadResult readFileBounded(const std::string &path, size_t cap, const ReadOptions &opt, std::string *out) {
    std::string why;
    if (!pathOk(path, &why)) return unsafeRead(why);
    return readNative(native(path), cap, opt, out);
}

DirHandle::~DirHandle() = default;

bool DirHandle::open(const std::string &dir, std::string *err) {
    std::string why;
    if (!pathOk(dir, &why)) {
        if (err) *err = why;
        return false;
    }
    const DWORD de = dirError(native(dir));
    if (de != 0) {
        if (err) *err = winError(de);
        return false;
    }
    dir_ = dir;
    fd_ = 0;
    return true;
}

ReadResult DirHandle::readFileBounded(const std::string &name, size_t cap, const ReadOptions &opt,
                                      std::string *out) const {
    std::string why;
    if (name.find_first_of("/\\") != std::string::npos) return unsafeRead("unsafe file name (separator): " + name);
    if (!pathOk(dir_ + "/" + name, &why)) return unsafeRead(why);
    return readNative(native(dir_ + "/" + name), cap, opt, out);
}

bool createFileExclusive(const std::string &path, const std::string &data) {
    if (!pathOk(path)) return false;
    PrivateSecurity sec(false);
    const std::wstring wp = native(path);
    const std::wstring wtmp = native(path + ".tmp." + std::to_string(processId()));
    ::DeleteFileW(wtmp.c_str());  // leftover of an earlier run with the same pid
    Handle h(::CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, sec.get(), CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h.valid()) return false;
    std::string why;
    bool ok = writeAll(h.h, data, &why) && ::FlushFileBuffers(h.h);
    ::CloseHandle(h.h);
    h.h = INVALID_HANDLE_VALUE;
    // Without MOVEFILE_REPLACE_EXISTING the move fails if the destination exists: never overwrites,
    // and the final name only ever holds the complete content.
    if (ok && !::MoveFileExW(wtmp.c_str(), wp.c_str(), MOVEFILE_WRITE_THROUGH)) ok = false;
    if (!ok) ::DeleteFileW(wtmp.c_str());
    return ok;
}

bool touchFilePrivate(const std::string &path) {
    if (!pathOk(path)) return false;
    PrivateSecurity sec(false);
    // OPEN_ALWAYS creates the file with the private ACL or opens the existing one; the reparse point
    // itself is opened (never its target) and refused below.
    Handle h(::CreateFileW(native(path).c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, sec.get(), OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h.valid()) return false;
    BY_HANDLE_FILE_INFORMATION info;
    if (!::GetFileInformationByHandle(h.h, &info)) return false;
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return false;
    FILETIME now;
    ::GetSystemTimeAsFileTime(&now);
    return ::SetFileTime(h.h, nullptr, nullptr, &now) != 0;
}

bool removeFile(const std::string &path) { return pathOk(path) && ::DeleteFileW(native(path).c_str()) != 0; }
bool removeDir(const std::string &path) { return pathOk(path) && ::RemoveDirectoryW(native(path).c_str()) != 0; }
bool renamePath(const std::string &from, const std::string &to) {
    return pathOk(from) && pathOk(to) && ::MoveFileExW(native(from).c_str(), native(to).c_str(), MOVEFILE_WRITE_THROUGH) != 0;
}
bool renameNoReplace(const std::string &from, const std::string &to, bool *exists) {
    if (exists) *exists = false;  // MoveFileEx without MOVEFILE_REPLACE_EXISTING never replaces
    if (renamePath(from, to)) return true;
    if (exists) *exists = ::GetFileAttributesW(native(to).c_str()) != INVALID_FILE_ATTRIBUTES;
    return false;
}
long processId() { return static_cast<long>(::GetCurrentProcessId()); }

MkdirResult mkdirPrivate(const std::string &path, std::string *err) {
    if (!pathOk(path, err)) return MkdirResult::Failed;
    const std::wstring w = native(path);
    if (dirExists(w)) return MkdirResult::Exists;
    PrivateSecurity sec(true);
    if (::CreateDirectoryW(w.c_str(), sec.get())) return MkdirResult::Created;
    const DWORD e = ::GetLastError();
    if (e == ERROR_ALREADY_EXISTS) return MkdirResult::Exists;
    if (err) *err = winError(e);
    return MkdirResult::Failed;
}

bool mkdirsPrivate(const std::string &dirIn, std::string *emsg) {
    if (!pathOk(dirIn, emsg)) return false;
    const std::string dir = toSlashes(dirIn);
    // Skip the volume part: "C:" or "//server/share".
    size_t i = 0;
    if (dir.size() >= 2 && dir[1] == ':') {
        i = 2;
    } else if (dir.compare(0, 2, "//") == 0) {
        size_t p = dir.find('/', 2);          // end of the server name
        p = p == std::string::npos ? p : dir.find('/', p + 1);  // end of the share name
        i = p == std::string::npos ? dir.size() : p;
    }
    while (i <= dir.size()) {
        size_t j = dir.find('/', i);
        if (j == std::string::npos) j = dir.size();
        const std::string cur = dir.substr(0, j);
        if (!cur.empty() && cur != "." && cur != ".." && j > i) {
            std::string why;
            if (mkdirPrivate(cur, &why) == MkdirResult::Failed) {
                if (emsg) *emsg = cur + ": " + why;
                return false;
            }
        }
        i = j + 1;
    }
    return true;
}

bool listDir(const std::string &dir, size_t maxEntries, std::vector<std::string> *names, bool *capped) {
    if (!pathOk(dir)) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((native(dir) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    if (capped) *capped = false;
    size_t seen = 0;
    do {
        if (maxEntries && ++seen > maxEntries) {
            if (capped) *capped = true;
            break;
        }
        names->push_back(narrow(fd.cFileName));
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return true;
}

void randomHex16(char out[17]) {
    unsigned char r[8];
    if (::BCryptGenRandom(nullptr, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        const unsigned long long v = static_cast<unsigned long long>(::GetTickCount64()) * 6364136223846793005ULL ^
                                     static_cast<unsigned long long>(::GetCurrentProcessId());
        std::memcpy(r, &v, sizeof r);
    }
    for (int k = 0; k < 8; k++) std::snprintf(out + 2 * k, 3, "%02x", r[k]);
}

static bool replaceFileImpl(const std::string &pathIn, const std::string &data, bool followLink, std::string *err) {
    std::string real = toSlashes(pathIn);
    if (!pathOk(real, err)) return false;
    if (!followLink) {  // runtime files: a link at the final name is refused (untested on Windows)
        FileInfo nl;
        if (lstatPath(real, &nl) && (nl.isLink || !nl.isFile)) return setMsg(err, real + ": not a regular file");
    }
    // A link at the target is followed once: the temp file goes next to the real file.
    FileInfo li;
    if (followLink && lstatPath(real, &li) && li.isLink) {
        // Only a symbolic link is followed; junctions, mount points and every other reparse point
        // are refused.
        Handle t(::CreateFileW(native(real).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        const DWORD tag = t.valid() ? reparseTag(t.h) : 0;
        if (tag != IO_REPARSE_TAG_SYMLINK) {
            char hex[32];
            std::snprintf(hex, sizeof hex, "0x%08lX", static_cast<unsigned long>(tag));
            return setMsg(err, real + ": reparse point refused (tag " + hex + ")");
        }
        Handle h(::CreateFileW(native(real).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!h.valid()) return setMsg(err, real + ": " + winError(::GetLastError()));
        wchar_t buf[4096];
        const DWORD n = ::GetFinalPathNameByHandleW(h.h, buf, 4096, FILE_NAME_NORMALIZED);
        if (n == 0 || n >= 4096) return setMsg(err, real + ": cannot resolve link");
        // "\\?\C:\x" -> "C:\x" and "\\?\UNC\server\share\x" -> "\\server\share\x"
        real = toSlashes(stripVerbatimPrefix(narrow(std::wstring(buf, n))));
        if (!pathOk(real, err)) return false;
    }
    std::string dir, base;
    {
        const size_t slash = real.rfind('/');
        if (slash == std::string::npos) { dir = "."; base = real; }
        else { dir = slash == 0 ? "/" : real.substr(0, slash); base = real.substr(slash + 1); }
    }
    if (base.empty() || base == "." || base == "..") return setMsg(err, real + ": invalid file name");
    if (dir != "." && dir != "/") {
        std::string emsg;
        if (!mkdirsPrivate(dir, &emsg)) return setMsg(err, emsg);
    }
    FileInfo ei;
    if (lstatPath(real, &ei) && !ei.isFile) return setMsg(err, real + ": not a regular file");

    PrivateSecurity sec(false);
    std::string tmp;
    std::wstring wtmp;
    Handle h;
    for (int attempt = 0; attempt < 8 && !h.valid(); attempt++) {
        char suf[17];
        randomHex16(suf);
        tmp = base + ".tmp." + suf;
        wtmp = native(dir + "/" + tmp);
        h.h = ::CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, sec.get(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!h.valid() && ::GetLastError() != ERROR_FILE_EXISTS && ::GetLastError() != ERROR_ALREADY_EXISTS)
            return setMsg(err, dir + "/" + tmp + ": " + winError(::GetLastError()));
    }
    if (!h.valid()) return setMsg(err, dir + "/" + base + ".tmp.*: file exists");

    std::string m, why;
    if (!writeAll(h.h, data, &why)) m = tmp + ": write failed: " + why;
    if (m.empty() && !::FlushFileBuffers(h.h)) m = tmp + ": flush: " + winError(::GetLastError());
    ::CloseHandle(h.h);
    h.h = INVALID_HANDLE_VALUE;
    if (m.empty()) {
        const std::wstring wreal = native(real);
        DWORD e = 0;
        for (int attempt = 0; attempt < 10; attempt++) {  // a reader without FILE_SHARE_DELETE may hold it briefly
            if (::MoveFileExW(wtmp.c_str(), wreal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                e = 0;
                break;
            }
            e = ::GetLastError();
            if (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) break;
            ::Sleep(25);
        }
        if (e) m = real + ": " + winError(e);
    }
    if (!m.empty()) {
        ::DeleteFileW(wtmp.c_str());
        return setMsg(err, m);
    }
    return true;
}

bool writeFileReplace(const std::string &pathIn, const std::string &data, std::string *err) {
    return replaceFileImpl(pathIn, data, true, err);
}

bool writeFileReplaceNoFollow(const std::string &pathIn, const std::string &data, std::string *err) {
    return replaceFileImpl(pathIn, data, false, err);
}

std::string executablePath() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (n == 0 || n >= MAX_PATH * 4) return "";
    return toSlashes(narrow(std::wstring(buf, n)));
}

namespace {
std::string envVar(const wchar_t *name) {
    wchar_t buf[32768];
    const DWORD n = ::GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768) return "";
    return narrow(std::wstring(buf, n));
}
}  // namespace

std::string getEnvUtf8(const char *name) {
    std::wstring w;
    for (const char *c = name; *c; ++c) w += static_cast<wchar_t>(*c);  // names are ASCII
    return envVar(w.c_str());
}

std::string userConfigBase() { return windowsConfigBase(envVar(L"APPDATA"), envVar(L"USERPROFILE")); }

size_t lastSeparator(const std::string &p) { return p.find_last_of("/\\"); }

}  // namespace spn::fs
