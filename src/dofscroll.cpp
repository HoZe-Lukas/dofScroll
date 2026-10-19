// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll 2.0: Daemon, der eine 3Dconnexion-SpaceMouse (via spacenavd/
// libspnav) in Scroll-, Zoom-, Pfeiltasten- und Maustasten-Events uebersetzt.
// Ersetzt spacenav_bridge.cpp funktional, nutzt aber die gemeinsamen Module:
//   config.hpp    - Settings/Profile ([global], [device:*], [app:*])
//   dsp.hpp       - Kanal-Signalverarbeitung, Schritt-Akkumulator, Zoom-Erkennung
//   output.hpp    - Ausgabe ueber X11/XTest oder uinput
//   activewin.hpp - aktives Programm (WM_CLASS) fuer [app:*]-Profile
//
// Threads: ein Reader-Thread blockiert auf spnav_wait_event() und schreibt nur
// den jeweils aktuellsten Zustand (lock-free Atomics) weg; der Haupt-Thread
// tickt mit Settings.tick_hz, wertet aus und sendet gleichmaessig verteilte
// Schritte (statt Bursts pro Event). Config/Profile gehoeren allein dem
// Haupt-Thread.
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <streambuf>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <X11/Xlib.h>
#include <spnav.h>

#include "activewin.hpp"
#include "common.hpp"
#include "config.hpp"
#include "device_count.hpp"
#include "dsp.hpp"
#include "log_sanitize.hpp"
#include "output.hpp"
#include "profile_match.hpp"

using namespace spn;

namespace {

const char *const VERSION = "dofscroll 2.0";
const char *const LOG = "[dofscroll] ";

// --debug: Button-Presses nur dann loggen (vom Daemon gesetzt, bevor der Reader startet).
std::atomic<bool> g_debug{false};

// Von Signal-Handlern gesetzt (deshalb sig_atomic_t statt bool). Der Haupt-Loop
// prueft die Flags pro Tick und handelt selbst - die Handler bleiben minimal.
volatile std::sig_atomic_t g_reload_requested = 0;
volatile std::sig_atomic_t g_quit_requested = 0;

void handleReloadSignal(int) { g_reload_requested = 1; }
void handleQuitSignal(int) { g_quit_requested = 1; }

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Obergrenze fuer dt: nach Suspend/Stall wuerde sonst ein riesiger
// Akkumulator auf einmal als Burst entladen.
constexpr double MAX_DT = 0.05;
// "Frisch genug": deutlich ueber der nominellen ~8 ms HID-Poll-Periode, aber
// kurz genug, dass ein Verbindungsabbruch waehrend einer Auslenkung praktisch
// sofort endet (statt ewig in die letzte Richtung weiterzuscrollen).
constexpr int64_t STALE_NS = 250'000'000;  // 250 ms
// Abfrage des aktiven Fensters (~10x/s, nicht jeden Tick).
constexpr int64_t ACTIVEWIN_POLL_NS = 100'000'000;
// mtime-Pruefung der Config-Datei fuer Auto-Reload.
constexpr int64_t CONFIG_POLL_NS = 1'000'000'000;
// Heartbeat-Datei des Trays (Ausgabe pausieren) ~20x/s pruefen.
constexpr int64_t SUSPEND_POLL_NS = 50'000'000;
// --debug-Ausgabe ~10x/s.
constexpr int64_t DEBUG_PERIOD_NS = 100'000'000;
// Angesteckte 3Dconnexion-Geraete neu zaehlen (zusaetzlich bei jedem (Re-)Connect).
constexpr int64_t DEVCOUNT_POLL_NS = 10'000'000'000;
// Status-Datei: unveraendert hoechstens so oft neu schreiben (Zeitstempel "updated").
constexpr int64_t STATUS_REFRESH_NS = 1'000'000'000;

// Pausiert der Tray die Ausgabe gerade (frische Heartbeat-Datei)?
// Vertrauensprinzip: Datei muss dem effektiven Benutzer gehoeren und darf fuer
// Gruppe/Andere nicht zugreifbar sein; XDG_RUNTIME_DIR selbst muss dem
// effektiven Benutzer gehoeren und darf nicht group/other-schreibbar sein
// (einmal geprueft, gecacht) - sonst wird der Mechanismus ignoriert.
bool suspendRequested() {
    static const std::string path = suspendFilePath();
    if (path.empty()) return false;
    static const bool dir_ok = [] {
        const char *d = std::getenv("XDG_RUNTIME_DIR");
        struct stat ds;
        if (!d || stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode)) return false;
        return ds.st_uid == geteuid() && (ds.st_mode & 022) == 0;
    }();
    if (!dir_ok) return false;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) return false;
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    long age_ms = (now.tv_sec - st.st_mtim.tv_sec) * 1000L +
                  (now.tv_nsec - st.st_mtim.tv_nsec) / 1000000L;
    return age_ms >= -SUSPEND_MAX_AGE_MS && age_ms <= SUSPEND_MAX_AGE_MS;
}

