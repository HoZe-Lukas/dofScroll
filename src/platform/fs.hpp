// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// File-system layer: the few security-relevant file operations the shared core needs (bounded
// reads that refuse symlinks / foreign owners, private directories, atomic replace, directory
// listing). One implementation per OS family: fs_posix.cpp (Linux, macOS) and fs_win.cpp
// (Windows). Paths are UTF-8; '/' is accepted as separator everywhere (Windows converts).
// The POSIX implementation is the code that used to live in config.cpp, modes.cpp, seed.cpp and
// hid_descriptor.cpp, moved unchanged. Security notes per OS: docs/security.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spn::fs {

struct FileInfo {
    bool isFile = false;     // regular file
    bool isDir = false;
    bool isLink = false;     // symlink (Windows: any reparse point); only set by lstatPath
    int64_t size = 0;
    int64_t mtimeNs = 0;     // modification time, ns since the epoch (Windows: 100 ns resolution)
    int64_t ctimeNs = 0;     // inode change time (POSIX; 0 on Windows): a same-size rewrite within one mtime tick still shows
    uint64_t fileId = 0;     // inode / file index; changes when a file is replaced by a rename
};
// false when the path does not exist (or cannot be examined). statPath follows links, lstatPath
// does not.
bool statPath(const std::string &path, FileInfo *out);
bool lstatPath(const std::string &path, FileInfo *out);
inline bool pathExists(const std::string &p) { FileInfo i; return lstatPath(p, &i); }

enum class ReadStatus {
    Ok,
    NotFound,               // open failed with "does not exist"
    OpenFailed,             // open failed otherwise (e.g. symlink refused, permission); see sysError
    NotRegular,             // not a regular file (directory, device, FIFO, ...)
    NotOwned,               // requireOwner and the file belongs to someone else
    TooLargeBySize,         // reported size above the cap (nothing was read)
    TooLargeWhileReading,   // grew past the cap while reading
    ReadFailed,             // read error; see sysError
};
struct ReadOptions {
    bool refuseLinks = false;   // POSIX: O_NOFOLLOW; Windows: reparse points are refused
    bool requireOwner = false;  // owner must be the current user (Windows: or Administrators)
    bool requirePrivate = false;  // POSIX: no group/other permission bits (Windows: private ACL comes from the
                                  // directory, nothing extra is checked); refused as NotOwned
};
struct ReadResult {
    ReadStatus status = ReadStatus::Ok;
    std::string sysError;       // OS error text for OpenFailed / ReadFailed
    // POSIX, set on a successful open of a regular file (never set on Windows): the file belongs to another user, or
    // others (or a group that is not the user's own) can write it. The read goes on; the caller decides whether to say so.
    bool foreignOwner = false;
    bool looseMode = false;
    bool ok() const { return status == ReadStatus::Ok; }
};
// Opens the file once and works only through the handle (no stat/open race): regular-file check,
// optional owner check, size check against `cap`, then a bounded read.
ReadResult readFileBounded(const std::string &path, size_t cap, const ReadOptions &opt, std::string *out);

// Directory held open while several files in it are read, so a swapped directory cannot redirect
// later opens (POSIX: openat on the directory descriptor; Windows: path based, see
// docs/security.md).
class DirHandle {
public:
    DirHandle() = default;
    ~DirHandle();
    DirHandle(const DirHandle &) = delete;
    DirHandle &operator=(const DirHandle &) = delete;
    bool open(const std::string &dir, std::string *err);  // *err = OS error text
    // `name` is a single file name inside the directory.
    ReadResult readFileBounded(const std::string &name, size_t cap, const ReadOptions &opt, std::string *out) const;

private:
    std::string dir_;
    int fd_ = -1;
};

// Creates a new file that must not exist yet (no symlink following), mode 0600 / user only. The
// final name only ever holds the complete content (written to a temp file next to it and then
// published without overwriting). Returns false if the file exists or anything fails.
bool createFileExclusive(const std::string &path, const std::string &data);

// Replaces `path` atomically with `data` (temp file next to the target, flush, rename). Missing
// parent directories are created private (0700 / user only). POSIX: a symlink at `path` is
// resolved first so the link survives, the mode of an existing file is kept, new files are 0600.
// On failure *err holds a one-line message.
bool writeFileReplace(const std::string &path, const std::string &data, std::string *err);

// Like writeFileReplace, but for runtime files (status, log): a symlink or any other non-regular
// file at the final name is refused, never followed and never replaced.
bool writeFileReplaceNoFollow(const std::string &path, const std::string &data, std::string *err);

// Creates the file empty (user-only) or sets its modification time to now, never following a link at
// the final name. false when the path is a link, a directory, another user's file or the call fails.
// Used for heartbeat files: the content is irrelevant, the age is the message.
bool touchFilePrivate(const std::string &path);

bool removeFile(const std::string &path);          // unlink (never follows a link at the end)
bool removeDir(const std::string &path);           // rmdir (empty directories only)
// Renames a file or directory; the destination of a directory rename must not exist (POSIX also
// accepts an empty directory there, Windows does not replace anything).
bool renamePath(const std::string &from, const std::string &to);
// Renames a file and never replaces anything at `to`, also not one that appears while this runs (no check-then-rename
// gap). false when `to` exists (*exists = true) or the call fails.
bool renameNoReplace(const std::string &from, const std::string &to, bool *exists = nullptr);
long processId();

enum class MkdirResult { Created, Exists, Failed };
// Creates one directory level, private (0700 / user only). `Exists` also for non-directories (the
// caller checks). *err = OS error text for Failed.
MkdirResult mkdirPrivate(const std::string &path, std::string *err);
// Like mkdir -p; existing components stay untouched. *err = "<component>: <reason>".
bool mkdirsPrivate(const std::string &dir, std::string *err);

// Entry names of a directory (including "." and ".."), at most `maxEntries` of them (0 = no
// limit); *capped is set when more existed. false if the directory cannot be opened.
bool listDir(const std::string &dir, size_t maxEntries, std::vector<std::string> *names, bool *capped);

// Hex string of 8 random bytes (+ NUL) for temp file names. Never fails (falls back to time+pid).
void randomHex16(char out[17]);

// Full path of the running executable ("" if unknown), with '/' separators.
std::string executablePath();

// Base directory of the per-user configuration (without a trailing separator, '/' separators):
// Linux $XDG_CONFIG_HOME or ~/.config, macOS ~/Library/Application Support, Windows %APPDATA%.
std::string userConfigBase();

// Environment variable as UTF-8 ("" when unset). Windows reads the wide variable: std::getenv would
// return the ANSI code page text and lose non-ASCII characters.
std::string getEnvUtf8(const char *name);

// Position of the last path separator ('/' everywhere, also '\\' on Windows), npos if none.
size_t lastSeparator(const std::string &p);

}  // namespace spn::fs
