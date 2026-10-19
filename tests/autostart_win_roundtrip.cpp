// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows only: round trip on the real HKCU Run key with the test value name `dofscroll-test`
// (never the real value names). The programs are copies of this test executable in a folder
// with spaces and non-ASCII characters. The values are removed at the end, also on failure.
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

#include "platform/win/autostart_win.hpp"
#include "testutil.hpp"

using namespace spn::autostart;
namespace stdfs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
    wchar_t self[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    CHECK(n > 0 && n < MAX_PATH);
    const stdfs::path dir = stdfs::path(tu::makeTempDir("dofscroll-autostart")) / L"Prögramme (x86) test";
    stdfs::create_directories(dir);
    const stdfs::path exe = dir / L"dofscroll-test.exe";
    stdfs::copy_file(self, exe, stdfs::copy_options::overwrite_existing);
    const std::string utf8 = exe.u8string();

    const std::vector<RunEntry> es = {{"dofscroll-test", utf8, false}, {"dofscroll-test-ui", (dir / L"missing-ui.exe").u8string(), true}};
    std::string detail, err;
    setRunEntries(es, false, &err);  // clean start
    CHECK(queryRunEntries(es, &detail) == AutostartState::Disabled);
    CHECK(setRunEntries(es, true, &err));
    CHECK(queryRunEntries(es, &detail) == AutostartState::Enabled);  // missing optional UI is ignored
    CHECK(setRunEntries(es, true, &err));                            // idempotent
    CHECK(queryRunEntries(es, &detail) == AutostartState::Enabled);

    // the same value name pointing at another file is "partial" and gets repaired by enabling
    std::vector<RunEntry> other = es;
    const stdfs::path exe2 = dir / L"other.exe";
    stdfs::copy_file(self, exe2, stdfs::copy_options::overwrite_existing);
    other[0].exePath = exe2.u8string();
    CHECK(queryRunEntries(other, &detail) == AutostartState::Partial);
    CHECK(setRunEntries(other, true, &err));
    CHECK(queryRunEntries(es, &detail) == AutostartState::Partial);
    CHECK(setRunEntries(es, true, &err));

    CHECK(setRunEntries(es, false, &err));
    CHECK(queryRunEntries(es, &detail) == AutostartState::Disabled);
    CHECK(setRunEntries(es, false, &err));  // removing twice is fine

    // a required program that does not exist: unavailable, enabling refused, nothing written
    std::vector<RunEntry> gone = {{"dofscroll-test", (dir / L"nope.exe").u8string(), false}};
    CHECK(queryRunEntries(gone, &detail) == AutostartState::Unavailable);
    CHECK(!setRunEntries(gone, true, &err));
    CHECK(queryRunEntries(es, &detail) == AutostartState::Disabled);

    setRunEntries(es, false, &err);
    tu::removeAll(dir.parent_path().generic_string());
    if (failures == 0) std::printf("autostart_win_roundtrip: ok\n");
    return failures == 0 ? 0 : 1;
}