// XDG_RUNTIME_DIR vertrauenswuerdig? (dem effektiven Benutzer gehoerig, nicht
// group/other-schreibbar) - Voraussetzung fuer Status- und Log-Datei.
bool runtimeDirTrusted() {
    static const bool ok = [] {
        const char *d = std::getenv("XDG_RUNTIME_DIR");
        struct stat ds;
        if (!d || !*d || stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode)) return false;
        return ds.st_uid == geteuid() && (ds.st_mode & 022) == 0;
    }();
    return ok;
}

// Spiegelt alles, was nach std::cerr geht (stderr/Journal bleibt unveraendert), zusaetzlich
// in die Log-Datei im Runtime-Dir (fuer die Anzeige im Tray). Geschrieben wird beim Flush
// (std::endl) unter einem Mutex, weil auch der Reader-Thread loggt. O_APPEND|O_NOFOLLOW,
// 0600; Besitzer/Typ werden geprueft. Rollender Puffer: wird max_lines (Normalbetrieb
// kLogMaxLines = 300, --debug kLogMaxLinesDebug) oder kLogMaxBytes ueberschritten, bleiben
// nur die neuesten keep_lines Zeilen (Datei wird atomar neu geschrieben). Jeder Fehler schaltet nur das Spiegeln ab; der Haupt-Thread
// blockiert nie (regulaere Datei auf tmpfs, O_NONBLOCK).
class TeeLogBuf : public std::streambuf {
public:
    TeeLogBuf(std::streambuf *orig, size_t max_lines, size_t keep_lines)
        : orig_(orig), max_lines_(max_lines), keep_lines_(keep_lines) {
        const std::string path = logFilePath();
        if (path.empty() || !runtimeDirTrusted()) return;
        int fd = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (fd < 0) return;
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 077) != 0) {
            ::close(fd);
            return;
        }
        fd_ = fd;
        size_ = (size_t)st.st_size;
        path_ = path;
        loadExisting();  // Neustart (Restart=always) soll das Log nicht verlieren
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        std::streamsize r = orig_->sputn(s, n);
        std::lock_guard<std::mutex> lk(m_);
        if (fd_ >= 0 && pending_.size() < 64 * 1024) {
            // Zeitstempel (lokale Zeit) nur in der Datei, am Anfang jeder Zeile.
            for (std::streamsize i = 0; i < n; ++i) {
                if (at_line_start_) {
                    char ts[32];
                    std::time_t t = std::time(nullptr);
                    std::tm tmv{};
                    localtime_r(&t, &tmv);
                    if (std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S ", &tmv) > 0) pending_ += ts;
                    at_line_start_ = false;
                }
                pending_ += s[i];
                if (s[i] == '\n') at_line_start_ = true;
            }
        }
        return r;
    }
    int sync() override {
        int r = orig_->pubsync();
        std::lock_guard<std::mutex> lk(m_);
        flushPending();
        return r;
    }

private:
    std::streambuf *orig_;
    size_t max_lines_, keep_lines_;
    std::mutex m_;
    int fd_ = -1;
    size_t size_ = 0;
    std::string pending_;
    std::string path_;
    std::deque<std::string> lines_;  // Spiegel des Dateiinhalts (ohne \n)
    std::string partial_;            // noch nicht abgeschlossene Zeile
    bool at_line_start_ = true;      // naechstes Zeichen beginnt eine Zeile (Zeitstempel)

    void loadExisting() {
        if (size_ == 0 || size_ > kLogMaxBytes) {
            if (size_ > kLogMaxBytes) rewrite();  // ueberlang (alte Version): leeren
            return;
        }
        int rfd = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (rfd < 0) return;
        std::string data(size_, '\0');
        ssize_t n = ::pread(rfd, &data[0], data.size(), 0);
        ::close(rfd);
        if (n <= 0) return;
        data.resize((size_t)n);
        size_t pos = 0;
        while (pos < data.size()) {
            size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) break;
            lines_.push_back(data.substr(pos, nl - pos));
            pos = nl + 1;
        }
        if (lines_.size() > max_lines_) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }

    // Schreibt lines_ atomar (temp + rename) neu und oeffnet den Anhaenge-Deskriptor erneut.
    void rewrite() {
        const std::string tmp = path_ + ".tmp";
        ::unlink(tmp.c_str());
        int tfd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (tfd < 0) { disable(); return; }
        std::string out;
        for (const std::string &l : lines_) { out += l; out += '\n'; }
        const char *p = out.data();
        size_t left = out.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(tfd, p, left);
            if (w <= 0) { if (w < 0 && errno == EINTR) continue; ok = false; break; }
            p += w;
            left -= (size_t)w;
        }
        ::close(tfd);
        if (!ok || ::rename(tmp.c_str(), path_.c_str()) != 0) {
            ::unlink(tmp.c_str());
            disable();
            return;
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd_ < 0) { disable(); return; }
        size_ = out.size();
    }

    void flushPending() {
        if (fd_ < 0 || pending_.empty()) {
            pending_.clear();
            return;
        }
        const char *p = pending_.data();
        size_t left = pending_.size();
        while (left > 0) {
            ssize_t w = ::write(fd_, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                disable();
                return;
            }
            p += w;
            left -= (size_t)w;
            size_ += (size_t)w;
        }
        // Zeilen mitzaehlen; bei Ueberlauf auf die neuesten keep_lines_ kuerzen.
        partial_ += pending_;
        pending_.clear();
        size_t pos = 0, nl;
        while ((nl = partial_.find('\n', pos)) != std::string::npos) {
            lines_.push_back(partial_.substr(pos, nl - pos));
            pos = nl + 1;
        }
        partial_.erase(0, pos);
        if (partial_.size() > 8192) partial_.clear();  // Schutz, nie eine endlose Zeile
        if (lines_.size() > max_lines_ || size_ > kLogMaxBytes) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }
    void disable() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        pending_.clear();
    }
};

