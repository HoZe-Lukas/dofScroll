// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Small portable helpers for the tests (temp directories, recursive delete, process id,
// environment, configuration base directory). Tests keep using plain asserts; anything that is
// POSIX-only by nature (symlinks, mode bits, rlimit, FIFOs) stays behind #ifndef _WIN32.
#pragma once
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tu {

namespace stdfs = std::filesystem;

inline long pid() {
#ifdef _WIN32
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

// Directory for temporary test data ($TMPDIR, %TEMP% or /tmp), '/' separators, no trailing slash.
inline std::string tmpRoot() {
    std::error_code ec;
    std::string p = stdfs::temp_directory_path(ec).generic_string();
    if (ec || p.empty()) p = ".";
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// Creates a fresh, empty, private directory "<tmp>/<prefix>.<pid>.<n>" and returns its path.
inline std::string makeTempDir(const std::string &prefix) {
    static int counter = 0;
    for (int i = 0; i < 1000; i++) {
        const std::string p = tmpRoot() + "/" + prefix + "." + std::to_string(pid()) + "." + std::to_string(++counter);
        std::error_code ec;
        if (stdfs::create_directory(p, ec) && !ec) {
#ifndef _WIN32
            ::chmod(p.c_str(), 0700);
#endif
            return p;
        }
    }
    assert(false && "cannot create a temporary directory");
    return "";
}

// rm -rf (symlinks are removed, not followed).
inline void removeAll(const std::string &p) {
    std::error_code ec;
    stdfs::remove_all(p, ec);
}

inline bool exists(const std::string &p) {
    std::error_code ec;
    return stdfs::symlink_status(p, ec).type() != stdfs::file_type::not_found && !ec;
}

inline void setEnv(const char *name, const std::string &value) {
#ifdef _WIN32
    ::_putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

inline void unsetEnv(const char *name) {
#ifdef _WIN32
    ::_putenv_s(name, "");  // an empty value removes the variable
#else
    ::unsetenv(name);
#endif
}

// Points the per-user configuration base (fs::userConfigBase()) at a directory below `root` and
// returns that base: Linux $XDG_CONFIG_HOME=root, macOS $HOME=root (base = root/Library/Application
// Support), Windows %APPDATA%=root. The returned directory exists.
inline std::string useConfigBase(const std::string &root) {
#if defined(_WIN32)
    setEnv("APPDATA", root);
    return root;
#elif defined(__APPLE__)
    setEnv("HOME", root);
    const std::string base = root + "/Library/Application Support";
    std::error_code ec;
    stdfs::create_directories(base, ec);
    return base;
#else
    setEnv("XDG_CONFIG_HOME", root);
    return root;
#endif
}

}  // namespace tu
