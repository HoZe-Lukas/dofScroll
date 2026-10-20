// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Text of the in-app log viewer: the last kLogViewBytes of the daemon log (<runtime dir>/dofscroll.log),
// cut at a line start, with control characters made harmless (the log can hold names of programs
// and devices). The daemon keeps the file small (kLogMaxBytes), the read is bounded anyway. No toolkit.
#pragma once
#include <string>

#include "common.hpp"
#include "log_sanitize.hpp"
#include "platform/fs.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

constexpr size_t kLogViewBytes = 64 * 1024;          // what the dialog shows at most
constexpr size_t kLogViewReadCap = 4 * 1024 * 1024;  // files above this are refused (the daemon never writes them)
constexpr size_t kLogViewLineBytes = 2000;           // longer lines are shortened

struct LogView {
    bool ok = false;
    bool cut = false;      // older lines were left out
    std::string text;      // safe to show
    std::string error;     // translated, when !ok
    std::string path;
};

// The last `maxBytes` of `content`, starting at a line start (when it had to cut, the first, partial
// line is dropped), every line sanitized.
inline std::string logTail(const std::string &content, size_t maxBytes, bool *cut = nullptr) {
    size_t start = 0;
    if (content.size() > maxBytes) {
        start = content.size() - maxBytes;
        const size_t nl = content.find('\n', start - 1);  // the line that starts exactly at `start` is whole
        start = nl == std::string::npos ? content.size() : nl + 1;
        if (cut) *cut = true;
    }
    std::string out;
    size_t pos = start;
    while (pos < content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? content.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out += sanitizeForLog(line, kLogViewLineBytes);
        out += '\n';
    }
    return out;
}

inline LogView readLogView(const std::string &runtimeDir, const StringProvider &s, size_t maxBytes = kLogViewBytes) {
    LogView v;
    v.path = logFilePathIn(runtimeDir);
    if (v.path.empty()) {
        v.error = s.tr("No log file: the runtime directory is not available");
        return v;
    }
    std::string content;
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    const fs::ReadResult r = fs::readFileBounded(v.path, kLogViewReadCap, opt, &content);
    if (r.status == fs::ReadStatus::NotFound) {
        v.error = s.tr("There is no log file yet. The daemon writes it while it runs.");
        return v;
    }
    if (!r.ok()) {
        v.error = format(s.tr("Cannot read the log file: %1"), {r.sysError.empty() ? v.path : r.sysError});
        return v;
    }
    v.ok = true;
    v.text = logTail(content, maxBytes, &v.cut);
    return v;
}

}  // namespace spn::ui