// Schreibt die Status-Datei (Format: siehe common.hpp) atomar: Temp-Datei im selben
// Verzeichnis (O_EXCL|O_NOFOLLOW, 0600) + rename. Fehler sind nie fatal, es wird einmal
// geloggt. Ohne XDG_RUNTIME_DIR passiert nichts.
class StatusWriter {
public:
    StatusWriter() : path_(statusFilePath()) {}

    // body = Zeilen ohne "updated"; geschrieben wird bei Aenderung bzw. nach STATUS_REFRESH_NS.
    void update(const std::string &body, int64_t now_ns) {
        if (path_.empty() || !runtimeDirTrusted()) return;
        if (body == last_body_ && now_ns < next_refresh_ns_) return;
        last_body_ = body;
        next_refresh_ns_ = now_ns + STATUS_REFRESH_NS;
        std::string text = body + "updated=" + std::to_string((long long)std::time(nullptr)) + "\n";
        if (text.size() > kStatusMaxBytes) return;
        if (!write(text) && !warned_) {
            warned_ = true;
            std::cerr << LOG << "WARNUNG: Status-Datei nicht schreibbar (" << std::strerror(errno)
                      << ") - Anzeige im Tray eingeschraenkt" << std::endl;
        }
    }

    void remove() {
        if (!path_.empty() && wrote_) ::unlink(path_.c_str());
    }

private:
    std::string path_, last_body_;
    int64_t next_refresh_ns_ = 0;
    bool warned_ = false, wrote_ = false;

    bool write(const std::string &text) {
        const std::string tmp = path_ + ".tmp." + std::to_string((long long)getpid());
        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno == EEXIST) {  // Rest eines frueheren Laufs mit gleicher PID
            ::unlink(tmp.c_str());
            fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        }
        if (fd < 0) return false;
        const char *p = text.data();
        size_t left = text.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(fd, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                ok = false;
                break;
            }
            p += w;
            left -= (size_t)w;
        }
        int e = errno;
        if (::close(fd) != 0 && ok) { ok = false; e = errno; }
        if (ok && ::rename(tmp.c_str(), path_.c_str()) != 0) { ok = false; e = errno; }
        if (!ok) {
            ::unlink(tmp.c_str());
            errno = e;
            return false;
        }
        wrote_ = true;
        return true;
    }
};

std::string describeMatches(const ProfileSet &ps, const std::string &device,
                            const std::string &wm_class) {
    std::string d = "global";
    for (const Section &s : ps.sections) {
        if (s.kind == Section::DEVICE && patternMatches(s.pattern, device))
            d += " + device:" + s.pattern;
    }
    for (const Section &s : ps.sections) {
        if (s.kind == Section::APP && patternMatches(s.pattern, wm_class))
            d += " + app:" + s.pattern;
    }
    return d;
}

// Identitaet der Config-Datei fuer die Auto-Reload-Erkennung. Der Tray
// schreibt atomar (temp + rename) -> auch Inode/Groesse vergleichen.
struct FileStamp {
    bool exists = false;
    int64_t mtime_ns = 0;
    int64_t size = 0;
    uint64_t ino = 0;
    bool operator!=(const FileStamp &o) const {
        return exists != o.exists || mtime_ns != o.mtime_ns || size != o.size || ino != o.ino;
    }
};

FileStamp statFile(const std::string &path) {
    FileStamp f;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        f.exists = true;
        f.mtime_ns = (int64_t)st.st_mtim.tv_sec * 1'000'000'000 + st.st_mtim.tv_nsec;
        f.size = (int64_t)st.st_size;
        f.ino = (uint64_t)st.st_ino;
    }
    return f;
}

bool buttonHeld(uint32_t mask, int bnum) {
    return bnum >= 0 && bnum < 32 && (mask & (1u << bnum)) != 0;
}

// ---------------------------------------------------------------------------
// Reader-Thread: Zustand der SpaceMouse, lock-free zum Haupt-Thread.
// ---------------------------------------------------------------------------
class DeviceReader {
public:
    std::array<std::atomic<int>, AX_COUNT> axes{};
    // Bitmaske gedrueckter Buttons (Bit n = spnav_event_button.bnum n).
    std::atomic<uint32_t> button_mask{0};
    // Zeitstempel (steady_clock, ns) des letzten Motion-Events -> Stale-Watchdog.
    std::atomic<int64_t> last_motion_ns{0};
    // Tastenzahl des aktuellen Geraets (fuer Warnungen im Haupt-Thread).
    std::atomic<int> dev_buttons{0};
    // Wird nach jedem (Re-)Connect erhoeht, nachdem der Name unter dem Mutex
    // gesetzt wurde. Der Haupt-Thread vergleicht pro Tick nur diese Zahl
    // (billig) und holt den Namen nur bei Aenderung unter dem Mutex.
    std::atomic<uint64_t> dev_generation{0};

