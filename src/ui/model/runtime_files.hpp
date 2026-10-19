// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Where the UI finds the daemon's runtime files. Daemon and UI agree on one directory,
// platformPaths().runtimeDir (Linux $XDG_RUNTIME_DIR, Windows %LOCALAPPDATA%/dofscroll, macOS
// ~/Library/Caches/dofscroll); the file names are fixed (src/common.hpp). An empty directory means
// "no safe per-user runtime directory": nothing is read and the daemon counts as not running.
#pragma once
#include <string>

#include "common.hpp"
#include "platform/fs.hpp"

namespace spn::ui {

// Reads the status file (bounded by kStatusMaxBytes, no symlinks, owned by the current user) into
// *text. false when the directory is empty or the file is missing, foreign or oversized.
inline bool readStatusFile(const std::string &runtimeDir, std::string *text) {
    text->clear();
    const std::string path = statusFilePathIn(runtimeDir);
    if (path.empty()) return false;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    return fs::readFileBounded(path, kStatusMaxBytes, opt, text).ok();
}

}  // namespace spn::ui
