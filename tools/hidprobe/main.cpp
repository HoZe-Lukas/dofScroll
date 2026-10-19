// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll-hidprobe: read-only diagnostic that verifies the report layouts claimed by
// devices/*.conf against real hardware. Usage: see usage() and docs/devices.md.
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "hid_descriptor.hpp"
#include "log_sanitize.hpp"
#include "hidprobe_backend.hpp"
#include "hidprobe_core.hpp"
#ifndef _WIN32
#include "hidprobe_posix.hpp"
#endif

using namespace hidprobe;
using Clock = std::chrono::steady_clock;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

struct Options {
    bool list = false, dump = false, check = false, descriptor = false, guided = false;
    int seconds = -1;
    std::string device, devicesDir, confFile, reportPath = "hidprobe-report.txt";
};

void usage() {
    std::puts(
        "dofscroll-hidprobe - verify HID report layouts on real hardware (read-only)\n"
        "\n"
        "  --list                 list HID devices and whether a devices/*.conf entry matches\n"
        "  --dump                 hex dump of raw reports with timestamps\n"
        "  --check                decode reports with the matching description, then print a verdict\n"
        "  --descriptor           print the HID report descriptor (hex + summary)\n"
        "  --guided               guided test (push/tilt/twist, press buttons), writes a report file\n"
        "\n"
        "  --seconds N            duration of --dump (default 10) and --check (default 20); 0 = until Ctrl-C\n"
        "  --device PATH          device node, e.g. /dev/hidraw3 (default: first device with a description)\n"
        "  --devices-dir DIR      directory with *.conf descriptions (default: ./devices, then the\n"
        "                         installed share directory)\n"
        "  --conf FILE            use this single description file\n"
        "  --report FILE          output of --guided (default hidprobe-report.txt)\n"
        "\n"
        "hidprobe never writes to the device. spacenavd may keep running: hidraw can be read by\n"
        "several readers at once.");
}

bool parseArgs(int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string &dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        if (a == "--list") o.list = true;
        else if (a == "--dump") o.dump = true;
        else if (a == "--check") o.check = true;
        else if (a == "--descriptor") o.descriptor = true;
        else if (a == "--guided") o.guided = true;
        else if (a == "--seconds") {
            std::string s;
            if (!next(s)) return false;
            char *end = nullptr;
            long n = std::strtol(s.c_str(), &end, 10);
            if (*end || n < 0 || n > 86400) return false;
            o.seconds = int(n);
        } else if (a == "--device") { if (!next(o.device)) return false; }
        else if (a == "--devices-dir") { if (!next(o.devicesDir)) return false; }
        else if (a == "--conf") { if (!next(o.confFile)) return false; }
        else if (a == "--report") { if (!next(o.reportPath)) return false; }
        else return false;
    }
    return o.list || o.dump || o.check || o.descriptor || o.guided;
}

std::vector<spn::HidDescriptor> loadDescriptions(const Options &o) {
    std::vector<spn::HidDescriptor> all;
    std::vector<std::string> errors;
    if (!o.confFile.empty()) {
        spn::HidDescriptor d;
        std::string err;
        if (spn::loadHidDescriptorFile(o.confFile, d, err)) all.push_back(d);
        else std::fprintf(stderr, "%s: %s\n", o.confFile.c_str(), err.c_str());
        return all;
    }
    std::vector<std::string> dirs;
    if (!o.devicesDir.empty()) dirs.push_back(o.devicesDir);
    else dirs = {"devices", "/usr/local/share/dofscroll/devices", "/usr/share/dofscroll/devices"};
    for (const auto &d : dirs)
        if (spn::loadHidDescriptorDir(d, all, errors) && !all.empty()) break;
    for (const auto &e : errors) std::fprintf(stderr, "warning: %s\n", e.c_str());
    return all;
}

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

struct Session {
    HidBackend &be;
    DeviceInfo dev;
    const spn::HidDescriptor *desc = nullptr;
    std::vector<uint8_t> rdesc;
    bool haveRdesc = false;
    std::string rdescErr;
};