    DeviceReader() {
        for (auto &a : axes) a.store(0, std::memory_order_relaxed);
    }

    std::string deviceName() const {
        std::lock_guard<std::mutex> lk(name_mutex_);
        return dev_name_;
    }

    void start() {
        std::thread t(&DeviceReader::loop, this);
        // Detached: spnav_wait_event() blockiert ohne Abbruchmoeglichkeit; beim
        // Beenden raeumt der Prozess-Exit den Thread mit ab.
        t.detach();
    }

private:
    mutable std::mutex name_mutex_;
    std::string dev_name_;

    // Gibt Geraetename/Tasten/Achsen aus (Compact: 2 Tasten, Pro: 15) und
    // reicht den Namen an den Haupt-Thread weiter (Profil [device:*]). Nach
    // Hotplug kann sich das Geraet aendern -> bei jedem (Re-)Connect.
    void publishDeviceInfo() {
        char name[256] = "";
        if (spnav_dev_name(name, sizeof(name)) < 0) name[0] = '\0';
        name[sizeof(name) - 1] = '\0';
        int buttons = spnav_dev_buttons();
        std::cerr << LOG << "Geraet: " << (name[0] ? sanitizeForLog(name) : std::string("?")) << " (" << buttons
                  << " Tasten, " << spnav_dev_axes() << " Achsen)" << std::endl;
        dev_buttons.store(buttons, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(name_mutex_);
            dev_name_ = name;
        }
        dev_generation.fetch_add(1, std::memory_order_release);
    }

    // Backoff-Verzoegerung, ueber Aufrufe hinweg erhalten: wird nur nach einer
    // Verbindung zurueckgesetzt, die Events geliefert hat oder lange stand.
    static constexpr int MIN_DELAY_MS = 500;
    static constexpr int MAX_DELAY_MS = 5000;
    static constexpr int64_t HEALTHY_NS = 10'000'000'000;  // 10 s
    int delay_ms_ = MIN_DELAY_MS;

    void backoffSleep(const char *why) {
        std::cerr << LOG << why << ", naechster Versuch in " << (delay_ms_ / 1000.0) << "s"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        delay_ms_ = std::min(delay_ms_ * 2, MAX_DELAY_MS);
    }

    // Verbindet sich mit spacenavd, exponentieller Backoff bis 5 s. Dadurch
    // startet der Dienst sauber, auch wenn spacenavd (noch) nicht laeuft, und
    // erholt sich von selbst nach einem spacenavd-Neustart.
    void connectWithRetry() {
        while (spnav_open() == -1) backoffSleep("spacenavd nicht erreichbar");
        std::cerr << LOG << "mit spacenavd verbunden" << std::endl;
        publishDeviceInfo();
    }

    void clearState() {
        for (auto &a : axes) a.store(0, std::memory_order_relaxed);
        button_mask.store(0, std::memory_order_relaxed);
    }

    // Blockiert auf spnav-Events und schreibt nur den aktuellsten Zustand weg.
    // Kein Versand hier, damit unregelmaessiges Event-Timing den Fluss nicht
    // beeinflusst.
    void loop() {
        while (true) {
            connectWithRetry();

            const int64_t connected_ns = nowNs();
            bool got_event = false;
            spnav_event sev;
            while (spnav_wait_event(&sev)) {
                got_event = true;
                if (sev.type == SPNAV_EVENT_MOTION) {
                    axes[AX_X].store(sev.motion.x, std::memory_order_relaxed);
                    axes[AX_Y].store(sev.motion.y, std::memory_order_relaxed);
                    axes[AX_Z].store(sev.motion.z, std::memory_order_relaxed);
                    axes[AX_RX].store(sev.motion.rx, std::memory_order_relaxed);
                    axes[AX_RY].store(sev.motion.ry, std::memory_order_relaxed);
                    axes[AX_RZ].store(sev.motion.rz, std::memory_order_relaxed);
                    last_motion_ns.store(nowNs(), std::memory_order_release);
                } else if (sev.type == SPNAV_EVENT_BUTTON) {
                    if (sev.button.press && g_debug.load(std::memory_order_relaxed))
                        std::cerr << LOG << "Button " << sev.button.bnum << " gedrueckt" << std::endl;
                    if (sev.button.bnum >= 0 && sev.button.bnum < 32) {
                        uint32_t bit = 1u << sev.button.bnum;
                        if (sev.button.press) button_mask.fetch_or(bit, std::memory_order_relaxed);
                        else button_mask.fetch_and(~bit, std::memory_order_relaxed);
                    }
                } else if (sev.type == SPNAV_EVENT_DEV && sev.dev.op == SPNAV_DEV_RM) {
                    // Geraet abgezogen: Release-Events kommen dann nie mehr -
                    // gehaltene Tasten/Achsen sonst haengend (Klick bliebe gedrueckt).
                    // Bewusst kein Heuristik-Timeout: eine gehaltene Taste ohne
                    // Bewegung ist legitim (Klicken-und-Halten).
                    std::cerr << LOG << "Geraet entfernt" << std::endl;
                    clearState();
                }
            }

            spnav_close();
            clearState();
            if (got_event || nowNs() - connected_ns >= HEALTHY_NS) delay_ms_ = MIN_DELAY_MS;
            backoffSleep("Verbindung zu spacenavd verloren");
        }
    }
};

