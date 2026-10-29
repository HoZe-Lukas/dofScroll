// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Cleanup of the official 3Dconnexion driver's per-program axis settings. When the driver has an axis
// active for a program (pan, zoom, ... mapped to the mouse wheel or to keys), the program gets the
// driver's input and dofscroll's scroll and zoom, so every movement arrives twice. The driver keeps one
// XML file per program (Windows: %APPDATA%\3Dconnexion\3DxWare\Cfg\<program>.xml); in it every axis of
// an <AxisBank> has <Enabled>true|false</Enabled>. This model lists the programs that have active
// axes, switches them off in the files the user picked (a text edit of those flags only: BOM, comments
// and formatting stay), keeps a backup of every changed file and can put the backup back.
// Programs whose active axes only pass the motion on to the program (HIDMultiAxis_*: 3D navigation of
// CAD and 3D programs) are listed but not picked by default. No toolkit, no writing outside `dir`.
//
//   std::vector<DriverApp> apps;  scanDriverConfigs(driverConfigDir(), &apps, &err);
//   apps[i].selected = ...;       applyDriverCleanup(dir, apps, "20261023-101500", &report);
//   restoreDriverBackup(dir, "20261023-101500", &report);
#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "platform/fs.hpp"

namespace spn::ui {

// What the driver does with an active axis.
enum class DriverOutput { MouseWheel, Keystroke, MouseMove, Joystick, Passthrough, Other };

struct DriverAxis {
    std::string input;   // "X", "Y", "Z", "Rx", "Ry", "Rz" (driver names, not dofscroll's)
    DriverOutput output = DriverOutput::Other;
    std::string outputId;  // the driver's action id, e.g. "HIDMouse_Wheel"
};

struct DriverApp {
    std::string file;  // "Chrome.xml" (name only, in the driver's folder)
    std::string name;  // display name: the program name of the file, else the file name
    std::string exe;   // "chrome.exe" or empty
    std::vector<DriverAxis> active;  // axes with <Enabled>true</Enabled>
    bool navigation = false;  // every active axis passes the motion on to the program (3D navigation)
    bool selected = false;    // to clean up; set by the scan to !navigation
};

struct DriverCleanupReport {
    std::vector<std::string> changed;  // files whose axes were switched off
    std::vector<std::string> failed;   // "<file>: reason"
    std::string backupDir;             // where the originals are
};

constexpr size_t kMaxDriverFiles = 512;
constexpr size_t kMaxDriverFileBytes = 1024 * 1024;
inline constexpr const char *kDriverBackupDir = "dofscroll-backup";

// The driver's per-program folder; empty where it is not known (only Windows).
inline std::string driverConfigDir() {
#ifdef _WIN32
    const std::string appdata = fs::getEnvUtf8("APPDATA");
    if (appdata.empty()) return "";
    std::string d = appdata;
    for (char &c : d)
        if (c == '\\') c = '/';
    return d + "/3Dconnexion/3DxWare/Cfg";
#else
    return "";
#endif
}

namespace driver_detail {

struct Span {
    size_t begin = 0, end = 0;  // [begin, end) of the text between <tag> and </tag>
};

inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The first <tag>...</tag> in xml[from, to). Attributes on the opening tag are allowed.
inline bool findElement(const std::string &xml, const std::string &tag, size_t from, size_t to, Span *out) {
    const std::string open = "<" + tag;
    size_t p = from;
    while ((p = xml.find(open, p)) != std::string::npos && p < to) {
        const size_t after = p + open.size();
        if (after >= xml.size()) return false;
        const char c = xml[after];
        if (c != '>' && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '/') {
            p = after;  // a longer tag name such as <AxisBank>
            continue;
        }
        const size_t gt = xml.find('>', after);
        if (gt == std::string::npos || gt >= to) return false;
        if (xml[gt - 1] == '/') {  // <tag/>: empty element
            p = gt + 1;
            continue;
        }
        const size_t close = xml.find("</" + tag + ">", gt + 1);
        if (close == std::string::npos || close > to) return false;
        out->begin = gt + 1;
        out->end = close;
        return true;
    }
    return false;
}

inline std::string textOf(const std::string &xml, const std::string &tag, size_t from, size_t to) {
    Span s;
    return findElement(xml, tag, from, to, &s) ? trim(xml.substr(s.begin, s.end - s.begin)) : std::string();
}

// The <Axis> elements inside <AxisBank> elements.
inline std::vector<Span> axisSpans(const std::string &xml) {
    std::vector<Span> out;
    size_t pos = 0;
    Span bank;
    while (findElement(xml, "AxisBank", pos, xml.size(), &bank)) {
        size_t p = bank.begin;
        Span ax;
        while (findElement(xml, "Axis", p, bank.end, &ax)) {
            out.push_back(ax);
            p = ax.end;
        }
        pos = bank.end;
    }
    return out;
}

inline DriverOutput classify(const std::string &id) {
    auto starts = [&](const char *p) { return id.rfind(p, 0) == 0; };
    if (id == "HIDMouse_Wheel" || id == "HIDMouse_HWheel") return DriverOutput::MouseWheel;
    if (starts("KB_")) return DriverOutput::Keystroke;
    if (starts("HIDMouse_")) return DriverOutput::MouseMove;
    if (starts("HIDJoystick_")) return DriverOutput::Joystick;
    if (starts("HIDMultiAxis_")) return DriverOutput::Passthrough;
    return DriverOutput::Other;
}

inline std::string lowerAscii(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool endsWithXml(const std::string &n) {
    return n.size() > 4 && lowerAscii(n.substr(n.size() - 4)) == ".xml";
}

inline bool plainName(const std::string &n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos && n.find('\\') == std::string::npos &&
           n.find(':') == std::string::npos;
}

// A backup stamp names a folder: digits and '-' only.
inline bool validStamp(const std::string &s) {
    if (s.empty() || s.size() > 32) return false;
    for (char c : s)
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
    return true;
}

}  // namespace driver_detail

// Parses one file. Returns false when it has no <AxisBank> (nothing to clean up).
inline bool parseDriverFile(const std::string &fileName, const std::string &xml, DriverApp *app) {
    using namespace driver_detail;
    const std::vector<Span> axes = axisSpans(xml);
    if (axes.empty()) return false;
    DriverApp a;
    a.file = fileName;
    Span sig;
    if (findElement(xml, "Signature", 0, xml.size(), &sig)) {
        a.name = textOf(xml, "Name", sig.begin, sig.end);
        a.exe = textOf(xml, "ExecutableName", sig.begin, sig.end);
    }
    if (a.name.empty() || a.name.rfind("STR_", 0) == 0)  // a string key of the driver, not a name
        a.name = fileName.substr(0, fileName.size() - 4);
    for (const Span &s : axes) {
        if (lowerAscii(textOf(xml, "Enabled", s.begin, s.end)) != "true") continue;
        DriverAxis d;
        Span in, out;
        if (findElement(xml, "Input", s.begin, s.end, &in)) {
            d.input = textOf(xml, "ActionID", in.begin, in.end);
            const size_t us = d.input.rfind('_');
            if (us != std::string::npos) d.input = d.input.substr(us + 1);  // HIDMultiAxis_Rx -> Rx
        }
        if (findElement(xml, "Output", s.begin, s.end, &out)) d.outputId = textOf(xml, "ActionID", out.begin, out.end);
        d.output = classify(d.outputId);
        a.active.push_back(d);
    }
    a.navigation = !a.active.empty() &&
                   std::all_of(a.active.begin(), a.active.end(), [](const DriverAxis &d) { return d.output == DriverOutput::Passthrough; });
    a.selected = !a.active.empty() && !a.navigation;
    *app = a;
    return true;
}

// The XML text with every active axis of every <AxisBank> switched off; *changed counts them.
inline std::string disableDriverAxes(const std::string &xml, int *changed) {
    using namespace driver_detail;
    std::string out = xml;
    int n = 0;
    const std::vector<Span> axes = axisSpans(xml);
    for (auto it = axes.rbegin(); it != axes.rend(); ++it) {  // from the back: earlier offsets stay valid
        Span e;
        if (!findElement(out, "Enabled", it->begin, it->end, &e)) continue;
        if (lowerAscii(trim(out.substr(e.begin, e.end - e.begin))) != "true") continue;
        out.replace(e.begin, e.end - e.begin, "false");
        ++n;
    }
    if (changed) *changed = n;
    return out;
}

// Lists the programs of the driver folder that have active axes (sorted by name). false + *err when
// the folder cannot be read. Global.xml and UserInfo.xml are not programs.
// *truncated is set when the folder holds more entries than are looked at (kMaxDriverFiles): the list is then partial.
inline bool scanDriverConfigs(const std::string &dir, std::vector<DriverApp> *apps, std::string *err,
                              bool *truncated = nullptr) {
    using namespace driver_detail;
    apps->clear();
    std::vector<std::string> names;
    bool capped = false;
    if (truncated) *truncated = false;
    if (dir.empty() || !fs::listDir(dir, kMaxDriverFiles, &names, &capped)) {
        if (err) *err = "the driver's settings folder cannot be read";
        return false;
    }
    if (truncated) *truncated = capped;
    for (const std::string &n : names) {
        if (!plainName(n) || !endsWithXml(n)) continue;
        const std::string low = lowerAscii(n);
        if (low == "global.xml" || low == "userinfo.xml") continue;
        std::string xml;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        if (!fs::readFileBounded(dir + "/" + n, kMaxDriverFileBytes, opt, &xml).ok()) continue;
        DriverApp a;
        if (parseDriverFile(n, xml, &a) && !a.active.empty()) apps->push_back(a);
    }
    std::sort(apps->begin(), apps->end(),
              [](const DriverApp &a, const DriverApp &b) { return lowerAscii(a.name) < lowerAscii(b.name); });
    return true;
}

// Switches the axes of the selected programs off. For every file the original is first copied to
// <dir>/dofscroll-backup/<stamp>/ (no change without a backup), then the file is replaced and read
// back; if the read-back still shows an active axis the original is put back. Never throws.
inline bool applyDriverCleanup(const std::string &dir, const std::vector<DriverApp> &apps, const std::string &stamp,
                               DriverCleanupReport *report) {
    using namespace driver_detail;
    *report = DriverCleanupReport();
    if (!validStamp(stamp) || dir.empty()) {
        report->failed.push_back("invalid backup name");
        return false;
    }
    std::string bdir = dir + "/" + kDriverBackupDir + "/" + stamp;
    report->backupDir = bdir;
    bool madeDir = false;
    for (const DriverApp &a : apps) {
        if (!a.selected) continue;
        if (!plainName(a.file) || !endsWithXml(a.file)) {
            report->failed.push_back(a.file + ": not a driver file name");
            continue;
        }
        std::string xml, e;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        if (!fs::readFileBounded(dir + "/" + a.file, kMaxDriverFileBytes, opt, &xml).ok()) {
            report->failed.push_back(a.file + ": cannot be read");
            continue;
        }
        int n = 0;
        const std::string cleaned = disableDriverAxes(xml, &n);
        if (n == 0) continue;  // nothing active any more
        if (!madeDir) {
            // A stamp has a resolution of one second: a second cleanup in the same second would back up the files
            // the first one already cleaned, over its backup of the originals. Every run gets a folder of its own.
            const std::string base = dir + "/" + kDriverBackupDir;
            bool made = fs::mkdirsPrivate(base, &e);
            for (int k = 1; made && k < 100; ++k) {
                const std::string cand = k == 1 ? stamp : stamp + "-" + std::to_string(k);
                if (!validStamp(cand)) {
                    made = false;
                    break;
                }
                const fs::MkdirResult mk = fs::mkdirPrivate(base + "/" + cand, &e);
                if (mk == fs::MkdirResult::Failed) {
                    made = false;
                    break;
                }
                if (mk == fs::MkdirResult::Created) {
                    bdir = base + "/" + cand;
                    report->backupDir = bdir;
                    break;
                }
                if (k == 99) made = false;
            }
            if (!made) {
                report->failed.push_back(a.file + ": the backup folder cannot be created");
                return false;
            }
            madeDir = true;
        }
        if (!fs::writeFileReplace(bdir + "/" + a.file, xml, &e)) {
            report->failed.push_back(a.file + ": the backup cannot be written, file left unchanged");
            continue;
        }
        if (!fs::writeFileReplace(dir + "/" + a.file, cleaned, &e)) {
            report->failed.push_back(a.file + ": cannot be written (" + e + ")");
            continue;
        }
        std::string back;
        DriverApp check;
        if (!fs::readFileBounded(dir + "/" + a.file, kMaxDriverFileBytes, opt, &back).ok() || back != cleaned ||
            (parseDriverFile(a.file, back, &check) && !check.active.empty())) {
            fs::writeFileReplace(dir + "/" + a.file, xml, &e);  // put the original back
            report->failed.push_back(a.file + ": the change did not hold, original restored");
            continue;
        }
        report->changed.push_back(a.file);
    }
    return report->failed.empty();
}

// Stamps of the backups, newest first (the stamps sort as time).
// *truncated: more entries than looked at (the list is partial).
inline std::vector<std::string> listDriverBackups(const std::string &dir, bool *truncated = nullptr) {
    using namespace driver_detail;
    std::vector<std::string> out, names;
    bool capped = false;
    if (truncated) *truncated = false;
    if (dir.empty() || !fs::listDir(dir + "/" + kDriverBackupDir, 1024, &names, &capped)) return out;
    if (truncated) *truncated = capped;
    for (const std::string &n : names)
        if (validStamp(n)) out.push_back(n);
    std::sort(out.begin(), out.end(), std::greater<std::string>());
    return out;
}

// Puts the files of one backup back (overwrites the current ones; a link at the destination is not written through).
inline bool restoreDriverBackup(const std::string &dir, const std::string &stamp, DriverCleanupReport *report) {
    using namespace driver_detail;
    *report = DriverCleanupReport();
    if (!validStamp(stamp) || dir.empty()) {
        report->failed.push_back("invalid backup name");
        return false;
    }
    const std::string bdir = dir + "/" + kDriverBackupDir + "/" + stamp;
    report->backupDir = bdir;
    std::vector<std::string> names;
    bool capped = false;
    if (!fs::listDir(bdir, kMaxDriverFiles, &names, &capped)) {
        report->failed.push_back("the backup cannot be read");
        return false;
    }
    if (capped) report->failed.push_back("the backup folder holds more files than are restored");
    for (const std::string &n : names) {
        if (!plainName(n) || !endsWithXml(n)) continue;
        std::string xml, e;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        if (!fs::readFileBounded(bdir + "/" + n, kMaxDriverFileBytes, opt, &xml).ok()) {
            report->failed.push_back(n + ": cannot be read");
            continue;
        }
        // not through a link: the driver's folder is the user's, but a link there may point anywhere
        if (!fs::writeFileReplaceNoFollow(dir + "/" + n, xml, &e)) {
            report->failed.push_back(n + ": cannot be written (" + e + ")");
            continue;
        }
        report->changed.push_back(n);
    }
    return report->failed.empty();
}

}  // namespace spn::ui
