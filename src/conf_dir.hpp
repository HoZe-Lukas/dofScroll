// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Bounded reading of a directory of "<id>.conf" files (mode files, button mode files): valid ids
// only, at most kMaxFiles files of kMaxFileBytes each, regular files owned by the current user,
// opened without following links. Shared by the mode and the button mode loader.
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace spn::confdir {

constexpr size_t kMaxFiles = 64;
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxDirEntries = 4096;  // readdir cap (names inspected), against huge directories

struct Entry {
    std::string id;    // file name without ".conf"
    std::string path;  // dir + "/" + file name
    std::string text;
};

// Sorted names of "<id>.conf" entries (valid ids only). Scans at most kMaxDirEntries entries.
// false if the directory cannot be opened.
bool listConfNames(const std::string &dir, std::vector<std::string> *names, bool *capped);

// Reads the files that pass the limits, in name order. Problems go to warn(text); a missing
// directory is no error (empty result). noun is used in the "too many <noun>" warning.
std::vector<Entry> readConfDir(const std::string &dir, const char *noun,
                               const std::function<void(const std::string &)> &warn);

// Change detection: prefix plus name, mtime and size of every file.
std::string stamp(const std::string &dir, const char *prefix);

}  // namespace spn::confdir