// ---------------------------------------------------------------------------
// Haupt-Thread: Profile, Auswertung, Ausgabe.
// ---------------------------------------------------------------------------
class Daemon {
public:
    explicit Daemon(bool debug) : debug_(debug) { g_debug.store(debug, std::memory_order_relaxed); }

    bool initialize() {
        {
            // Best-effort: Legacy-Config in den eigenen Ordner uebernehmen (kann in
            // der read-only Sandbox des Daemons scheitern - dann egal).
            std::string mlog;
            migrateLegacyConfig(&mlog);
            if (!mlog.empty()) {
                std::cerr << LOG << mlog;
                if (mlog.back() != '\n') std::cerr << std::endl;
            }
        }
        config_path_ = defaultConfigPath();
        loadConfig();
        // Erste Aufloesung ohne Geraet/Programm, damit BACKEND/TICK_HZ aus
        // [global] fuer den Start feststehen.
        settings_ = profiles_.resolve("", "");
        match_desc_ = describeMatches(profiles_, "", "");

        std::string log;
        output_ = makeOutput(settings_.backend, &log);
        if (!log.empty()) {
            std::cerr << LOG << log;
            if (log.back() != '\n') std::cerr << std::endl;
        }
        if (!output_) {
            std::cerr << LOG << "FEHLER: keine Ausgabe moeglich (BACKEND=" << settings_.backend
                      << ")" << std::endl;
            return false;
        }
        backend_cfg_ = settings_.backend;
        std::cerr << LOG << "Ausgabe ueber " << output_->name() << std::endl;
        // Saubere Ausgangslage: ein vorheriger Prozess (Absturz, SIGKILL) kann Strg
        // oder die Maustaste per XTest gedrueckt hinterlassen haben.
        output_->forceReleaseAll();
        output_->flush();

        if (!activewin_.init())
            std::cerr << LOG << "Aktives Fenster nicht ermittelbar - [app:*]-Profile inaktiv"
                      << std::endl;

        std::signal(SIGHUP, handleReloadSignal);
        std::signal(SIGTERM, handleQuitSignal);
        std::signal(SIGINT, handleQuitSignal);
        return true;
    }