bool openStream(Session &s, std::unique_ptr<ReportStream> &st) {
    std::string err;
    bool denied = false;
    st = s.be.open(s.dev, err, denied);
    if (st) return true;
    std::fprintf(stderr, "error: %s\n", err.c_str());
    if (denied) std::fprintf(stderr, "%s\n", s.be.permissionHelp(s.dev).c_str());
    return false;
}

void printDescriptor(const Session &s) {
    if (!s.haveRdesc) {
        std::printf("Report descriptor not available: %s\n", s.rdescErr.c_str());
        return;
    }
    std::printf("Report descriptor (%zu bytes):\n%s\n\nSummary:\n", s.rdesc.size(),
                hexBytes(s.rdesc.data(), s.rdesc.size()).c_str());
    for (const auto &l : summarizeDescriptor(s.rdesc).lines) std::printf("  %s\n", l.c_str());
}

void printVerdicts(const std::vector<Verdict> &v) {
    for (const auto &x : v) std::printf("%s %s\n", statusTag(x.status), x.text.c_str());
}

int runDump(Session &s, int seconds) {
    std::unique_ptr<ReportStream> st;
    if (!openStream(s, st)) return 1;
    std::printf("Dumping reports of %s (%s). Move the cap and press buttons. Ctrl-C stops.\n",
                s.dev.path.c_str(), vidpid(s.dev.vid, s.dev.pid).c_str());
    auto t0 = Clock::now();
    size_t n = 0;
    std::vector<uint8_t> r;
    while (!g_stop && (seconds == 0 || secondsSince(t0) < seconds)) {
        ReadResult rr = st->read(100, r);
        if (rr == ReadResult::Report) {
            ++n;
            std::printf("+%9.6f id=%02x len=%-3zu : %s\n", secondsSince(t0), r.empty() ? 0 : r[0],
                        r.size(), hexBytes(r.data(), r.size()).c_str());
        } else if (rr == ReadResult::Closed || rr == ReadResult::Error) {
            std::fprintf(stderr, "device closed or read error\n");
            return 1;
        }
    }
    std::printf("%zu reports.\n", n);
    if (n == 0)
        std::puts("No reports. Move the cap. spacenavd reading the same device is fine; if still "
                  "nothing, try another /dev/hidraw node of this device (--list).");
    return 0;
}

void applyDescriptorLengths(Checker &c, const Session &s) {
    if (s.haveRdesc) c.setDescriptorLengths(summarizeDescriptor(s.rdesc).inputBytes);
}

int runCheck(Session &s, int seconds) {
    std::unique_ptr<ReportStream> st;
    if (!openStream(s, st)) return 1;
    Checker chk(*s.desc);
    applyDescriptorLengths(chk, s);
    std::printf("Checking %s against \"%s\". Move every axis to its limits and press every button.\n",
                s.dev.path.c_str(), s.desc->name.c_str());
    auto t0 = Clock::now();
    auto lastPrint = t0;
    std::vector<uint8_t> r;
    while (!g_stop && (seconds == 0 || secondsSince(t0) < seconds)) {
        ReadResult rr = st->read(100, r);
        if (rr == ReadResult::Report) {
            chk.feed(r.data(), r.size());
            if (Clock::now() - lastPrint > std::chrono::milliseconds(50)) {
                std::printf("\r%s  ", chk.liveLine().c_str());
                std::fflush(stdout);
                lastPrint = Clock::now();
            }
        } else if (rr == ReadResult::Closed || rr == ReadResult::Error) {
            std::fprintf(stderr, "\ndevice closed or read error\n");
            break;
        }
    }
    std::printf("\n\nVerdict (%zu reports):\n", chk.reportCount());
    printVerdicts(chk.verdicts());
    return 0;
}

// ---- guided mode ------------------------------------------------------------------------
struct Step {
    int axis;
    const char *text;
};

