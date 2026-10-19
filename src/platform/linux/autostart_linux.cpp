// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux "Start at login": `systemctl --user enable|disable` of dofscroll.service and
// dofscroll-ui.service (the UI unit is optional). Thin: argv building and state parsing are in
// src/platform/autostart_os.hpp. Never --now; no shell.
#include <string>
#include <vector>

#include "../autostart_os.hpp"
#include "../exec_posix.hpp"

namespace spn {
namespace autostart {

namespace {

Entry queryUnit(const char *unit, bool optional) {
    const ExecResult r = execCapture(systemctlArgv("is-enabled", {unit}));
    if (!r.started) {
        Entry e;
        e.name = unit;
        e.kind = Entry::Failed;
        e.why = r.error;
        return e;
    }
    return entryFromIsEnabled(unit, r.out, r.err, optional);
}

std::vector<Entry> queryAll() {
    return {queryUnit(kLinuxDaemonUnit, false), queryUnit(kLinuxUiUnit, true)};
}

}  // namespace

AutostartState queryAutostart(std::string *detail) {
    return combineEntries(queryAll(), detail);
}

bool setAutostart(bool enabled, std::string *err) {
    auto fail = [&](const std::string &m) { if (err) *err = m; return false; };
    const std::vector<Entry> es = queryAll();
    std::string why;
    const AutostartState st = combineEntries(es, &why);
    if (st == AutostartState::Unavailable || st == AutostartState::Error) return fail(why);
    std::vector<std::string> units;
    for (const Entry &e : es)
        if (e.kind != Entry::Missing) units.push_back(e.name);
    const ExecResult r = execCapture(systemctlArgv(enabled ? "enable" : "disable", units));
    if (!r.started) return fail(r.error);
    if (r.status != 0) {
        std::string m = r.err.empty() ? r.out : r.err;
        while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
        return fail(m.empty() ? "systemctl failed" : m);
    }
    return true;
}

}  // namespace autostart
}  // namespace spn