    void run() {
        reader_.start();

        auto tick_period = periodFor(settings_.tick_hz);
        auto next_tick = std::chrono::steady_clock::now();
        auto last_tick = next_tick;
        int64_t next_win_poll = 0, next_cfg_poll = 0, next_debug = 0, next_susp_poll = 0;
        bool suspended = false;
        uint64_t seen_generation = 0;
        // Zustand des linken Mausbuttons (Klick-Button); bleibt ueber Profil-
        // Neuaufloesungen hinweg erhalten, damit ein gehaltener Drag nicht bricht.
        bool prev_click_active = false;
        bool prev_enabled = true;  // fuer das einmalige Log je ENABLED-Wechsel
        int64_t next_devcount = 0;
        int devices_seen = 0;
        std::string hint_key;      // Mehrgeraete-Hinweis: einmal pro Aenderung

        std::cerr << LOG << "Profil: " << sanitizeForLog(match_desc_) << " (Tickrate " << settings_.tick_hz
                  << " Hz)" << std::endl;

        while (!g_quit_requested) {
            next_tick += tick_period;
            std::this_thread::sleep_until(next_tick);
            if (g_quit_requested) break;

            const int64_t now_ns = nowNs();

            // --- Profil-Eingaben: Config, Geraet, aktives Programm ---------
            bool need_resolve = false;
            if (g_reload_requested) {
                g_reload_requested = 0;
                std::cerr << LOG << "SIGHUP: lade Config neu" << std::endl;
                loadConfig();
                need_resolve = true;
                next_cfg_poll = now_ns + CONFIG_POLL_NS;
            } else if (now_ns >= next_cfg_poll) {
                next_cfg_poll = now_ns + CONFIG_POLL_NS;
                // Pfad neu bewerten: z.B. hat der Tray die Datei inzwischen in den
                // neuen Ordner migriert, waehrend wir noch am Legacy-Pfad hingen.
                const std::string new_path = defaultConfigPath();
                if (new_path != config_path_) {
                    config_path_ = new_path;
                    std::cerr << LOG << "Config-Pfad gewechselt: " << sanitizeForLog(config_path_)
                              << std::endl;
                    loadConfig();  // setzt den Stempel neu
                    need_resolve = true;
                } else if (statFile(config_path_) != stamp_) {
                    std::cerr << LOG << "Config-Datei geaendert, lade neu" << std::endl;
                    loadConfig();
                    need_resolve = true;
                }
            }

            uint64_t gen = reader_.dev_generation.load(std::memory_order_acquire);
            if (gen != seen_generation) {
                seen_generation = gen;
                next_devcount = 0;  // nach (Re-)Connect sofort neu zaehlen
                std::string name = reader_.deviceName();
                warn_buttons_ = true;
                if (name != device_) {
                    device_ = name;
                    need_resolve = true;
                } else {
                    checkButtons();
                }
            }

            if (now_ns >= next_win_poll) {
                next_win_poll = now_ns + ACTIVEWIN_POLL_NS;
                std::string wc = activewin_.poll();
                if (wc != wm_class_) {
                    wm_class_ = wc;
                    need_resolve = true;
                }
            }

            if (need_resolve) {
                applyProfile();
                tick_period = periodFor(settings_.tick_hz);
            }

            // Angesteckte 3Dconnexion-Geraete zaehlen (billig: alle ~10 s bzw. nach Connect).
            // spacenavd meldet bei mehreren nur eines -> einmaliger Hinweis je Aenderung.
            if (now_ns >= next_devcount) {
                next_devcount = now_ns + DEVCOUNT_POLL_NS;
                int c = countSpaceMice();
                devices_seen = c < 0 ? 0 : c;
                std::string key = std::to_string(devices_seen) + "|" + device_;
                if (devices_seen >= 2 && key != hint_key) {
                    std::string dn = device_.empty() ? "?" : sanitizeForLog(device_);
                    std::cerr << LOG << "Hinweis: " << devices_seen
                              << " SpaceMice angesteckt, spacenavd meldet nur '" << dn
                              << "' - Profil [device:" << dn
                              << "] gilt fuer alle; Tasten beider Geraete teilen sich die Indizes"
                              << std::endl;
                }
                hint_key = devices_seen >= 2 ? key : std::string();
            }

            // --- Zeitbasis -------------------------------------------------
            auto now = std::chrono::steady_clock::now();
            double dt = std::min(std::chrono::duration<double>(now - last_tick).count(), MAX_DT);
            // Drift-Schutz: liegen wir weit hinter dem Plan (Suspend, Stall),
            // nicht alle verpassten Ticks nachholen.
            if (now > next_tick + std::chrono::milliseconds(100)) next_tick = now;
            last_tick = now;

            // --- Eingaben lesen --------------------------------------------
            bool fresh = (nowNs() - reader_.last_motion_ns.load(std::memory_order_acquire)) <= STALE_NS;
            RawAxes raw{};
            if (fresh) {
                for (int i = 0; i < AX_COUNT; ++i)
                    raw[i] = reader_.axes[i].load(std::memory_order_relaxed);
            }
            uint32_t buttons = reader_.button_mask.load(std::memory_order_relaxed);

            // Tray wertet die Maus selbst aus (Reiter "Achsen"/Assistent): Eingaben
            // wie "keine Auslenkung, keine Taste" behandeln - gehaltene Tasten
            // (Klick, Strg) werden dadurch sauber losgelassen.
            if (now_ns >= next_susp_poll) {
                next_susp_poll = now_ns + SUSPEND_POLL_NS;
                bool want = suspendRequested();
                if (want != suspended) {
                    suspended = want;
                    std::cerr << LOG << (suspended ? "Ausgabe pausiert (Tray-Achsenanzeige)"
                                                   : "Ausgabe wieder aktiv") << std::endl;
                }
            }
            // Profil mit ENABLED=0 (z.B. [app:blender]): gleiche Behandlung, keine Ausgabe.
            const bool enabled = settings_.enabled;
            if (enabled != prev_enabled) {
                prev_enabled = enabled;
                if (enabled) std::cerr << LOG << "Steuerung wieder aktiv" << std::endl;
                else std::cerr << LOG << "Steuerung deaktiviert (Profil " << sanitizeForLog(match_desc_)
                               << ")" << std::endl;
            }
            if (suspended || !enabled) {
                fresh = false;
                raw = RawAxes{};
                buttons = 0;
            }

            status_.update("profile=" + sanitizeForLog(match_desc_) + "\ndevice=" + sanitizeForLog(device_) +
                               "\napp=" + sanitizeForLog(wm_class_) + "\nenabled=" + (enabled ? "1" : "0") +
                               "\npaused=" + (suspended ? "1" : "0") +
                               "\ndevices_seen=" + std::to_string(devices_seen) + "\n",
                           now_ns);

            const Settings &s = settings_;
            bool nav_active = fresh && s.nav_button_enabled && buttonHeld(buttons, s.nav_button);

            // Klick-Button 1:1 als linke Maustaste (Press + Release getrennt),
            // dadurch funktioniert auch Klicken-und-Halten zum Draggen.
            bool click_active = s.click_button_enabled && buttonHeld(buttons, s.click_button);
            if (click_active != prev_click_active) {
                output_->mouseButton(1, click_active);
                prev_click_active = click_active;
            }

            // --- Kanaele ---------------------------------------------------
            // Scroll-Kanaele laufen jeden Tick (Glaettungszustand bleibt stetig),
            // gesendet wird je nach Modus.
            double rate_v = ch_v_.process(s.scroll_v, raw, dt);
            double rate_h = ch_h_.process(s.scroll_h, raw, dt);

            // Zoom = Twist; Einstieg nur, wenn er die anderen Achsen dominiert
            // (Hysterese/Verzoegerung in ZoomDetector). Im Nav-Modus aus.
            double zoom_mixed = evalMix(s.zoom.mix, raw);
            double twist = nav_active ? 0.0 : std::fabs(zoom_mixed * s.zoom.sign);
            double lateral = lateralMax(s.zoom_lateral, raw);
            if (s.zoom.sensitivity <= 0) twist = 0.0;
            bool zooming = zoom_.update(twist, lateral, s, dt);
            double rate_zoom = ch_zoom_.processValue(s.zoom, zoom_mixed, dt);

            setCtrl(zooming);
            if (zooming) {
                acc_v_.reset();
                acc_h_.reset();
                // Positive Rate = Scroll hoch = Zoom rein; wheelV(n>0) = runter.
                int n = acc_zoom_.add(rate_zoom, dt);
                if (n != 0) output_->wheelV(-n);
            } else {
                acc_zoom_.reset();
                // Positive Rate = runter bzw. rechts (wie bisher).
                int nv = stepOrReset(acc_v_, rate_v, dt);
                int nh = stepOrReset(acc_h_, rate_h, dt);
                if (nav_active) {
                    tapArrows(Key::Up, Key::Down, nv);
                    tapArrows(Key::Left, Key::Right, nh);
                } else {
                    if (nv != 0) output_->wheelV(nv);
                    if (nh != 0) output_->wheelH(nh);
                }
            }

            if (debug_ && now_ns >= next_debug &&
                (raw[AX_X] || raw[AX_Y] || raw[AX_Z] || raw[AX_RX] || raw[AX_RY] || raw[AX_RZ])) {
                next_debug = now_ns + DEBUG_PERIOD_NS;
                std::cerr << "h=" << rate_h << " v=" << rate_v << " twist=" << twist
                          << " lateral=" << lateral << " zoom=" << zooming
                          << (nav_active ? " nav" : "") << " profil=[" << sanitizeForLog(match_desc_) << "]"
                          << std::endl;
            }
            output_->flush();
        }

        ctrl_held_ = false;
        output_->releaseAll();
        output_->flush();
        status_.remove();
        std::cerr << LOG << "beendet" << std::endl;
    }

private:
    bool debug_;
    std::string config_path_;
    FileStamp stamp_;
    ProfileSet profiles_;
    Settings settings_;
    std::string match_desc_;
    std::string backend_cfg_;
    std::string device_;
    std::string wm_class_;
    bool warn_buttons_ = false;