int runGuided(Session &s, const Options &o) {
    std::unique_ptr<ReportStream> st;
    if (!openStream(s, st)) return 1;
    Checker chk(*s.desc);
    applyDescriptorLengths(chk, s);
    std::ostringstream log;
    std::vector<uint8_t> r;

    auto pump = [&](int ms) {
        auto t = Clock::now();
        while (secondsSince(t) * 1000 < ms) {
            if (st->read(20, r) == ReadResult::Report) chk.feed(r.data(), r.size());
        }
    };
    double scale = 1.0;
    for (int a = 0; a < 6; ++a)
        if (s.desc->axes[a].present) { scale = s.desc->axes[a].scale; break; }
    const double threshold = 30.0 * scale;

    static const Step steps[6] = {
        {0, "Push the cap to the RIGHT and hold it there."},
        {1, "Push the cap AWAY from you (forward) and hold it."},
        {2, "Pull the cap UP (lift it) and hold it."},
        {3, "TILT the cap forward (top edge away from you) and hold it."},
        {4, "TILT the cap to the right and hold it."},
        {5, "TWIST the cap clockwise (seen from above) and hold it."},
    };
    std::puts("Guided test. Do each step when asked; press Enter to skip a step. Nothing is written to "
              "the device.");
    log << "Axis steps (expected axis is the usual 3Dconnexion naming; a different axis only means the "
           "naming differs, the report offset is what counts):\n";
    pump(300);
    for (const Step &step : steps) {
        if (g_stop) break;
        if (!s.desc->axes[step.axis].present) continue;
        std::printf("\n%s\n  waiting up to 20 s... ", step.text);
        std::fflush(stdout);
        double start[6];
        std::memcpy(start, chk.current(), sizeof start);
        double peak[6] = {0, 0, 0, 0, 0, 0};
        auto t0 = Clock::now();
        bool moved = false, skipped = false;
        Clock::time_point movedAt;
        while (!g_stop && secondsSince(t0) < 20) {
            if (st->read(20, r) == ReadResult::Report) chk.feed(r.data(), r.size());
            for (int a = 0; a < 6; ++a)
                peak[a] = std::max(peak[a], std::fabs(chk.current()[a] - start[a]));
            if (!moved && dominantAxis(start, chk.current(), threshold) >= 0) {
                moved = true;
                movedAt = Clock::now();
            }
            if (moved && Clock::now() - movedAt > std::chrono::milliseconds(1200)) break;
            if (stdinLineReady()) { skipped = true; break; }
        }
        double zero[6] = {0, 0, 0, 0, 0, 0};
        int got = moved ? dominantAxis(zero, peak, threshold) : -1;
        if (skipped) {
            std::puts("skipped");
            log << "  " << axisName(step.axis) << ": skipped\n";
        } else if (got < 0) {
            std::puts("no movement seen");
            log << "  " << axisName(step.axis) << ": NO MOVEMENT SEEN\n";
        } else {
            std::printf("moved axis %s (peak %.1f)\n", axisName(got), peak[got]);
            log << "  expected " << axisName(step.axis) << ", observed " << axisName(got) << " (peak "
                << peak[got] << ")" << (got == step.axis ? "" : "  <-- different") << "\n";
        }
        std::puts("  Release the cap.");
        pump(1200);
    }

    if (s.desc->hasButtons && !g_stop) {
        std::printf("\nNow press EVERY physical button once, one after another (this device should "
                    "have %d). Press Enter when done (max 90 s).\n",
                    s.desc->factoryButtons);
        uint32_t known = 0;
        auto t0 = Clock::now();
        while (!g_stop && secondsSince(t0) < 90) {
            if (st->read(50, r) == ReadResult::Report) chk.feed(r.data(), r.size());
            uint32_t now = chk.seenButtons() | chk.rawButtons();
            if (now != known) {
                known = now;
                std::printf("  distinct button bits seen so far: %d\n", __builtin_popcount(known));
            }
            if (stdinLineReady()) break;
        }
    }

    // report
    std::ostringstream rep;
    rep << "dofscroll-hidprobe report\n";
    rep << "device: " << spn::sanitizeForLog(s.dev.name, 256) << "\n";
    rep << "usb id: " << vidpid(s.dev.vid, s.dev.pid) << "\n";
    rep << "serial fingerprint: " << hashSerial(s.dev.serial) << " (hashed)\n";
    rep << "description: " << s.desc->name << " (factory_buttons=" << s.desc->factoryButtons << ")\n\n";
    rep << "report descriptor:\n";
    if (s.haveRdesc) {
        rep << hexBytes(s.rdesc.data(), s.rdesc.size()) << "\n";
        for (const auto &l : summarizeDescriptor(s.rdesc).lines) rep << "  " << l << "\n";
    } else {
        rep << "  not available: " << s.rdescErr << "\n";
    }
    rep << "\n" << log.str() << "\nobserved layout and mismatches:\n";
    for (const auto &v : chk.verdicts()) rep << statusTag(v.status) << " " << v.text << "\n";
    const char *home = std::getenv("HOME");
    const char *user = std::getenv("USER");
    std::string text = scrubReport(rep.str(), home ? home : "", user ? user : "");
    std::ofstream f(o.reportPath);
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", o.reportPath.c_str());
        return 1;
    }
    f << text;
    std::printf("\n%s\nReport written to %s. It contains no home path, user name or serial number; "
                "please read it once before you paste it into an issue.\n",
                text.c_str(), o.reportPath.c_str());
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) {
        usage();
        return 2;
    }
    std::signal(SIGINT, onSignal);
    std::unique_ptr<HidBackend> be = makeBackend();
    std::vector<DeviceInfo> devs = be->list();
    std::vector<spn::HidDescriptor> descs = loadDescriptions(o);

    if (o.list) {
        if (devs.empty())
            std::puts("No HID devices found (is /dev/hidraw* present?).");
        for (const auto &d : devs) {
            const spn::HidDescriptor *m = findDescriptor(descs, d.vid, d.pid);
            std::printf("%-14s %s  %s  [%s]\n", d.path.c_str(), vidpid(d.vid, d.pid).c_str(),
                        d.name.c_str(),
                        m ? ("matches \"" + m->name + "\"").c_str() : "no devices/*.conf entry");
        }
        if (!(o.dump || o.check || o.descriptor || o.guided)) return 0;
    }

    // pick the device
    Session s{*be, {}, nullptr, {}, false, {}};
    bool found = false;
    for (const auto &d : devs) {
        if (!o.device.empty() ? d.path == o.device : findDescriptor(descs, d.vid, d.pid) != nullptr) {
            s.dev = d;
            found = true;
            break;
        }
    }
    if (!found && o.device.empty() && devs.size() == 1) {
        s.dev = devs[0];
        found = true;
    }
    if (!found) {
        std::fprintf(stderr, "error: no device selected. Run --list and pass --device PATH.\n");
        return 1;
    }
    s.desc = findDescriptor(descs, s.dev.vid, s.dev.pid);
    if (!s.desc && !descs.empty() && !o.confFile.empty()) s.desc = &descs[0];
    if ((o.check || o.guided) && !s.desc) {
        std::fprintf(stderr,
                     "error: no description for %s. Pass --conf FILE or --devices-dir DIR.\n",
                     vidpid(s.dev.vid, s.dev.pid).c_str());
        return 1;
    }
    s.haveRdesc = be->readDescriptor(s.dev, s.rdesc, s.rdescErr);
    if (!s.haveRdesc && s.rdescErr.find("Permission denied") != std::string::npos)
        std::fprintf(stderr, "error: %s\n%s\n", s.rdescErr.c_str(), be->permissionHelp(s.dev).c_str());

    int rc = 0;
    if (o.descriptor) printDescriptor(s);
    if (o.dump && rc == 0) rc = runDump(s, o.seconds < 0 ? 10 : o.seconds);
    if (o.check && rc == 0) rc = runCheck(s, o.seconds < 0 ? 20 : o.seconds);
    if (o.guided && rc == 0) rc = runGuided(s, o);
    return rc;
}
