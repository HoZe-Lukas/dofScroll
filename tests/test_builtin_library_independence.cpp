// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Proves that the tests do not depend on the number or the ids of the embedded example files.
// The embedded set is fixed at configure time, so this test copies the examples folder, adds a new
// joystick mode file and a new button mode file, and feeds that library to the loaders through
// their library parameter. The counts the helpers of builtin_counts.hpp derive must follow, the
// loaders must agree with them, and the id-ordering rules the cycle tests rely on ("0-first" sorts
// before and "zzzz-last" after every built-in) must still hold with the extra files.
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "builtin_counts.hpp"
#include "button_modes.hpp"
#include "modes.hpp"
#include "testutil.hpp"

using namespace spn;
namespace fsys = std::filesystem;

namespace {

// A library list read from a directory of *.conf files; owns the texts.
struct DirLibrary {
    std::vector<std::string> ids, texts;
    std::vector<BuiltinFile> list;
    explicit DirLibrary(const std::string &dir) {
        for (const auto &de : fsys::directory_iterator(dir))
            if (de.path().extension() == ".conf") {
                std::ifstream in(de.path(), std::ios::binary);
                ids.push_back(de.path().stem().string());
                texts.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
        std::vector<size_t> order(ids.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ids[a] < ids[b]; });
        for (size_t i : order) list.push_back({ids[i].c_str(), texts[i].c_str()});
        list.push_back({nullptr, nullptr});
    }
    const BuiltinFile *get() const { return list.data(); }
};

void put(const std::string &p, const std::string &text) { std::ofstream(p, std::ios::binary) << text; }

}  // namespace

int main() {
    const fsys::path examples = fsys::path(__FILE__).parent_path() / ".." / "examples";
    const std::string root = tu::makeTempDir("dofscroll-builtin-indep");
    fsys::create_directories(root + "/modes");
    fsys::create_directories(root + "/buttons");
    for (const char *kind : {"modes", "buttons"})
        for (const auto &de : fsys::directory_iterator(examples / kind))
            if (de.path().extension() == ".conf") fsys::copy_file(de.path(), fsys::path(root) / kind / de.path().filename());

    // the copy is the embedded library, so the counts agree with the real one
    {
        const DirLibrary m(root + "/modes"), b(root + "/buttons");
        assert(tu::fileCount(m.get()) == tu::fileCount(builtinModeFiles()));
        assert(tu::fileCount(b.get()) == tu::fileCount(builtinButtonModeFiles()));
        assert(tu::builtinModeCount(m.get()) == tu::builtinModeCount());
        assert(tu::builtinButtonModeCount(b.get()) == tu::builtinButtonModeCount());
        assert(loadModes("", nullptr, m.get()).modes.size() == tu::builtinModeCount());
        assert(loadButtonModes("", nullptr, b.get()).modes.size() == tu::builtinButtonModeCount());
    }

    // add one new file of each kind: the derived counts grow by exactly one
    put(root + "/modes/zz-extra.conf", "NAME = Extra\nDESCRIPTION = Added by the test\nV_ACTION = none\n");
    put(root + "/buttons/zz-extra.conf", "NAME = Extra\nACTION = click\n");
    const DirLibrary m(root + "/modes"), b(root + "/buttons");
    assert(tu::builtinModeCount(m.get()) == tu::builtinModeCount() + 1);
    assert(tu::builtinButtonModeCount(b.get()) == tu::builtinButtonModeCount() + 1);

    std::vector<std::string> warn;
    const ModeSet more = loadModes("", &warn, m.get());
    const ModeSet embedded = loadModes("", nullptr);
    assert(warn.empty() && more.modes.size() == embedded.modes.size() + 1);
    assert(more.modes.size() == tu::builtinModeCount(m.get()));
    const ModeDef *extra = more.find("zz-extra");
    assert(extra && extra->builtin && extra->label == "Extra" && extra->v.type == ChannelActionType::Off);
    assert(!embedded.find("zz-extra"));  // the embedded library itself was not touched
    for (const ModeDef &e : embedded.modes) assert(more.find(e.id) && more.find(e.id)->builtin);

    const ButtonModeSet moreB = loadButtonModes("", &warn, b.get());
    assert(warn.empty() && moreB.modes.size() == loadButtonModes("", nullptr).modes.size() + 1);
    assert(moreB.modes.size() == tu::builtinButtonModeCount(b.get()));
    assert(moreB.find("zz-extra") && moreB.find("zz-extra")->builtin && moreB.find("zz-extra")->type == ButtonActionType::Click);

    // a file named like a code-defined mode in the library does not add a mode (the code wins)
    {
        const std::string dir = root + "/codewins";
        fsys::create_directories(dir);
        put(dir + "/normal.conf", "NAME = Not normal\nV_ACTION = none\n");
        const DirLibrary c(dir);
        assert(tu::builtinModeCount(c.get()) == tu::codeDefinedModeCount());
        const ModeSet cs = loadModes("", nullptr, c.get());
        assert(cs.modes.size() == tu::codeDefinedModeCount() && cs.normal().label == "Normal");
    }

    // user files against the extended library, as the cycle and catalog tests use them
    fsys::create_directories(root + "/user");
    put(root + "/user/0-first.conf", "NAME = First\n");
    put(root + "/user/zzzz-last.conf", "NAME = Last\n");
    put(root + "/user/zz-extra.conf", "NAME = Mine\n");  // replaces the added built-in: no new mode
    const ModeSet us = loadModes(root + "/user", &warn, m.get());
    assert(warn.empty() && us.modes.size() == tu::builtinModeCount(m.get()) + 2);
    assert(us.find("zz-extra") && !us.find("zz-extra")->builtin && us.find("zz-extra")->label == "Mine");
    const std::vector<std::string> ids = tu::sortedIds(us);
    assert(ids.front() == "0-first" && ids.back() == "zzzz-last");
    // the ids between them are exactly the built-ins, in sorted order, whatever their number
    std::vector<std::string> middle(ids.begin() + 1, ids.end() - 1);
    std::vector<std::string> expect = tu::sortedIds(loadModes("", nullptr, m.get()));
    assert(middle == expect);

    // a user file with exactly the added built-in's text is that built-in, not a second mode
    fsys::create_directories(root + "/same");
    put(root + "/same/zz-extra.conf", "NAME = Extra\nDESCRIPTION = Added by the test\nV_ACTION = none\n");
    const ModeSet same = loadModes(root + "/same", nullptr, m.get());
    assert(same.modes.size() == tu::builtinModeCount(m.get()) && same.find("zz-extra")->builtin);
    fsys::create_directories(root + "/sameb");
    put(root + "/sameb/zz-extra.conf", "NAME = Extra\nACTION = click\n");
    const ButtonModeSet sameB = loadButtonModes(root + "/sameb", nullptr, b.get());
    assert(sameB.modes.size() == tu::builtinButtonModeCount(b.get()) && sameB.find("zz-extra")->builtin);

    tu::removeAll(root);
    std::puts("test_builtin_library_independence OK");
    return 0;
}