    DeviceReader reader_;
    ActiveWindow activewin_;
    StatusWriter status_;
    std::unique_ptr<Output> output_;

    Channel ch_v_, ch_h_, ch_zoom_;
    StepAccumulator acc_v_, acc_h_, acc_zoom_;
    ZoomDetector zoom_;
    bool ctrl_held_ = false;

    static std::chrono::steady_clock::duration periodFor(int hz) {
        hz = std::clamp(hz, 30, 1000);
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / hz));
    }

    void loadConfig() {
        // Stempel vor dem Lesen nehmen: aendert sich die Datei waehrend des
        // Lesens, faellt das bei der naechsten Pruefung auf.
        stamp_ = statFile(config_path_);
        ProfileSet ps;
        std::vector<std::string> warnings;
        if (!loadProfiles(config_path_, &ps, &warnings)) {
            std::cerr << LOG << "WARNUNG: Config " << config_path_
                      << " nicht lesbar, behalte bisherige Einstellungen" << std::endl;
        } else {
            profiles_ = std::move(ps);
        }
        for (const std::string &w : warnings)
            std::cerr << LOG << "Config: " << w << std::endl;
    }

    // Loest die Settings neu auf. Aendern sich weder die greifenden Sektionen
    // noch die aufgeloesten Werte (z.B. Config nur angefasst, identischer
    // Inhalt oder Ladefehler), passiert nichts - insbesondere bleiben gehaltene
    // Tasten (Klick-Drag, Strg) erhalten. Sonst werden Filterzustaende
    // zurueckgesetzt und Strg freigegeben; der Klick-Button wird nicht
    // angefasst (die Tick-Logik gleicht ihn mit den neuen Settings ab).
    void applyProfile() {
        std::string desc = describeMatches(profiles_, device_, wm_class_);
        Settings next = profiles_.resolve(device_, wm_class_);
        if (desc == match_desc_ && allKeys(next) == allKeys(settings_)) return;

        settings_ = std::move(next);
        match_desc_ = desc;

        ch_v_.reset();
        ch_h_.reset();
        ch_zoom_.reset();
        acc_v_.reset();
        acc_h_.reset();
        acc_zoom_.reset();
        zoom_.reset();
        setCtrl(false);
        output_->flush();

        std::cerr << LOG << "Profil: " << sanitizeForLog(match_desc_) << " (Geraet \""
                  << (device_.empty() ? "?" : sanitizeForLog(device_)) << "\", Programm \""
                  << (wm_class_.empty() ? "?" : sanitizeForLog(wm_class_)) << "\")" << std::endl;

        if (settings_.backend != backend_cfg_) {
            std::cerr << LOG << "Hinweis: BACKEND=" << settings_.backend
                      << " wird erst nach Neustart wirksam (aktiv: " << output_->name() << ")"
                      << std::endl;
            backend_cfg_ = settings_.backend;
        }
        warn_buttons_ = true;
        checkButtons();
    }

    // Warnt, wenn ein konfigurierter Button auf dem Geraet nicht existiert
    // (nur einmal pro Geraet/Profil-Wechsel).
    void checkButtons() {
        if (!warn_buttons_) return;
        warn_buttons_ = false;
        int buttons = reader_.dev_buttons.load(std::memory_order_relaxed);
        if (buttons <= 0) return;
        if (settings_.nav_button_enabled && settings_.nav_button >= buttons)
            std::cerr << LOG << "WARNUNG: NAV_BUTTON " << settings_.nav_button
                      << " existiert auf diesem Geraet nicht" << std::endl;
        if (settings_.click_button_enabled && settings_.click_button >= buttons)
            std::cerr << LOG << "WARNUNG: CLICK_BUTTON " << settings_.click_button
                      << " existiert auf diesem Geraet nicht" << std::endl;
    }

    void setCtrl(bool down) {
        if (down == ctrl_held_) return;
        output_->setKey(Key::Ctrl, down);
        ctrl_held_ = down;
    }

    // Unterhalb der Deadzone (Rate 0) Rest verwerfen, damit ein angefangener
    // Schritt nicht beim naechsten Antippen ploetzlich ausgeloest wird.
    static int stepOrReset(StepAccumulator &acc, double rate, double dt) {
        if (rate == 0.0) {
            acc.reset();
            return 0;
        }
        return acc.add(rate, dt);
    }

    void tapArrows(Key neg, Key pos, int n) {
        for (; n > 0; --n) output_->tapKey(pos);
        for (; n < 0; ++n) output_->tapKey(neg);
    }
};

