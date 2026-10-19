// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "conf_dir.hpp"

#include <algorithm>

#include "modes.hpp"
#include "platform/fs.hpp"

namespace spn::confdir {

bool listConfNames(const std::string &dir, std::vector<std::string> *names, bool *capped) {
    std::vector<std::string> all;
    if (!fs::listDir(dir, kMaxDirEntries, &all, capped)) return false;
    for (const std::string &n : all) {
        if (n.size() <= 5 || n.compare(n.size() - 5, 5, ".conf") != 0) continue;
        if (!validModeId(n.substr(0, n.size() - 5))) continue;
        names->push_back(n);
    }
    std::sort(names->begin(), names->end());
    return true;
}

std::vector<Entry> readConfDir(const std::string &dir, const char *noun,
                               const std::function<void(const std::string &)> &warn) {
    std::vector<Entry> out;
    std::vector<std::string> names;
    bool capped = false;
    if (!listConfNames(dir, &names, &capped)) return out;  // missing directory is no error
    if (capped) warn(dir + ": too many entries - only the first " + std::to_string(kMaxDirEntries) + " were inspected");
    if (names.size() > kMaxFiles) {
        warn(dir + ": too many " + noun + " (max. " + std::to_string(kMaxFiles) + ") - the rest is ignored");
        names.resize(kMaxFiles);
    }

    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    fs::DirHandle dh;
    std::string derr;
    if (!dh.open(dir, &derr)) {
        warn(dir + ": " + derr);
        return out;
    }
    for (const std::string &n : names) {
        const std::string path = dir + "/" + n;
        std::string text;
        const fs::ReadResult rr = dh.readFileBounded(n, kMaxFileBytes, opt, &text);
        switch (rr.status) {
        case fs::ReadStatus::Ok:
            break;
        case fs::ReadStatus::NotFound:
        case fs::ReadStatus::OpenFailed:
            warn(path + ": " + rr.sysError + " - ignored");
            continue;
        case fs::ReadStatus::NotRegular:
            warn(path + ": not a regular file - ignored");
            continue;
        case fs::ReadStatus::NotOwned:
            warn(path + ": not owned by the current user - ignored");
            continue;
        case fs::ReadStatus::TooLargeBySize:
            warn(path + ": file too large (max. " + std::to_string(kMaxFileBytes) + " bytes) - ignored");
            continue;
        case fs::ReadStatus::TooLargeWhileReading:
        case fs::ReadStatus::ReadFailed:
            warn(path + ": unreadable or too large - ignored");
            continue;
        }
        out.push_back({n.substr(0, n.size() - 5), path, std::move(text)});
    }
    return out;
}

std::string stamp(const std::string &dir, const char *prefix) {
    std::vector<std::string> names;
    bool capped = false;
    if (!listConfNames(dir, &names, &capped)) return "";
    std::string s = prefix;
    for (const std::string &n : names) {
        fs::FileInfo fi;
        if (!fs::lstatPath(dir + "/" + n, &fi)) continue;
        s += "|" + n + ":" + std::to_string(static_cast<long long>(fi.mtimeNs / 1'000'000'000)) + "." +
             std::to_string(static_cast<long long>(fi.mtimeNs % 1'000'000'000)) + ":" +
             std::to_string(static_cast<long long>(fi.size));
    }
    return s;
}

}  // namespace spn::confdir