void printHelp() {
    std::cout <<
        "Aufruf: dofscroll [--debug] [--release-keys] [--help] [--version]\n"
        "\n"
        "Uebersetzt SpaceMouse-Bewegungen (spacenavd/libspnav) in Scroll-, Zoom-,\n"
        "Pfeiltasten- und Maustasten-Events. Config: " << defaultConfigPath() << "\n"
        "(frueherer Pfad: " << previousConfigPath() << ", wird automatisch uebernommen)\n"
        "(Profile [global], [device:NAME], [app:WM_CLASS]).\n"
        "\n"
        "  --debug    gibt ~10x/s die ausgewerteten Kanaele, die Zoom-Entscheidung\n"
        "             und das aktive Profil aus (vorher den Dienst stoppen, sonst\n"
        "             werden Events doppelt gesendet)\n"
        "  --release-keys  gibt Strg, Pfeiltasten und linke Maustaste frei und endet\n"
        "             (ExecStopPost der systemd-Unit: Absicherung nach Absturz/SIGKILL)\n"
        "  --help     diese Hilfe\n"
        "  --version  Version ausgeben\n"
        "\n"
        "Signale: SIGHUP = Config neu laden (zusaetzlich automatisch bei Aenderung\n"
        "der Datei), SIGTERM/SIGINT = sauber beenden (gehaltene Tasten frei).\n";
}

}  // namespace

// --release-keys: eigene Ausgabe oeffnen, alles freigeben, beenden.
static int releaseKeysAndExit() {
    ProfileSet ps;
    std::vector<std::string> warnings;
    loadProfiles(defaultConfigPath(), &ps, &warnings);
    std::string log;
    std::unique_ptr<Output> out = makeOutput(ps.resolve("", "").backend, &log);
    if (!out) {
        std::cerr << LOG << "--release-keys: keine Ausgabe moeglich" << std::endl;
        return 1;
    }
    out->forceReleaseAll();
    out->flush();
    return 0;
}

int main(int argc, char **argv) {
    // Die Fensterabfrage laeuft in einem eigenen Thread (eigenes Display), die Ausgabe
    // im Haupt-Thread: Xlib muss vor dem ersten Aufruf thread-faehig gemacht werden.
    XInitThreads();
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--debug") debug = true;
        else if (a == "--help" || a == "-h") { printHelp(); return 0; }
        else if (a == "--version") { std::cout << VERSION << std::endl; return 0; }
        else if (a == "--release-keys") return releaseKeysAndExit();
        else {
            std::cerr << "dofscroll: unbekannte Option '" << a << "' (siehe --help)" << std::endl;
            return 2;
        }
    }

    // Bewusst nie zerstoert: der detachte Reader-Thread greift bis zum
    // Prozess-Exit auf den DeviceReader im Daemon zu.
    // Logzeilen zusaetzlich in die Log-Datei spiegeln (bewusst nie zerstoert: detachter Reader-Thread).
    if (!debug)
        std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), kLogMaxLines, kLogKeepLines));
    else
        std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), kLogMaxLinesDebug, kLogKeepLinesDebug));
    Daemon *daemon = new Daemon(debug);
    if (!daemon->initialize()) return 1;
    daemon->run();
    return 0;
}
