// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tray icon + settings window for dofscroll (profile-aware version).
//
// Deliberately a separate process/service from the scroll daemon: the GTK main loop
// and tray overhead must never affect the time-critical tick thread.
// This app only reads/writes the shared config file - exclusively through
// loadProfiles()/saveProfiles() from config.hpp (no parser copy of its own) - and
// triggers a live reload in the running daemon via SIGHUP (through systemctl --user).
//
// Operating model: a profile is chosen at the top ([global], [device:*], [app:*]).
// All controls show the *resolved* values of that profile (global + section).
// When saving, ProfileSet::setSectionFromSettings() writes only the keys that differ
// from Global for non-global profiles (diffKeys).
//
// "Axes" tab: axis sketch, live display and the "Learn axes" wizard. For this the
// tray opens - only while the settings window is open - its own read-only libspnav
// connection (spacenavd allows several clients; the daemon is unaffected). The
// wizard's evaluation lives GUI-free in axis_wizard.hpp (tested in
// tests/test_axis_wizard.cpp).
//
// Bottom of the settings window: "Application modes" list (program/device profile ->
// mode), built from the loaded mode files; pure logic in mode_list.hpp.
//
// UI strings are English and wrapped in _() (gettext, see i18n.hpp and po/).
// mode_list.hpp/modes.hpp first: modes.hpp has an enumerator "None", which the Xlib macro
// None (pulled in by spnav.h/GTK) would break if those headers came earlier.
#include "mode_list.hpp"
#include "modes.hpp"

#include <glib-unix.h>
#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#include <spnav.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#include "axis_wizard.hpp"
#include "config.hpp"
#include "device_db.hpp"
#include "i18n.hpp"
#include "seed.hpp"

using spn::ChannelCfg;
using spn::ProfileSet;
using spn::Section;
using spn::Settings;

// ---------------------------------------------------------------------------
// Helper processes: always via g_spawn_async with an argv array, never system() with
// an assembled string (paths may contain spaces/special characters).
// Without G_SPAWN_DO_NOT_REAP_CHILD GLib reaps the child processes itself.
// ---------------------------------------------------------------------------
// Programs are preferably resolved from /usr/bin or /bin (not from a manipulable
// $PATH); only if not found there does it fall back to a PATH search.
static std::string resolveExe(const std::string &name) {
    if (name.find('/') != std::string::npos) return name;
    for (const char *dir : {"/usr/bin/", "/bin/"}) {
        std::string p = std::string(dir) + name;
        if (access(p.c_str(), X_OK) == 0) return p;
    }
    return name;
}

static void spawnDetached(const std::vector<std::string> &args_in) {
    std::vector<std::string> args = args_in;
    if (!args.empty()) args[0] = resolveExe(args[0]);
    const bool search = !args.empty() && args[0].find('/') == std::string::npos;
    std::vector<gchar *> argv;
    for (const auto &a : args) argv.push_back(const_cast<gchar *>(a.c_str()));
    argv.push_back(nullptr);
    GError *err = nullptr;
    if (!g_spawn_async(nullptr, argv.data(), nullptr,
                       GSpawnFlags((search ? G_SPAWN_SEARCH_PATH : 0) | G_SPAWN_STDOUT_TO_DEV_NULL |
                                   G_SPAWN_STDERR_TO_DEV_NULL),
                       nullptr, nullptr, nullptr, &err)) {
        fprintf(stderr, "dofscroll-tray: could not start %s: %s\n",
                args.empty() ? "?" : args[0].c_str(), err ? err->message : "?");
        if (err) g_error_free(err);
    }
}

// Makes the running scroll daemon re-read the config that was just saved
// (see the SIGHUP handler there).
static void reloadDaemon() {
    spawnDetached({"systemctl", "--user", "kill", "-s", "HUP", "dofscroll.service"});
}

static void restartDaemon() {
    spawnDetached({"systemctl", "--user", "restart", "dofscroll.service"});
}

// printf-style formatting into a std::string (for translated format strings).
static std::string sfmt(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static std::string sfmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    gchar *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    std::string r = s ? s : "";
    g_free(s);
    return r;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Description of a channel: display name, key prefix in the file, field in Settings.
struct ChanDesc {
    const char *title;
    const char *prefix;
    ChannelCfg Settings::*member;
};

static const ChanDesc CHANNELS[] = {
    {N_("Scroll vertical"), "SCROLL_V", &Settings::scroll_v},
    {N_("Scroll horizontal"), "SCROLL_H", &Settings::scroll_h},
    {N_("Zoom"), "ZOOM", &Settings::zoom},
};

// A control in the window: load() writes the value from App::cur into the widget,
// apply() reads the widget back into App::cur (false = nothing taken over,
// e.g. invalid input).
struct Binding {
    std::function<void()> load;
    std::function<bool()> apply;
    GtkWidget *widget = nullptr;  // for disconnecting the signal on close
};

struct App {
    ProfileSet ps;
    size_t cur_idx = 0;
    Settings cur;            // resolved settings of the selected profile
    guint debounce_id = 0;
    bool updating = false;   // true during refresh(): ignore widget signals

    // Only valid while the window is open.
    GtkWidget *window = nullptr;
    GtkWidget *profile_combo = nullptr;
    GtkWidget *profile_info = nullptr;
    GtkWidget *delete_btn = nullptr;
    std::vector<GtkWidget *> reset_btns;
    std::vector<GtkWidget *> graphs;
    std::vector<std::unique_ptr<Binding>> binds;
    std::vector<std::function<void()>> sensitivity_rules;
    int axes_chan = 0;       // "Axes" tab: highlighted channel (index into CHANNELS)
    GtkWidget *mode_box = nullptr;  // rows of the "Application modes" list
    std::string modes_stamp;        // modesStamp() at the last rebuild of that list
    // Button-index spinners (limited to the connected device's real button count).
    std::vector<std::pair<GtkWidget *, int Settings::*>> button_spins;

    // Sync with the file (lost-update protection, see applyDebounced):
    ProfileSet base;         // state of g.ps at the last load/save
    std::string disk_content;  // file content at that time
    bool disk_known = false;
};

static App g;

// Always determine the config path fresh: after migration/first save defaultConfigPath()
// may switch from the legacy to the new path.
static std::string cfgPath() { return spn::defaultConfigPath(); }

// ---------------------------------------------------------------------------
// Saving (debounced)
// ---------------------------------------------------------------------------

static bool readFileContent(const std::string &path, std::string *out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

static void syncDiskState() {
    g.base = g.ps;
    g.disk_known = true;
    if (!readFileContent(cfgPath(), &g.disk_content)) g.disk_content.clear();
}

// Same normalization as ProfileSet::findOrAdd (trimmed, lowercase).
static std::string patKey(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string r = s.substr(b, e - b);
    for (char &c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return r;
}

static size_t findSection(const ProfileSet &ps, const Section &like) {
    if (like.kind == Section::GLOBAL) return 0;
    const std::string want = patKey(like.pattern);
    for (size_t i = 1; i < ps.sections.size(); ++i)
        if (ps.sections[i].kind == like.kind && patKey(ps.sections[i].pattern) == want) return i;
    return ProfileSet::npos;
}

static void refresh();

// Lost-update protection: if the file changed since the last load/save (e.g. by hand via
// "Open config file"), it is loaded fresh and only the difference between g.base (state at
// the last sync) and g.ps - i.e. exactly what the user changed in the window - is applied
// to it: set changed/new keys, delete removed keys, take over new/deleted profiles.
// Everything else stays as on disk. The result then replaces g.ps.
// Returns false: file not readable, nothing merged (caller must NOT save).
static bool mergeDiskChanges() {
    ProfileSet disk;
    if (!spn::loadProfiles(cfgPath(), &disk, nullptr)) return false;
    auto setKey = [](Section &sec, const std::string &k, const std::string &v) {
        bool found = false;
        for (auto &kv : sec.kv)
            if (kv.first == k) {
                kv.second = v;
                found = true;
            }
        if (!found) sec.kv.emplace_back(k, v);
    };
    for (const Section &mine : g.ps.sections) {
        size_t bi = findSection(g.base, mine);
        size_t di = findSection(disk, mine);
        if (di == ProfileSet::npos) {
            if (bi != ProfileSet::npos) continue;  // removed by hand on disk: stays removed
            di = disk.findOrAdd(mine.kind, mine.pattern);
            if (di == ProfileSet::npos) continue;
        }
        const Section *bs = bi == ProfileSet::npos ? nullptr : &g.base.sections[bi];
        for (const auto &kv : mine.kv) {
            bool changed = true;
            if (bs)
                for (const auto &b : bs->kv)
                    if (b.first == kv.first && b.second == kv.second) changed = false;
            if (changed) setKey(disk.sections[di], kv.first, kv.second);
        }
        if (bs)
            for (const auto &b : bs->kv) {
                bool still = false;
                for (const auto &kv : mine.kv) still = still || kv.first == b.first;
                if (still) continue;
                auto &v = disk.sections[di].kv;
                for (size_t k = v.size(); k-- > 0;)
                    if (v[k].first == b.first) v.erase(v.begin() + long(k));
            }
    }
    for (size_t i = 1; i < g.base.sections.size(); ++i)
        if (findSection(g.ps, g.base.sections[i]) == ProfileSet::npos) {
            size_t di = findSection(disk, g.base.sections[i]);
            if (di != ProfileSet::npos) disk.remove(di);
        }
    const Section cur = g.cur_idx < g.ps.sections.size() ? g.ps.sections[g.cur_idx] : Section();
    g.ps = disk;
    size_t ni = findSection(g.ps, cur);
    g.cur_idx = ni == ProfileSet::npos ? 0 : ni;
    g.cur = g.ps.sectionSettings(g.cur_idx);
    if (g.window) refresh();
    return true;
}

static void fillProfileCombo();
static void rebuildModeList();

static gboolean applyDebounced(gpointer) {
    g.debounce_id = 0;
    if (g.disk_known) {
        std::string now;
        const bool have = readFileContent(cfgPath(), &now);
        if ((have ? now : std::string()) != g.disk_content) {
            fprintf(stderr, "dofscroll-tray: %s was changed externally - merging\n",
                    cfgPath().c_str());
            if (!mergeDiskChanges()) {
                // The change stays in g.ps, the next change tries again.
                static bool warned = false;
                if (!warned)
                    fprintf(stderr, "dofscroll-tray: %s not readable - save skipped, "
                                    "change stays pending\n", cfgPath().c_str());
                warned = true;
                return G_SOURCE_REMOVE;
            }
            if (g.window && g.profile_combo) fillProfileCombo();
        }
    }
    std::string err;
    if (!spn::saveProfiles(cfgPath(), g.ps, &err)) {
        fprintf(stderr, "dofscroll-tray: saving %s failed: %s\n",
                cfgPath().c_str(), err.c_str());
        return G_SOURCE_REMOVE;
    }
    syncDiskState();
    reloadDaemon();
    return G_SOURCE_REMOVE;
}

// Does not save/reload on every single slider tick, only 250 ms after the last
// change - avoids file I/O spam while dragging.
static void scheduleApply() {
    if (g.debounce_id != 0) g_source_remove(g.debounce_id);
    g.debounce_id = g_timeout_add(250, applyDebounced, nullptr);
}

// Run a pending save immediately (e.g. before "open file"/quit).
static void flushPending() {
    if (g.debounce_id != 0) {
        g_source_remove(g.debounce_id);
        applyDebounced(nullptr);
    }
}

// "Program: x" / "Device: x" (kind and pattern of a profile).
static std::string kindLabel(Section::Kind kind, const std::string &pattern) {
    switch (kind) {
    case Section::DEVICE: return sfmt(_("Device: %s"), pattern.c_str());
    case Section::APP: return sfmt(_("Program: %s"), pattern.c_str());
    default: return _("Global");
    }
}

static std::string sectionLabel(const Section &s) { return kindLabel(s.kind, s.pattern); }

static void updateProfileInfo() {
    if (!g.profile_info) return;
    std::string txt;
    if (g.cur_idx == 0) {
        txt = _("Global base settings. Device and program profiles override only "
                "individual values of them.");
    } else {
        size_t n = g.ps.sections[g.cur_idx].kv.size();
        txt = sfmt(NG_("The effective values are shown (Global + this profile). Only "
                       "deviations from Global are saved - currently %d value.",
                       "The effective values are shown (Global + this profile). Only "
                       "deviations from Global are saved - currently %d values.", n),
                   int(n));
    }
    gtk_label_set_text(GTK_LABEL(g.profile_info), txt.c_str());
}

static void updateSensitivity() {
    for (auto &rule : g.sensitivity_rules) rule();
    for (GtkWidget *b : g.reset_btns) gtk_widget_set_sensitive(b, g.cur_idx != 0);
    if (g.delete_btn) gtk_widget_set_sensitive(g.delete_btn, g.cur_idx != 0);
    for (GtkWidget *d : g.graphs) gtk_widget_queue_draw(d);
}

// After every accepted change: write back into the section, schedule saving.
static void commit() {
    g.ps.setSectionFromSettings(g.cur_idx, g.cur);
    updateProfileInfo();
    updateSensitivity();
    scheduleApply();
}

// Refill all widgets from g.cur (without triggering a save).
static void refresh() {
    g.updating = true;
    for (auto &b : g.binds) b->load();
    g.updating = false;
    updateProfileInfo();
    updateSensitivity();
}

static void selectProfile(size_t idx) {
    if (idx >= g.ps.sections.size()) idx = 0;
    g.cur_idx = idx;
    g.cur = g.ps.sectionSettings(idx);
    refresh();
}

static void fillProfileCombo() {
    if (!g.profile_combo) return;
    g.updating = true;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(g.profile_combo));
    for (const auto &s : g.ps.sections)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g.profile_combo), sectionLabel(s).c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(g.profile_combo), int(g.cur_idx));
    g.updating = false;
    rebuildModeList();
}

// ---------------------------------------------------------------------------
// Widget bindings
// ---------------------------------------------------------------------------

static void onBindChanged(GtkWidget *, gpointer data) {
    if (g.updating || !g.window) return;
    if (static_cast<Binding *>(data)->apply()) commit();
}

static Binding *addBinding(GtkWidget *w, const char *signal, std::function<void()> load,
                           std::function<bool()> apply) {
    g.binds.push_back(std::make_unique<Binding>(Binding{std::move(load), std::move(apply)}));
    Binding *b = g.binds.back().get();
    b->widget = w;
    g_signal_connect(w, signal, G_CALLBACK(onBindChanged), b);
    return b;
}

// Mouse wheel over a spin box/slider/combo must not change the value but scroll the
// surrounding window: stop the event there and pass it on to the parent widget.
static GtkWidget *noWheelChange(GtkWidget *w) {
    g_signal_connect(w, "scroll-event", G_CALLBACK(+[](GtkWidget *w, GdkEvent *ev, gpointer) -> gboolean {
        g_signal_stop_emission_by_name(w, "scroll-event");
        if (GtkWidget *parent = gtk_widget_get_parent(w)) gtk_propagate_event(parent, ev);
        return TRUE;
    }), nullptr);
    return w;
}

static GtkWidget *bindIntSpin(int lo, int hi, std::function<int &()> field) {
    GtkWidget *spin = noWheelChange(gtk_spin_button_new_with_range(lo, hi, 1));
    addBinding(spin, "value-changed",
               [spin, field] { gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), field()); },
               [spin, field] {
                   field() = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
                   return true;
               });
    return spin;
}

static GtkWidget *bindDoubleSpin(double lo, double hi, double step, int digits,
                                 std::function<double &()> field) {
    GtkWidget *spin = noWheelChange(gtk_spin_button_new_with_range(lo, hi, step));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
    addBinding(spin, "value-changed",
               [spin, field] { gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), field()); },
               [spin, field] {
                   field() = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
                   return true;
               });
    return spin;
}

static GtkWidget *bindCheck(const char *label, std::function<bool()> get,
                            std::function<void(bool)> set) {
    GtkWidget *check = gtk_check_button_new_with_label(label);
    addBinding(check, "toggled",
               [check, get] { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), get()); },
               [check, set] {
                   set(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
                   return true;
               });
    return check;
}

// Mark invalid text input in red (own class + Adwaita "error").
static void markEntry(GtkWidget *entry, bool valid, const std::string &err) {
    GtkStyleContext *sc = gtk_widget_get_style_context(entry);
    if (valid) {
        gtk_style_context_remove_class(sc, "spn-invalid");
        gtk_style_context_remove_class(sc, GTK_STYLE_CLASS_ERROR);
        gtk_widget_set_tooltip_text(entry, nullptr);
    } else {
        gtk_style_context_add_class(sc, "spn-invalid");
        gtk_style_context_add_class(sc, GTK_STYLE_CLASS_ERROR);
        gtk_widget_set_tooltip_text(entry, err.c_str());
    }
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Curve value for the preview, same formula as spn::applyCurve() in dsp.hpp
// (replicated here so the tray does not have to link against dsp.cpp).
static double previewCurve(const ChannelCfg &c, double v) {
    const double ref = 200.0;  // = spn::CURVE_REF
    if (v <= 0) return 0.0;
    if (c.curve_points.empty()) return std::pow(v / ref, c.curve_exp) * ref;
    const auto &p = c.curve_points;
    double f;
    if (v <= p.front().first) f = p.front().second;
    else if (v >= p.back().first) f = p.back().second;
    else {
        f = p.back().second;
        for (size_t i = 1; i < p.size(); ++i) {
            if (v <= p[i].first) {
                double x0 = p[i - 1].first, x1 = p[i].first;
                double t = x1 > x0 ? (v - x0) / (x1 - x0) : 1.0;
                f = p[i - 1].second + t * (p[i].second - p[i - 1].second);
                break;
            }
        }
    }
    return v * f;
}

// Preview graph: x = input after deadzone 0..350, y = output (linear = diagonal).
static gboolean onGraphDraw(GtkWidget *w, cairo_t *cr, gpointer data) {
    const ChanDesc *d = static_cast<const ChanDesc *>(data);
    const ChannelCfg &c = g.cur.*(d->member);
    const double W = gtk_widget_get_allocated_width(w);
    const double H = gtk_widget_get_allocated_height(w);
    const double pad = 4, XMAX = 350;

    double ymax = XMAX;
    for (int i = 0; i <= 70; ++i) ymax = std::max(ymax, previewCurve(c, XMAX * i / 70.0));
    if (ymax > 4 * XMAX) ymax = 4 * XMAX;  // cut off extreme curves at the top

    GdkRGBA fg;
    gtk_style_context_get_color(gtk_widget_get_style_context(w), GTK_STATE_FLAG_NORMAL, &fg);
    auto px = [&](double x) { return pad + x / XMAX * (W - 2 * pad); };
    auto py = [&](double y) { return H - pad - std::min(y, ymax) / ymax * (H - 2 * pad); };

    // Frame
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.25);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, pad + 0.5, pad + 0.5, W - 2 * pad - 1, H - 2 * pad - 1);
    cairo_stroke(cr);

    // Linear reference (dashed)
    double dash = 3;
    cairo_set_dash(cr, &dash, 1, 0);
    cairo_move_to(cr, px(0), py(0));
    cairo_line_to(cr, px(XMAX), py(XMAX));
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // Curve
    cairo_set_source_rgba(cr, 0.21, 0.52, 0.89, 1.0);
    cairo_set_line_width(cr, 2);
    const int N = 140;
    for (int i = 0; i <= N; ++i) {
        double x = XMAX * i / N;
        if (i == 0) cairo_move_to(cr, px(x), py(previewCurve(c, x)));
        else cairo_line_to(cr, px(x), py(previewCurve(c, x)));
    }
    cairo_stroke(cr);

    // Labels
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.6);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    cairo_move_to(cr, pad + 3, pad + 11);
    cairo_show_text(cr, ymax > XMAX * 1.01 ? _("Output (clipped)") : _("Output"));
    cairo_move_to(cr, W - pad - 72, H - pad - 4);
    cairo_show_text(cr, _("Input 0..350"));
    return FALSE;
}

// ---------------------------------------------------------------------------
// Window construction
// ---------------------------------------------------------------------------

static GtkWidget *newGrid() {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    return grid;
}

static void labeled(GtkWidget *grid, int row, const char *label, GtkWidget *control,
                    const char *tooltip = nullptr) {
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    if (tooltip) {
        gtk_widget_set_tooltip_text(lbl, tooltip);
        gtk_widget_set_tooltip_text(control, tooltip);
    }
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_grid_attach(GTK_GRID(grid), control, 1, row, 1, 1);
}

static GtkWidget *hint(const char *text) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), GTK_STYLE_CLASS_DIM_LABEL);
    return l;
}

static GtkWidget *frame(const char *title, GtkWidget *child) {
    GtkWidget *f = gtk_frame_new(title);
    gtk_container_add(GTK_CONTAINER(f), child);
    return f;
}

static GtkWidget *buildChannel(const ChanDesc *d, bool with_zoom_extras) {
    ChannelCfg Settings::*m = d->member;
    auto ch = [m]() -> ChannelCfg & { return g.cur.*m; };
    const std::string curve_key = std::string(d->prefix) + "_CURVE";

    GtkWidget *grid = newGrid();
    int row = 0;

    // Axis mix: the value is only taken over once parseMix() accepts it.
    GtkWidget *mix = gtk_entry_new();
    addBinding(mix, "changed",
               [mix, ch] {
                   gtk_entry_set_text(GTK_ENTRY(mix), spn::mixToString(ch().mix).c_str());
                   markEntry(mix, true, "");
               },
               [mix, ch] {
                   spn::Mix parsed;
                   std::string err;
                   bool ok = spn::parseMix(gtk_entry_get_text(GTK_ENTRY(mix)), &parsed, &err);
                   markEntry(mix, ok, err);
                   if (ok) ch().mix = parsed;
                   return ok;
               });
    labeled(grid, row++, _("Axis mix"), mix,
            _("Sum of [+-][weight*]axis, axes: x y z rx ry rz.\n"
              "Examples: \"z + rx\", \"x - rz\", \"0.5*rx + z\", \"-ry\". Empty = channel off."));

    labeled(grid, row++, _("Sensitivity"),
            bindIntSpin(0, 255, [ch]() -> int & { return ch().sensitivity; }),
            _("0..255, 0 = channel off"));
    labeled(grid, row++, _("Deadzone"),
            bindIntSpin(0, 500, [ch]() -> int & { return ch().deadzone; }),
            _("Raw sensor units; nothing happens below this"));
    labeled(grid, row++, _("Smoothing (ms)"),
            bindDoubleSpin(0, 500, 1, 0, [ch]() -> double & { return ch().smooth_ms; }),
            _("Low-pass time constant, 0 = off"));

    GtkWidget *inv = bindCheck(_("Invert direction"), [ch] { return ch().sign < 0; },
                               [ch](bool on) { ch().sign = on ? -1 : 1; });
    gtk_grid_attach(GTK_GRID(grid), inv, 0, row++, 2, 1);

    // Response curve: exponent slider, alternatively a point list (takes precedence).
    GtkWidget *exp = noWheelChange(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.2, 4.0, 0.05));
    gtk_scale_set_digits(GTK_SCALE(exp), 2);
    gtk_scale_set_value_pos(GTK_SCALE(exp), GTK_POS_RIGHT);
    gtk_scale_add_mark(GTK_SCALE(exp), 1.0, GTK_POS_BOTTOM, nullptr);
    addBinding(exp, "value-changed",
               [exp, ch] { gtk_range_set_value(GTK_RANGE(exp), ch().curve_exp); },
               [exp, ch] {
                   ch().curve_exp = gtk_range_get_value(GTK_RANGE(exp));
                   return true;
               });
    // "-"/"+" next to the slider (one step each), since the mouse wheel does not move it.
    GtkWidget *exp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_hexpand(exp, TRUE);
    GtkWidget *minus = gtk_button_new_with_label("\xe2\x88\x92");
    GtkWidget *plus = gtk_button_new_with_label("+");
    for (GtkWidget *b : {minus, plus}) {
        gtk_widget_set_tooltip_text(b, _("Adjust by one step (0.05)"));
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    }
    g_signal_connect_swapped(minus, "clicked", G_CALLBACK(+[](GtkWidget *r) {
        gtk_range_set_value(GTK_RANGE(r), gtk_range_get_value(GTK_RANGE(r)) - 0.05);
    }), exp);
    g_signal_connect_swapped(plus, "clicked", G_CALLBACK(+[](GtkWidget *r) {
        gtk_range_set_value(GTK_RANGE(r), gtk_range_get_value(GTK_RANGE(r)) + 0.05);
    }), exp);
    gtk_box_pack_start(GTK_BOX(exp_box), minus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(exp_box), exp, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(exp_box), plus, FALSE, FALSE, 0);
    labeled(grid, row++, _("Curve (exponent)"), exp_box,
            _("1.0 = linear, >1 = finer at small deflections"));

    GtkWidget *pts = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pts), _("empty = exponent applies, e.g. 0:0 50:0.1 200:1 350:3"));
    addBinding(pts, "changed",
               [pts, ch, curve_key] {
                   std::string txt;
                   if (!ch().curve_points.empty()) {
                       // Format exactly as config.cpp writes it (allKeys).
                       for (const auto &kv : spn::allKeys(g.cur))
                           if (kv.first == curve_key) txt = kv.second;
                   }
                   gtk_entry_set_text(GTK_ENTRY(pts), txt.c_str());
                   markEntry(pts, true, "");
               },
               [pts, ch, curve_key, m] {
                   std::string txt = trim(gtk_entry_get_text(GTK_ENTRY(pts)));
                   if (txt.empty()) {
                       markEntry(pts, true, "");
                       ch().curve_points.clear();
                       return true;
                   }
                   std::string err;
                   Settings tmp = g.cur;
                   bool ok = txt.find(':') != std::string::npos;
                   if (!ok) err = _("Point list expected: \"input:factor ...\" (use the slider for an exponent)");
                   else ok = spn::applyKey(tmp, curve_key, txt, &err) && !(tmp.*m).curve_points.empty();
                   if (ok) ch().curve_points = (tmp.*m).curve_points;
                   markEntry(pts, ok, err.empty() ? std::string(_("invalid point list")) : err);
                   return ok;
               });
    labeled(grid, row++, _("Curve (points)"), pts,
            _("Optional: ascending \"input:factor\" pairs, e.g. \"0:0 50:0.1 200:1 350:3\".\n"
              "Input = deflection after deadzone (raw sensor units), factor applies to the linear\n"
              "speed, linearly interpolated in between. Overrides the exponent."));
    g.sensitivity_rules.push_back([exp, ch] { gtk_widget_set_sensitive(exp, ch().curve_points.empty()); });

    GtkWidget *graph = gtk_drawing_area_new();
    gtk_widget_set_size_request(graph, -1, 110);
    g_signal_connect(graph, "draw", G_CALLBACK(onGraphDraw), const_cast<ChanDesc *>(d));
    g.graphs.push_back(graph);
    gtk_grid_attach(GTK_GRID(grid), graph, 0, row++, 2, 1);

    if (with_zoom_extras) {
        // Axes the zoom twist has to dominate against.
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        for (int a = 0; a < spn::AX_COUNT; ++a) {
            spn::Axis ax = spn::Axis(a);
            auto has = [ax] {
                for (spn::Axis x : g.cur.zoom_lateral)
                    if (x == ax) return true;
                return false;
            };
            GtkWidget *cb = bindCheck(spn::axisName(ax), has, [ax](bool on) {
                std::vector<spn::Axis> v;
                for (int b = 0; b < spn::AX_COUNT; ++b) {
                    spn::Axis bx = spn::Axis(b);
                    bool in = false;
                    for (spn::Axis x : g.cur.zoom_lateral) in = in || x == bx;
                    if (bx == ax) in = on;
                    if (in) v.push_back(bx);
                }
                g.cur.zoom_lateral = v;
            });
            gtk_box_pack_start(GTK_BOX(box), cb, FALSE, FALSE, 0);
        }
        labeled(grid, row++, _("Zoom must dominate against"), box,
                _("Zoom only starts if the twist is stronger than the strongest of these axes"));
        labeled(grid, row++, _("Entry time (ms)"),
                bindIntSpin(0, 1000, [] () -> int & { return g.cur.zoom_enter_ms; }),
                _("How long the twist must dominate before zoom engages"));
        labeled(grid, row++, _("Exit ratio"),
                bindDoubleSpin(1.0, 10.0, 0.05, 2, [] () -> double & { return g.cur.zoom_exit_ratio; }),
                _("Hysteresis: zoom only ends once a lateral axis is stronger by this factor"));
    }

    GtkWidget *reset = gtk_button_new_with_label(_("Reset to Global"));
    gtk_widget_set_halign(reset, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(reset, with_zoom_extras
                                           ? _("Reset the channel including zoom detection to the global values")
                                           : _("Reset the channel to the global values"));
    g.reset_btns.push_back(reset);
    struct ResetData { ChannelCfg Settings::*m; bool zoom; };
    auto *rd = new ResetData{m, with_zoom_extras};
    g_signal_connect_data(reset, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *r = static_cast<ResetData *>(data);
        if (g.cur_idx == 0) return;
        Settings glob = g.ps.sectionSettings(0);
        g.cur.*(r->m) = glob.*(r->m);
        if (r->zoom) {
            g.cur.zoom_lateral = glob.zoom_lateral;
            g.cur.zoom_enter_ms = glob.zoom_enter_ms;
            g.cur.zoom_exit_ratio = glob.zoom_exit_ratio;
        }
        commit();
        refresh();
    }), rd, +[](gpointer data, GClosure *) { delete static_cast<ResetData *>(data); },
                          GConnectFlags(0));
    gtk_grid_attach(GTK_GRID(grid), reset, 0, row++, 2, 1);

    return frame(_(d->title), grid);
}

static GtkWidget *buildGeneral() {
    GtkWidget *grid = newGrid();
    int row = 0;

    GtkWidget *backend = noWheelChange(gtk_combo_box_text_new());
    for (const char *id : {"auto", "x11", "uinput"})
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(backend), id, id);
    addBinding(backend, "changed",
               [backend] {
                   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(backend), g.cur.backend.c_str()))
                       gtk_combo_box_set_active_id(GTK_COMBO_BOX(backend), "auto");
               },
               [backend] {
                   const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(backend));
                   if (!id) return false;
                   g.cur.backend = id;
                   return true;
               });
    labeled(grid, row++, _("Output backend"), backend,
            _("auto: Wayland -> uinput, otherwise X11 (with fallback)"));
    GtkWidget *bh = hint(_("Changing the backend only takes effect after the daemon is restarted."));
    gtk_grid_attach(GTK_GRID(grid), bh, 0, row++, 2, 1);

    labeled(grid, row++, _("Tick rate (Hz)"),
            bindIntSpin(30, 1000, [] () -> int & { return g.cur.tick_hz; }),
            _("How many times per second the daemon generates events (30..1000)"));

    // Checkbox + associated button-index spinner: the spin box is only usable while the
    // checkbox is active, so it is clear whether this button is used for nav/click at all.
    auto addToggleableSpin = [&](const char *checkLabel, bool Settings::*enabled,
                                 const char *spinLabel, int Settings::*field) {
        GtkWidget *check = bindCheck(checkLabel, [enabled] { return g.cur.*enabled; },
                                     [enabled](bool on) { g.cur.*enabled = on; });
        gtk_grid_attach(GTK_GRID(grid), check, 0, row++, 2, 1);
        GtkWidget *spin = bindIntSpin(0, 31, [field]() -> int & { return g.cur.*field; });
        g.button_spins.emplace_back(spin, field);
        labeled(grid, row++, spinLabel, spin);
        g.sensitivity_rules.push_back([spin, enabled] { gtk_widget_set_sensitive(spin, g.cur.*enabled); });
    };
    addToggleableSpin(_("Use nav button"), &Settings::nav_button_enabled,
                      _("Nav button (arrow keys instead of scroll)"), &Settings::nav_button);
    addToggleableSpin(_("Use click button"), &Settings::click_button_enabled,
                      _("Click button (left mouse button)"), &Settings::click_button);

    GtkWidget *restart = gtk_button_new_with_label(_("Restart daemon"));
    gtk_widget_set_halign(restart, GTK_ALIGN_END);
    g_signal_connect(restart, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
        flushPending();
        restartDaemon();
    }), nullptr);
    gtk_grid_attach(GTK_GRID(grid), restart, 0, row++, 2, 1);

    return frame(_("General"), grid);
}

// ---------------------------------------------------------------------------
// "Axes" tab - live reader: own libspnav connection, only while the settings window is
// open. Reading is non-blocking via spnav_poll_event() on the GLib main loop
// (g_unix_fd_add on spnav_fd()). If spacenavd is unreachable, another attempt is made
// every 2 s.
// ---------------------------------------------------------------------------

struct LiveReader {
    bool connected = false;
    guint fd_watch = 0;
    guint redraw_timer = 0;
    guint retry_timer = 0;
    spn::RawAxes axes{};
    bool dirty = false;
    std::string dev_name;
    int dev_buttons = -1;
    int dev_axes = -1;
    std::vector<GtkWidget *> views;          // bar displays (window + wizard)
    std::vector<GtkWidget *> status_labels;  // connection/device text
    std::function<void(const spn::RawAxes &)> on_motion;  // wizard measurement
    std::function<void()> on_state;                       // connection up/down (wizard)
};

static LiveReader live;

// Add a widget to a list and remove it automatically when it is destroyed.
static void trackIn(std::vector<GtkWidget *> *vec, GtkWidget *w) {
    vec->push_back(w);
    g_signal_connect(w, "destroy", G_CALLBACK(+[](GtkWidget *self, gpointer data) {
        auto *v = static_cast<std::vector<GtkWidget *> *>(data);
        v->erase(std::remove(v->begin(), v->end(), self), v->end());
    }), vec);
}

static std::string liveStatusText() {
    if (!live.connected)
        return _("spacenavd is not reachable - the live display starts automatically as soon "
                 "as the service is running (check with: systemctl status spacenavd).");
    const std::string btn = live.dev_buttons >= 0 ? std::to_string(live.dev_buttons) : "?";
    const std::string axes = live.dev_axes >= 0 ? std::to_string(live.dev_axes) : "?";
    return sfmt(_("Connected to spacenavd. Device: %s - %s buttons, %s axes"),
                live.dev_name.empty() ? _("(no name reported)") : live.dev_name.c_str(),
                btn.c_str(), axes.c_str());
}

// Limit the button-index spinners to the buttons the connected device really has
// (factoryButtonCount knows the model's physical buttons even if spacenavd reports more).
// The range never drops below the currently configured value (nothing is changed silently).
static void updateButtonLimits() {
    if (g.button_spins.empty() || !live.connected) return;
    const int n = spn::factoryButtonCount(live.dev_name, live.dev_buttons);
    if (n <= 0 || n > 32) return;
    const bool was = g.updating;
    g.updating = true;
    for (auto &p : g.button_spins) {
        const int cur = g.cur.*(p.second);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(p.first), 0, std::max(n - 1, cur));
        gtk_widget_set_tooltip_text(p.first, sfmt(_("This device has %d buttons (0..%d)"), n, n - 1).c_str());
    }
    g.updating = was;
}

static void liveUpdateStatus() {
    const std::string t = liveStatusText();
    for (GtkWidget *l : live.status_labels) gtk_label_set_text(GTK_LABEL(l), t.c_str());
    for (GtkWidget *v : live.views) gtk_widget_queue_draw(v);
    updateButtonLimits();
    if (live.on_state) live.on_state();
}

static void liveQueryDevice() {
    char name[256];
    live.dev_name = spnav_dev_name(name, sizeof(name)) >= 0 ? std::string(name) : std::string();
    live.dev_buttons = spnav_dev_buttons();
    live.dev_axes = spnav_dev_axes();
}

static void liveDisconnect() {
    if (live.fd_watch) {
        g_source_remove(live.fd_watch);
        live.fd_watch = 0;
    }
    if (live.redraw_timer) {
        g_source_remove(live.redraw_timer);
        live.redraw_timer = 0;
    }
    if (live.connected) {
        spnav_close();
        live.connected = false;
    }
    live.axes = {};
    live.dev_name.clear();
    live.dev_buttons = live.dev_axes = -1;
    liveUpdateStatus();
}

static bool liveConnect();

static gboolean onLiveRetry(gpointer) {
    if (liveConnect()) {
        live.retry_timer = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void liveScheduleRetry() {
    if (!live.retry_timer) live.retry_timer = g_timeout_add_seconds(2, onLiveRetry, nullptr);
}

// Connection lost (spacenavd stopped/restarted): clean up, retry later.
static gboolean liveLost() {
    live.fd_watch = 0;  // the source is removed by G_SOURCE_REMOVE
    liveDisconnect();
    liveScheduleRetry();
    return G_SOURCE_REMOVE;
}

static gboolean onLiveFd(gint fd, GIOCondition cond, gpointer) {
    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) return liveLost();
    spnav_event ev;
    int n = 0;
    bool dev_changed = false;
    while (spnav_poll_event(&ev)) {
        ++n;
        if (ev.type == SPNAV_EVENT_MOTION) {
            live.axes = {ev.motion.x, ev.motion.y, ev.motion.z,
                         ev.motion.rx, ev.motion.ry, ev.motion.rz};
            live.dirty = true;
            if (live.on_motion) live.on_motion(live.axes);
        } else if (ev.type == SPNAV_EVENT_DEV) {
            dev_changed = true;
        }
    }
    if (n == 0) {
        // Readable but no event: did spacenavd close the connection (EOF)?
        char c;
        if (recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0) return liveLost();
    }
    if (dev_changed) {
        liveQueryDevice();
        liveUpdateStatus();
    }
    return G_SOURCE_CONTINUE;
}

// ~30 Hz: only redraw if motion data arrived since the last time.
static gboolean onLiveRedraw(gpointer) {
    if (live.dirty) {
        live.dirty = false;
        for (GtkWidget *v : live.views) gtk_widget_queue_draw(v);
    }
    return G_SOURCE_CONTINUE;
}

static bool liveConnect() {
    if (live.connected) return true;
    if (spnav_open() == -1) return false;
    int fd = spnav_fd();
    if (fd < 0) {
        spnav_close();
        return false;
    }
    spnav_client_name("dofscroll-tray (axes display)");
    live.connected = true;
    live.axes = {};
    liveQueryDevice();
    live.fd_watch = g_unix_fd_add(fd, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), onLiveFd, nullptr);
    live.redraw_timer = g_timeout_add(33, onLiveRedraw, nullptr);
    liveUpdateStatus();
    return true;
}

static void liveStart() {
    if (liveConnect()) return;
    liveUpdateStatus();
    liveScheduleRetry();
}

static void liveStop() {
    if (live.retry_timer) {
        g_source_remove(live.retry_timer);
        live.retry_timer = 0;
    }
    live.on_motion = nullptr;
    live.on_state = nullptr;
    live.views.clear();
    live.status_labels.clear();
    liveDisconnect();
}

// ---------------------------------------------------------------------------
// "Axes" tab - drawing (text colors always from the GTK theme)
// ---------------------------------------------------------------------------

static GdkRGBA themeFg(GtkWidget *w) {
    GdkRGBA c;
    gtk_style_context_get_color(gtk_widget_get_style_context(w), GTK_STATE_FLAG_NORMAL, &c);
    return c;
}

// Accent color of the theme (Adwaita & co. define theme_selected_bg_color).
static GdkRGBA themeAccent(GtkWidget *w) {
    GdkRGBA c;
    if (gtk_style_context_lookup_color(gtk_widget_get_style_context(w), "theme_selected_bg_color", &c))
        return c;
    return GdkRGBA{0.21, 0.52, 0.89, 1.0};
}

static void setRgba(cairo_t *cr, const GdkRGBA &c, double alpha) {
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha * alpha);
}

// Which raw axes channel 'chan' (index into CHANNELS) uses in the selected profile.
static std::array<bool, spn::AX_COUNT> channelAxes(int chan) {
    std::array<bool, spn::AX_COUNT> used{};
    if (chan < 0 || chan >= int(G_N_ELEMENTS(CHANNELS))) return used;
    // chan is range-checked above, [0, G_N_ELEMENTS(CHANNELS)) (false positive)
    // cppcheck-suppress arrayIndexOutOfBoundsCond
    for (const auto &t : (g.cur.*(CHANNELS[chan].member)).mix.terms)
        if (t.weight != 0 && t.axis >= 0 && t.axis < spn::AX_COUNT) used[t.axis] = true;
    return used;
}

static void arrowHead(cairo_t *cr, double x, double y, double ang, double size) {
    cairo_new_path(cr);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x - size * std::cos(ang - 0.45), y - size * std::sin(ang - 0.45));
    cairo_line_to(cr, x - size * std::cos(ang + 0.45), y - size * std::sin(ang + 0.45));
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void drawArrow(cairo_t *cr, double x0, double y0, double x1, double y1) {
    const double ang = std::atan2(y1 - y0, x1 - x0);
    cairo_new_path(cr);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1 - 6 * std::cos(ang), y1 - 6 * std::sin(ang));
    cairo_stroke(cr);
    arrowHead(cr, x1, y1, ang, 10);
}

// Elliptical arc (center cx/cy, semi-axes rx/ry) from a0 to a1 with an arrowhead at the end.
static void drawArcArrow(cairo_t *cr, double cx, double cy, double rx, double ry, double a0,
                         double a1) {
    cairo_new_path(cr);
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1, a0, a1);
    cairo_restore(cr);
    cairo_stroke(cr);
    const double ex = cx + rx * std::cos(a1), ey = cy + ry * std::sin(a1);
    arrowHead(cr, ex, ey, std::atan2(ry * std::cos(a1), -rx * std::sin(a1)), 9);
}

// Two-line label (axis name in bold + description), clamped horizontally into the image.
static void drawAxisLabel(cairo_t *cr, double x, double y, double W, const char *name,
                          const char *desc, const GdkRGBA &fg, bool hl) {
    cairo_text_extents_t e1, e2;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13);
    cairo_text_extents(cr, name, &e1);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    cairo_text_extents(cr, desc, &e2);
    const double w = std::max(e1.x_advance, e2.x_advance);
    x = std::max(4.0, std::min(x, W - 4 - w));

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13);
    setRgba(cr, fg, hl ? 1.0 : 0.8);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, name);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    setRgba(cr, fg, hl ? 0.9 : 0.65);
    cairo_move_to(cr, x, y + 13);
    cairo_show_text(cr, desc);
}

// Static sketch: cap seen obliquely from front above, axes per libspnav convention
// (x right, y up, z toward the user; rx/ry/rz = rotation about x/y/z). The axes of the
// channel selected in the tab are highlighted in the accent color.
static gboolean onAxesDraw(GtkWidget *w, cairo_t *cr, gpointer) {
    using namespace spn;
    const double W = gtk_widget_get_allocated_width(w);
    const double H = gtk_widget_get_allocated_height(w);
    const GdkRGBA fg = themeFg(w), acc = themeAccent(w);
    const auto used = channelAxes(g.axes_chan);
    const double cx = std::round(W * 0.42), cy = std::round(H * 0.47);
    const double capRx = 70, capRy = 22, capH = 30;

    // Cap: side + lid
    cairo_new_path(cr);
    cairo_move_to(cr, cx - capRx, cy);
    cairo_line_to(cr, cx - capRx, cy + capH);
    cairo_save(cr);
    cairo_translate(cr, cx, cy + capH);
    cairo_scale(cr, capRx, capRy);
    cairo_arc_negative(cr, 0, 0, 1, M_PI, 0);
    cairo_restore(cr);
    cairo_line_to(cr, cx + capRx, cy);
    cairo_close_path(cr);
    setRgba(cr, fg, 0.08);
    cairo_fill_preserve(cr);
    setRgba(cr, fg, 0.4);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    cairo_new_path(cr);
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, capRx, capRy);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr);
    setRgba(cr, fg, 0.14);
    cairo_fill_preserve(cr);
    setRgba(cr, fg, 0.5);
    cairo_stroke(cr);

    auto style = [&](Axis a) {
        if (used[a]) {
            setRgba(cr, acc, 1.0);
            cairo_set_line_width(cr, 3.0);
        } else {
            setRgba(cr, fg, 0.55);
            cairo_set_line_width(cr, 1.6);
        }
    };

    // Linear axes
    const double xEnd = cx + std::min(175.0, W * 0.34);
    const double yEnd = cy - 125;
    const double zx = cx - 115, zy = cy + 100;
    style(AX_X);
    drawArrow(cr, cx, cy, xEnd, cy);
    style(AX_Y);
    drawArrow(cr, cx, cy, cx, yEnd);
    style(AX_Z);
    drawArrow(cr, cx, cy, zx, zy);

    // Rotations: ry about the vertical axis, rx about x, rz about z
    style(AX_RY);
    drawArcArrow(cr, cx, cy - 80, 30, 9, -0.25, M_PI + 0.9);
    style(AX_RX);
    drawArcArrow(cr, xEnd - 40, cy, 8, 22, -M_PI * 0.7, M_PI * 0.6);
    style(AX_RZ);
    drawArcArrow(cr, cx + (zx - cx) * 0.72, cy + (zy - cy) * 0.72, 17, 17, -M_PI * 0.85, M_PI * 0.55);

    // Labels (text color from the theme)
    drawAxisLabel(cr, cx + 10, yEnd + 8, W, "y", _("+ up: lift / press"), fg, used[AX_Y]);
    drawAxisLabel(cr, cx - 190, cy - 92, W, "ry", _("Twist the cap"), fg, used[AX_RY]);
    drawAxisLabel(cr, xEnd - 60, cy - 40, W, "rx", _("Tilt forward/back"), fg, used[AX_RX]);
    drawAxisLabel(cr, cx + capRx + 12, cy + 22, W, "x", _("+ right: push sideways"), fg, used[AX_X]);
    drawAxisLabel(cr, cx - 40, cy + capH + capRy + 20, W, "rz", _("Tilt sideways"), fg, used[AX_RZ]);
    drawAxisLabel(cr, zx - 110, zy + 16, W, "z", _("+ toward user: push forward/back"), fg, used[AX_Z]);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    setRgba(cr, fg, 0.55);
    const char *cap = _("View: obliquely from front above (user in front)");
    cairo_text_extents_t e;
    cairo_text_extents(cr, cap, &e);
    cairo_move_to(cr, std::max(4.0, W - 6 - e.x_advance), 12);
    cairo_show_text(cr, cap);
    return FALSE;
}

// Live bars: per axis the deflection from the center, range -350..350.
static gboolean onBarsDraw(GtkWidget *w, cairo_t *cr, gpointer) {
    const double W = gtk_widget_get_allocated_width(w);
    const double H = gtk_widget_get_allocated_height(w);
    const GdkRGBA fg = themeFg(w), acc = themeAccent(w);
    const auto used = channelAxes(g.axes_chan);
    const double dim = live.connected ? 1.0 : 0.4;
    const double rowH = H / spn::AX_COUNT;
    const double x0 = 40, x1 = std::max(x0 + 20, W - 60);
    const double mid = std::round((x0 + x1) / 2) + 0.5, half = (x1 - x0) / 2;
    const double bh = std::min(14.0, rowH - 8);

    for (int a = 0; a < spn::AX_COUNT; ++a) {
        const double yc = a * rowH + rowH / 2;
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               used[a] ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        setRgba(cr, fg, dim);
        cairo_move_to(cr, 6, yc + 4);
        cairo_show_text(cr, spn::axisName(spn::Axis(a)));

        cairo_new_path(cr);
        setRgba(cr, fg, 0.12 * dim);
        cairo_rectangle(cr, x0, yc - bh / 2, x1 - x0, bh);
        cairo_fill(cr);

        const int v = std::max(-350, std::min(350, live.axes[a]));
        const double len = v / 350.0 * half;
        setRgba(cr, used[a] ? acc : fg, (used[a] ? 1.0 : 0.6) * dim);
        cairo_rectangle(cr, len >= 0 ? mid : mid + len, yc - bh / 2 + 1, std::fabs(len), bh - 2);
        cairo_fill(cr);

        setRgba(cr, fg, 0.5 * dim);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, mid, yc - bh / 2 - 2);
        cairo_line_to(cr, mid, yc + bh / 2 + 2);
        cairo_stroke(cr);

        char buf[16];
        snprintf(buf, sizeof(buf), "%+d", live.axes[a]);
        cairo_text_extents_t e;
        cairo_text_extents(cr, buf, &e);
        setRgba(cr, fg, dim);
        cairo_move_to(cr, W - 6 - e.x_advance, yc + 4);
        cairo_show_text(cr, buf);
    }
    return FALSE;
}

static GtkWidget *newBarsView() {
    GtkWidget *bars = gtk_drawing_area_new();
    gtk_widget_set_size_request(bars, -1, 6 * 26);
    g_signal_connect(bars, "draw", G_CALLBACK(onBarsDraw), nullptr);
    trackIn(&live.views, bars);
    return bars;
}

static GtkWidget *newLiveStatusLabel() {
    GtkWidget *l = gtk_label_new(liveStatusText().c_str());
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
    trackIn(&live.status_labels, l);
    return l;
}

static std::string mixText(int chan) {
    std::string m = spn::mixToString((g.cur.*(CHANNELS[chan].member)).mix);
    return m.empty() ? std::string(_("(off)")) : m;
}

// ---------------------------------------------------------------------------
// "Learn axes" wizard (modal, uses the same live reader)
// ---------------------------------------------------------------------------

struct WizStepDesc {
    const char *instruction;
    const char *meaning;
};

static const WizStepDesc WIZ_STEPS[spn::wiz::ROLE_COUNT] = {
    {N_("Pull the cap toward you, then push it forward - level, without tilting. "
        "Toward you first!"),
     N_("Toward you = positive (scroll down).")},
    {N_("Tilt the cap toward you, then forward (tilt the top, without pushing). "
        "Toward you first!"),
     N_("Tilting toward you = positive (scroll down). Optional - can be skipped.")},
    {N_("Push the cap to the right and back - level, without tilting. Right first!"),
     N_("Right = positive (scroll right).")},
    {N_("Tilt the cap sideways: tilt to the right and back. Right first!"),
     N_("Tilting right = positive (scroll right). Optional - can be skipped.")},
    {N_("Twist the cap clockwise (seen from above) and back. Clockwise first!"),
     N_("Clockwise = positive (zoom in).")},
};

constexpr int WIZ_MEASURE_MS = 3000;
enum { WIZ_RESP_START = 1, WIZ_RESP_SKIP, WIZ_RESP_BACK, WIZ_RESP_NEXT, WIZ_RESP_APPLY };

struct Wizard {
    GtkWidget *dlg = nullptr;
    GtkWidget *step_lbl = nullptr, *steps_bar = nullptr, *instr = nullptr, *meaning = nullptr;
    GtkWidget *count_bar = nullptr, *live_box = nullptr, *result = nullptr;
    int step = 0;  // 0..ROLE_COUNT-1 = measuring steps, ROLE_COUNT = result page
    bool measuring = false;
    gint64 t0 = 0;
    guint timer = 0;
    std::string note;  // one-time message (e.g. connection lost)
    std::vector<spn::RawAxes> samples;
    std::array<spn::wiz::StepResult, spn::wiz::ROLE_COUNT> res{};
    std::array<bool, spn::wiz::ROLE_COUNT> done{}, skipped{};
    spn::wiz::WizardResult final;
};

static Wizard wizUi;

static GtkWidget *wizButton(int resp) {
    return gtk_dialog_get_widget_for_response(GTK_DIALOG(wizUi.dlg), resp);
}

static std::string wizResultText() {
    using namespace spn::wiz;
    const WizardResult &f = wizUi.final;
    std::string t;
    if (f.ok) {
        t = std::string(_("Detected mapping:")) + "\n"
            "    Scroll V = " + f.mix_v + "\n"
            "    Scroll H = " + f.mix_h + "\n"
            "    Zoom = " + f.mix_zoom + "\n\n" +
            sfmt(_("\"Apply\" sets these axis mixes in the profile \"%s\" and resets the "
                   "direction switches (\"Invert direction\") to normal."),
                 sectionLabel(g.ps.sections[g.cur_idx]).c_str());
    } else {
        t = _("The mapping is incomplete and cannot be applied.");
    }
    for (const auto &e : f.errors) t += "\n\n" + sfmt(_("Error: %s"), e.c_str());
    for (const auto &w : f.warnings) t += "\n\n" + sfmt(_("Warning: %s"), w.c_str());
    return t;
}

static void wizUpdateUi() {
    using namespace spn::wiz;
    if (!wizUi.dlg) return;
    const bool result_page = wizUi.step >= ROLE_COUNT;
    const int s = std::min(wizUi.step, ROLE_COUNT - 1);

    const std::string step_txt =
        result_page ? std::string()
                    : sfmt(_("Step %d of %d: %s%s"), wizUi.step + 1, int(ROLE_COUNT),
                           _(roleTitle(wizUi.step)),
                           roleOptional(wizUi.step) ? _(" (optional)") : "");
    gchar *head = g_markup_printf_escaped("<b>%s</b>", result_page ? _("Result") : step_txt.c_str());
    gtk_label_set_markup(GTK_LABEL(wizUi.step_lbl), head);
    g_free(head);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(wizUi.steps_bar), double(wizUi.step) / ROLE_COUNT);

    gtk_widget_set_visible(wizUi.instr, !result_page);
    gtk_widget_set_visible(wizUi.meaning, !result_page);
    gtk_widget_set_visible(wizUi.count_bar, !result_page);
    gtk_widget_set_visible(wizUi.live_box, !result_page);

    std::string msg;
    if (result_page) {
        msg = wizResultText();
    } else {
        gchar *ins = g_markup_printf_escaped("<big>%s</big>", _(WIZ_STEPS[s].instruction));
        gtk_label_set_markup(GTK_LABEL(wizUi.instr), ins);
        g_free(ins);
        gtk_label_set_text(GTK_LABEL(wizUi.meaning), _(WIZ_STEPS[s].meaning));

        GtkProgressBar *cb = GTK_PROGRESS_BAR(wizUi.count_bar);
        if (wizUi.measuring) {
            double left = std::max(0.0, WIZ_MEASURE_MS / 1000.0 -
                                            static_cast<double>(g_get_monotonic_time() - wizUi.t0) / 1e6);
            const std::string m = sfmt(_("Measuring - move now! %.1f s left"), left);
            gtk_progress_bar_set_text(cb, m.c_str());
            gtk_progress_bar_set_fraction(cb, 1.0 - left * 1000.0 / WIZ_MEASURE_MS);
        } else if (wizUi.done[s]) {
            gtk_progress_bar_set_text(cb, _("Measurement complete"));
            gtk_progress_bar_set_fraction(cb, 1.0);
        } else {
            gtk_progress_bar_set_text(cb, live.connected
                                              ? _("Ready - click \"Start\", then move (3 s)")
                                              : _("No connection to spacenavd"));
            gtk_progress_bar_set_fraction(cb, 0.0);
        }
        if (!wizUi.note.empty()) msg = wizUi.note;
        else if (wizUi.done[s]) msg = wizUi.res[s].message;
        else if (wizUi.skipped[s]) msg = _("Skipped.");
    }
    gtk_label_set_text(GTK_LABEL(wizUi.result), msg.c_str());

    GtkWidget *start = wizButton(WIZ_RESP_START), *skip = wizButton(WIZ_RESP_SKIP);
    GtkWidget *back = wizButton(WIZ_RESP_BACK), *next = wizButton(WIZ_RESP_NEXT);
    GtkWidget *apply = wizButton(WIZ_RESP_APPLY);
    gtk_widget_set_visible(start, !result_page);
    gtk_button_set_label(GTK_BUTTON(start), wizUi.done[s] ? _("_Measure again") : _("_Start"));
    gtk_widget_set_sensitive(start, !wizUi.measuring && live.connected);
    gtk_widget_set_visible(skip, !result_page && roleOptional(wizUi.step));
    gtk_widget_set_sensitive(skip, !wizUi.measuring);
    gtk_widget_set_sensitive(back, wizUi.step > 0 && !wizUi.measuring);
    gtk_widget_set_visible(next, !result_page);
    gtk_widget_set_sensitive(next, !wizUi.measuring && ((wizUi.done[s] && wizUi.res[s].valid) ||
                                                      wizUi.skipped[s]));
    gtk_widget_set_visible(apply, result_page);
    gtk_widget_set_sensitive(apply, result_page && wizUi.final.ok);
}

static void wizStopMeasuring() {
    if (wizUi.timer) {
        g_source_remove(wizUi.timer);
        wizUi.timer = 0;
    }
    wizUi.measuring = false;
    live.on_motion = nullptr;
}

static gboolean onWizTick(gpointer) {
    if ((g_get_monotonic_time() - wizUi.t0) / 1000 < WIZ_MEASURE_MS) {
        wizUpdateUi();
        return G_SOURCE_CONTINUE;
    }
    wizUi.timer = 0;  // the source ends with G_SOURCE_REMOVE
    wizStopMeasuring();
    wizUi.res[wizUi.step] = spn::wiz::analyzeStep(wizUi.samples);
    wizUi.done[wizUi.step] = true;
    wizUi.skipped[wizUi.step] = false;
    wizUpdateUi();
    return G_SOURCE_REMOVE;
}

static void wizStartMeasuring() {
    if (!live.connected || wizUi.step >= spn::wiz::ROLE_COUNT) return;
    wizUi.samples.clear();
    wizUi.note.clear();
    wizUi.done[wizUi.step] = false;
    wizUi.measuring = true;
    wizUi.t0 = g_get_monotonic_time();
    live.on_motion = [](const spn::RawAxes &a) { wizUi.samples.push_back(a); };
    wizUi.timer = g_timeout_add(50, onWizTick, nullptr);
    wizUpdateUi();
}

static void wizGoto(int step) {
    using namespace spn::wiz;
    wizUi.note.clear();
    wizUi.step = std::max(0, std::min(step, int(ROLE_COUNT)));
    if (wizUi.step == ROLE_COUNT) {
        wizUi.final = evaluate(wizUi.res, wizUi.skipped);
        // Zoom can never engage if its axis is also a "lateral axis".
        if (!wizUi.skipped[ROLE_TWIST] && wizUi.res[ROLE_TWIST].valid) {
            int ax = wizUi.res[ROLE_TWIST].axis;
            for (spn::Axis l : g.cur.zoom_lateral)
                if (l == ax)
                    wizUi.final.warnings.push_back(sfmt(
                        _("The twist axis %s is listed under \"Zoom must dominate against\" for "
                          "zoom - please deselect it there, otherwise zoom never engages."),
                        spn::axisName(l)));
        }
    }
    wizUpdateUi();
}

static void wizApply() {
    const spn::wiz::WizardResult &f = wizUi.final;
    if (!f.ok) return;
    spn::Mix v, h, z;
    std::string err;
    if (!spn::parseMix(f.mix_v, &v, &err) || !spn::parseMix(f.mix_h, &h, &err) ||
        !spn::parseMix(f.mix_zoom, &z, &err)) {
        wizUi.final.errors.push_back(sfmt(_("Internal error while applying: %s"), err.c_str()));
        wizUi.final.ok = false;
        wizUpdateUi();
        return;
    }
    g.cur.scroll_v.mix = v;
    g.cur.scroll_h.mix = h;
    g.cur.zoom.mix = z;
    g.cur.scroll_v.sign = g.cur.scroll_h.sign = g.cur.zoom.sign = 1;
    commit();   // into the section of the selected profile, scheduleApply()
    refresh();  // update UI fields (axis mix, invert)
    gtk_widget_destroy(wizUi.dlg);
}

static void onWizResponse(GtkDialog *, gint resp, gpointer) {
    switch (resp) {
    case WIZ_RESP_START: wizStartMeasuring(); break;
    case WIZ_RESP_SKIP:
        if (spn::wiz::roleOptional(wizUi.step)) {
            wizStopMeasuring();
            wizUi.skipped[wizUi.step] = true;
            wizUi.done[wizUi.step] = false;
            wizGoto(wizUi.step + 1);
        }
        break;
    case WIZ_RESP_BACK: wizStopMeasuring(); wizGoto(wizUi.step - 1); break;
    case WIZ_RESP_NEXT: wizGoto(wizUi.step + 1); break;
    case WIZ_RESP_APPLY: wizApply(); break;
    default: gtk_widget_destroy(wizUi.dlg); break;  // cancel, Esc, window close
    }
}

static void wizOnLiveState() {
    if (wizUi.measuring && !live.connected) {
        wizStopMeasuring();
        wizUi.note = _("Connection to spacenavd lost - measurement aborted.");
    }
    wizUpdateUi();
}

// Pause the daemon output while the mouse is evaluated here: "Axes" tab with the
// window active, or wizard open. Heartbeat file: see common.hpp; it is touched as long
// as the condition holds, otherwise removed.
static GtkWidget *suspendNotebook = nullptr;
static guint suspend_timer = 0;
static bool suspend_file_made = false;

static void updateSuspend(bool force_remove = false) {
    const std::string path = spn::suspendFilePath();
    if (path.empty()) return;
    bool want = !force_remove && wizUi.dlg != nullptr;
    if (!want && g.window && suspendNotebook && gtk_window_is_active(GTK_WINDOW(g.window)))
        want = gtk_notebook_get_current_page(GTK_NOTEBOOK(suspendNotebook)) == 1;
    if (force_remove) {
        unlink(path.c_str());
        suspend_file_made = false;
    } else if (want) {
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            futimens(fd, nullptr);
            close(fd);
            suspend_file_made = true;
        }
    } else if (suspend_file_made) {
        unlink(path.c_str());
        suspend_file_made = false;
    }
}

static void showAxisWizard() {
    if (wizUi.dlg) {
        gtk_window_present(GTK_WINDOW(wizUi.dlg));
        return;
    }
    wizUi = Wizard{};
    GtkWidget *dlg = gtk_dialog_new();
    wizUi.dlg = dlg;
    gtk_window_set_title(GTK_WINDOW(dlg), _("Learn axes"));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(g.window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 540, -1);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Back"), WIZ_RESP_BACK);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Skip"), WIZ_RESP_SKIP);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Start"), WIZ_RESP_START);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Next"), WIZ_RESP_NEXT);
    gtk_dialog_add_button(GTK_DIALOG(dlg), _("_Apply"), WIZ_RESP_APPLY);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    gtk_box_set_spacing(GTK_BOX(area), 8);

    wizUi.step_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(wizUi.step_lbl), 0.0);
    wizUi.steps_bar = gtk_progress_bar_new();
    wizUi.instr = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(wizUi.instr), TRUE);
    gtk_label_set_xalign(GTK_LABEL(wizUi.instr), 0.0);
    gtk_label_set_max_width_chars(GTK_LABEL(wizUi.instr), 55);
    wizUi.meaning = hint("");
    wizUi.count_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(wizUi.count_bar), TRUE);
    wizUi.live_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(wizUi.live_box), newBarsView(), FALSE, FALSE, 0);
    GtkWidget *st = newLiveStatusLabel();
    gtk_style_context_add_class(gtk_widget_get_style_context(st), GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(wizUi.live_box), st, FALSE, FALSE, 0);
    wizUi.result = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(wizUi.result), TRUE);
    gtk_label_set_xalign(GTK_LABEL(wizUi.result), 0.0);
    gtk_label_set_max_width_chars(GTK_LABEL(wizUi.result), 60);
    gtk_label_set_selectable(GTK_LABEL(wizUi.result), TRUE);

    for (GtkWidget *w : {wizUi.step_lbl, wizUi.steps_bar, wizUi.instr, wizUi.meaning, wizUi.count_bar,
                         wizUi.live_box, wizUi.result})
        gtk_box_pack_start(GTK_BOX(area), w, FALSE, FALSE, 0);

    g_signal_connect(dlg, "response", G_CALLBACK(onWizResponse), nullptr);
    g_signal_connect(dlg, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) {
        wizStopMeasuring();
        live.on_state = nullptr;
        wizUi = Wizard{};
    }), nullptr);
    live.on_state = wizOnLiveState;
    updateSuspend();

    gtk_widget_show_all(dlg);
    wizUpdateUi();
}

static GtkWidget *buildAxesPage() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    // 1. Sketch + legend
    GtkWidget *grid = newGrid();
    int row = 0;
    GtkWidget *combo = noWheelChange(gtk_combo_box_text_new());
    for (const auto &c : CHANNELS) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _(c.title));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), g.axes_chan);
    g_signal_connect(combo, "changed", G_CALLBACK(+[](GtkComboBox *cb, gpointer) {
        int i = gtk_combo_box_get_active(cb);
        if (i >= 0) g.axes_chan = i;
        updateSensitivity();  // redraw sketch, legend and bars
    }), nullptr);
    labeled(grid, row++, _("Highlight channel"), combo,
            _("The axes of this channel (from its axis mix in the selected profile) "
              "are highlighted in the sketch and the live display"));

    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 300);
    g_signal_connect(area, "draw", G_CALLBACK(onAxesDraw), nullptr);
    g.graphs.push_back(area);
    gtk_grid_attach(GTK_GRID(grid), area, 0, row++, 2, 1);

    GtkWidget *legend = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(legend), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(legend), TRUE);
    gtk_grid_attach(GTK_GRID(grid), legend, 0, row++, 2, 1);
    g.sensitivity_rules.push_back([legend] {
        std::string m = mixText(g.axes_chan);
        std::string all = sfmt(_("Scroll V: %s     Scroll H: %s     Zoom: %s"), mixText(0).c_str(),
                               mixText(1).c_str(), mixText(2).c_str());
        gchar *mk = g_markup_printf_escaped(
            _("<b>%s</b> uses: <b>%s</b>\n<small>%s  (Profile: %s)</small>"),
            _(CHANNELS[g.axes_chan].title), m.c_str(), all.c_str(),
            sectionLabel(g.ps.sections[g.cur_idx]).c_str());
        gtk_label_set_markup(GTK_LABEL(legend), mk);
        g_free(mk);
        for (GtkWidget *v : live.views) gtk_widget_queue_draw(v);
    });
    gtk_grid_attach(GTK_GRID(grid),
                    hint(_("Which direction of an axis counts as positive depends on the device - "
                           "the wizard below determines axes and signs automatically.")),
                    0, row++, 2, 1);
    gtk_box_pack_start(GTK_BOX(vbox), frame(_("SpaceMouse axes"), grid), FALSE, FALSE, 0);

    // 2. Live display
    GtkWidget *lg = newGrid();
    row = 0;
    gtk_grid_attach(GTK_GRID(lg), newLiveStatusLabel(), 0, row++, 2, 1);
    GtkWidget *bars = newBarsView();
    gtk_widget_set_hexpand(bars, TRUE);
    gtk_grid_attach(GTK_GRID(lg), bars, 0, row++, 2, 1);
    gtk_grid_attach(GTK_GRID(lg),
                    hint(_("Raw values from spacenavd, range -350..350 (center = at rest). Own, "
                           "read-only connection - only active while this window is open.")),
                    0, row++, 2, 1);
    gtk_box_pack_start(GTK_BOX(vbox), frame(_("Live display"), lg), FALSE, FALSE, 0);

    // 3. Wizard
    GtkWidget *wg = newGrid();
    row = 0;
    gtk_grid_attach(GTK_GRID(wg),
                    hint(_("The wizard guides you through five short movements (3 s each) and "
                           "derives the axis mixes for vertical scroll, horizontal scroll "
                           "and zoom, including direction.")),
                    0, row++, 2, 1);
    GtkWidget *target = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(target), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(target), TRUE);
    gtk_widget_set_hexpand(target, TRUE);
    g.sensitivity_rules.push_back([target] {
        std::string t = sfmt(_("The result applies to the profile selected above: %s"),
                             sectionLabel(g.ps.sections[g.cur_idx]).c_str());
        gtk_label_set_text(GTK_LABEL(target), t.c_str());
    });
    GtkWidget *btn = gtk_button_new_with_label(_("Learn axes…"));
    g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) { showAxisWizard(); }),
                     nullptr);
    gtk_grid_attach(GTK_GRID(wg), target, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(wg), btn, 1, row++, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), frame(_("Learn axes"), wg), FALSE, FALSE, 0);
    return vbox;
}

// Dialog for the pattern of a new profile. Empty/cancelled -> "".
static std::string askPattern(Section::Kind kind) {
    const bool dev = kind == Section::DEVICE;
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        dev ? _("New device profile") : _("New program profile"), GTK_WINDOW(g.window),
        GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), _("_Cancel"),
        GTK_RESPONSE_CANCEL, _("_Create"), GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    gtk_box_set_spacing(GTK_BOX(area), 8);

    gtk_box_pack_start(GTK_BOX(area),
                       hint(dev ? _("The profile applies when the device name of the SpaceMouse (as "
                                    "spacenavd reports it, e.g. \"SpaceMouse Pro\" or \"SpaceMouse "
                                    "Compact\"; it is in the journal when the daemon starts: "
                                    "journalctl --user -u dofscroll.service) contains this text. "
                                    "Case-insensitive, a substring is enough.")
                                : _("The profile applies when the WM_CLASS of the active window "
                                    "contains this text (\"instance class\" is compared in lowercase, "
                                    "e.g. \"navigator firefox\"). Determine it with: xprop WM_CLASS "
                                    "and then click into the window. Case-insensitive, a substring "
                                    "is enough. Program profiles apply after device profiles (so "
                                    "they win). Programs can also be switched off here (e.g. "
                                    "Blender with its own 3D navigation): create a profile and "
                                    "uncheck \"Control active in this profile\" in it.")),
                       FALSE, FALSE, 0);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), dev ? _("e.g. SpaceMouse Pro") : _("e.g. firefox"));
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);

    // The main window can be closed during gtk_dialog_run: DESTROY_WITH_PARENT then also
    // destroys dlg. Weak pointer instead of a blind gtk_widget_destroy(dlg).
    GtkWidget *guard = dlg;
    g_object_add_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
    std::string result;
    while (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        if (!guard) break;
        std::string p = trim(gtk_entry_get_text(GTK_ENTRY(entry)));
        // ']' and line breaks would break the section line in the file.
        if (!p.empty() && p.find_first_of("]\n\r") == std::string::npos) {
            result = p;
            break;
        }
        markEntry(entry, false, _("Pattern must not be empty or contain ']'"));
    }
    if (guard) {
        g_object_remove_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
        gtk_widget_destroy(dlg);
    } else {
        result.clear();  // window was closed
    }
    return result;
}

static void onNewProfile(GtkButton *, gpointer data) {
    auto kind = Section::Kind(GPOINTER_TO_INT(data));
    std::string pattern = askPattern(kind);
    if (pattern.empty() || !g.window || !g.profile_combo) return;
    size_t idx = g.ps.findOrAdd(kind, pattern);
    if (idx == ProfileSet::npos) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(g.window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "%s", _("Invalid pattern: it must not be empty or contain ']' or a line break."));
        GtkWidget *guard = dlg;
        g_object_add_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
        gtk_dialog_run(GTK_DIALOG(dlg));
        if (guard) {
            g_object_remove_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
            gtk_widget_destroy(dlg);
        }
        return;
    }
    fillProfileCombo();
    gtk_combo_box_set_active(GTK_COMBO_BOX(g.profile_combo), int(idx));  // -> selectProfile
    scheduleApply();
}

static void onDeleteProfile(GtkButton *, gpointer) {
    if (g.cur_idx == 0 || g.cur_idx >= g.ps.sections.size()) return;
    std::string name = sectionLabel(g.ps.sections[g.cur_idx]);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(g.window), GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                            _("Delete profile \"%s\"?"), name.c_str());
    GtkWidget *guard = dlg;
    g_object_add_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (guard) {
        g_object_remove_weak_pointer(G_OBJECT(dlg), reinterpret_cast<gpointer *>(&guard));
        gtk_widget_destroy(dlg);
    }
    // Main window closed in the meantime -> do not touch the g.ps state/combo any more.
    if (resp != GTK_RESPONSE_YES || !g.window || !g.profile_combo ||
        g.cur_idx == 0 || g.cur_idx >= g.ps.sections.size())
        return;
    g.ps.remove(g.cur_idx);
    g.cur_idx = 0;
    fillProfileCombo();
    selectProfile(0);
    scheduleApply();
}

static void onProfileChanged(GtkComboBox *combo, gpointer) {
    if (g.updating || !g.window) return;
    int idx = gtk_combo_box_get_active(combo);
    if (idx >= 0) selectProfile(size_t(idx));
}

// ---------------------------------------------------------------------------
// "Application modes" list: one row per program/device profile, a combo box picks the
// mode (config key MODE of that profile). Rows and choices are built dynamically from
// the profiles and the loaded mode files (see mode_list.hpp).
// ---------------------------------------------------------------------------

struct ModeRowData {
    Section::Kind kind;
    std::string pattern;
    std::vector<std::string> ids;  // mode id per combo entry
};

static void onModeComboChanged(GtkComboBox *cb, gpointer) {
    if (g.updating || !g.window) return;
    auto *rd = static_cast<ModeRowData *>(g_object_get_data(G_OBJECT(cb), "row"));
    const int i = gtk_combo_box_get_active(cb);
    if (!rd || i < 0 || size_t(i) >= rd->ids.size()) return;
    if (!spn::modelist::setRowMode(g.ps, rd->kind, rd->pattern, rd->ids[size_t(i)])) return;
    // The edited section may be the selected one: re-resolve so g.cur has the new MODE.
    g.cur = g.ps.sectionSettings(g.cur_idx);
    refresh();
    scheduleApply();
}

static void rebuildModeList() {
    if (!g.mode_box) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(g.mode_box));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    const std::string dir = spn::modesDir();
    g.modes_stamp = spn::modesStamp(dir);
    std::vector<std::string> warnings;
    const spn::ModeSet ms = spn::loadModes(dir, &warnings);
    for (const auto &w : warnings) fprintf(stderr, "dofscroll-tray: modes: %s\n", w.c_str());

    const auto rows = spn::modelist::buildRows(g.ps);
    if (rows.empty()) {
        gtk_box_pack_start(GTK_BOX(g.mode_box),
                           hint(_("There are no program or device profiles yet. Create one with "
                                  "\"New program profile...\" above; it then appears here.")),
                           FALSE, FALSE, 0);
    } else {
        GtkWidget *grid = newGrid();
        int row = 0;
        for (const auto &rw : rows) {
            GtkWidget *combo = noWheelChange(gtk_combo_box_text_new());
            auto *rd = new ModeRowData{rw.kind, rw.pattern, {}};
            const auto choices = spn::modelist::buildChoices(ms, rw.mode_id);
            for (const auto &c : choices) {
                const std::string label =
                    c.known ? c.label : sfmt(_("%s (missing)"), c.label.c_str());
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label.c_str());
                rd->ids.push_back(c.id);
            }
            g.updating = true;
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo),
                                     spn::modelist::choiceIndex(choices, rw.mode_id));
            g.updating = false;
            g_object_set_data_full(G_OBJECT(combo), "row", rd, +[](gpointer p) {
                delete static_cast<ModeRowData *>(p);
            });
            g_signal_connect(combo, "changed", G_CALLBACK(onModeComboChanged), nullptr);
            const spn::ModeDef *md = ms.find(rw.mode_id);
            if (md && !md->description.empty())
                gtk_widget_set_tooltip_text(combo, md->description.c_str());
            labeled(grid, row++, kindLabel(rw.kind, rw.pattern).c_str(), combo);
        }
        gtk_box_pack_start(GTK_BOX(g.mode_box), grid, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(g.mode_box);
}

static GtkWidget *buildModeList() {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_pack_start(GTK_BOX(box),
                       hint(_("A mode defines what the three channels and the buttons do. Modes "
                              "are loaded from the modes folder; the choice is saved as MODE in "
                              "the profile.")),
                       FALSE, FALSE, 0);
    g.mode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 170);
    gtk_container_add(GTK_CONTAINER(sc), g.mode_box);
    gtk_box_pack_start(GTK_BOX(box), sc, FALSE, FALSE, 0);
    return frame(_("Application modes"), box);
}

static GtkWidget *buildProfileArea() {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new(_("Profile")), FALSE, FALSE, 0);
    g.profile_combo = noWheelChange(gtk_combo_box_text_new());
    gtk_box_pack_start(GTK_BOX(row), g.profile_combo, TRUE, TRUE, 0);
    g_signal_connect(g.profile_combo, "changed", G_CALLBACK(onProfileChanged), nullptr);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    GtkWidget *btns = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(btns), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(btns), FALSE);
    GtkWidget *nd = gtk_button_new_with_label(_("New device profile..."));
    GtkWidget *na = gtk_button_new_with_label(_("New program profile..."));
    g.delete_btn = gtk_button_new_with_label(_("Delete profile"));
    g_signal_connect(nd, "clicked", G_CALLBACK(onNewProfile), GINT_TO_POINTER(Section::DEVICE));
    g_signal_connect(na, "clicked", G_CALLBACK(onNewProfile), GINT_TO_POINTER(Section::APP));
    g_signal_connect(g.delete_btn, "clicked", G_CALLBACK(onDeleteProfile), nullptr);
    gtk_container_add(GTK_CONTAINER(btns), nd);
    gtk_container_add(GTK_CONTAINER(btns), na);
    gtk_container_add(GTK_CONTAINER(btns), g.delete_btn);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    g.profile_info = hint("");
    gtk_box_pack_start(GTK_BOX(box), g.profile_info, FALSE, FALSE, 0);
    return frame(_("Profile"), box);
}

static void installCss() {
    static bool done = false;
    if (done) return;
    done = true;
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css, "entry.spn-invalid { color: #b00000; background-color: #fbe3e3; border-color: #d00000; }",
        -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

static void loadFromDisk() {
    std::vector<std::string> warnings;
    ProfileSet ps;
    if (!spn::loadProfiles(cfgPath(), &ps, &warnings))
        fprintf(stderr, "dofscroll-tray: %s is not readable (not a regular file?) - "
                        "showing default values\n", cfgPath().c_str());
    for (const auto &w : warnings) fprintf(stderr, "dofscroll-tray: %s: %s\n", cfgPath().c_str(), w.c_str());
    g.ps = ps;
    syncDiskState();
}

static void showSettingsWindow() {
    if (g.window) {
        gtk_window_present(GTK_WINDOW(g.window));
        return;
    }
    installCss();
    flushPending();
    loadFromDisk();  // the file may have been edited by hand in the meantime
    g.cur_idx = 0;

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g.window = window;
    gtk_window_set_title(GTK_WINDOW(window), _("dofscroll - Settings"));
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 800);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) {
        if (wizUi.dlg) gtk_widget_destroy(wizUi.dlg);
        if (suspend_timer) g_source_remove(suspend_timer);
        suspend_timer = 0;
        suspendNotebook = nullptr;
        g.window = nullptr;  // from here on onBindChanged/onProfileChanged ignore all signals
        flushPending();      // do not lose a pending change
        updateSuspend();  // remove the heartbeat file
        liveStop();  // own libspnav connection only while the window is open
        g.window = g.profile_combo = g.profile_info = g.delete_btn = g.mode_box = nullptr;
        g.button_spins.clear();
        g.reset_btns.clear();
        g.graphs.clear();
        // Children still alive: disconnect signal handlers before the binding objects
        // are freed (otherwise late "changed" emissions hit freed memory).
        for (auto &b : g.binds)
            if (b->widget) g_signal_handlers_disconnect_by_data(b->widget, b.get());
        g.binds.clear();
        g.sensitivity_rules.clear();
    }), nullptr);

    // Profile selection at the top, visible for both tabs (the wizard in the "Axes" tab
    // writes into the profile selected here).
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 12);
    gtk_container_add(GTK_CONTAINER(window), outer);
    gtk_box_pack_start(GTK_BOX(outer), buildProfileArea(), FALSE, FALSE, 0);

    GtkWidget *notebook = gtk_notebook_new();
    suspendNotebook = notebook;
    gtk_box_pack_start(GTK_BOX(outer), notebook, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer), buildModeList(), FALSE, FALSE, 0);
    g_signal_connect_after(notebook, "switch-page", G_CALLBACK(+[](GtkNotebook *, GtkWidget *, guint, gpointer) {
        updateSuspend();
    }), nullptr);
    g_signal_connect(window, "notify::is-active", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) {
        updateSuspend();
    }), nullptr);
    suspend_timer = g_timeout_add(150, +[](gpointer) -> gboolean {
        updateSuspend();
        return G_SOURCE_CONTINUE;
    }, nullptr);

    // Scrollbar, so the window stays usable on small screens.
    auto addPage = [notebook](GtkWidget *content, const char *title) {
        GtkWidget *scroller = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                       GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroller), content);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroller, gtk_label_new(title));
    };

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    // At the very top: control on/off per profile (ENABLED). Off -> all other settings of
    // this tab are greyed out (they have no effect then anyway); the checkbox stays usable.
    GtkWidget *enabled_check = bindCheck(_("Control active in this profile"),
                                         [] { return g.cur.enabled; },
                                         [](bool on) { g.cur.enabled = on; });
    gtk_widget_set_tooltip_text(
        enabled_check,
        _("Off = the SpaceMouse generates no scroll/zoom/key events in this profile, e.g. "
          "for programs with their own 3D navigation such as Blender: create a program "
          "profile and switch this off"));
    gtk_box_pack_start(GTK_BOX(vbox), enabled_check, FALSE, FALSE, 0);
    GtkWidget *rest = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(rest), buildChannel(&CHANNELS[0], false), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rest), buildChannel(&CHANNELS[1], false), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rest), buildChannel(&CHANNELS[2], true), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rest), buildGeneral(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), rest, FALSE, FALSE, 0);
    g.sensitivity_rules.push_back([rest] { gtk_widget_set_sensitive(rest, g.cur.enabled); });
    addPage(vbox, _("Channels"));
    addPage(buildAxesPage(), _("Axes"));

    fillProfileCombo();
    selectProfile(0);
    gtk_widget_show_all(window);
    liveStart();
}

// ---------------------------------------------------------------------------
// Tray menu
// ---------------------------------------------------------------------------

static void quitNow() {
    flushPending();
    g.window = nullptr;
    updateSuspend(true);  // remove the heartbeat file without fail (even with the wizard open)
    // Close an open wizard: gtk_main_quit from a nested gtk_dialog_run would otherwise
    // only take effect after the dialog is closed.
    if (wizUi.dlg) gtk_dialog_response(GTK_DIALOG(wizUi.dlg), GTK_RESPONSE_CANCEL);
    GList *tops = gtk_window_list_toplevels();
    for (GList *l = tops; l; l = l->next)
        if (GTK_IS_DIALOG(l->data) && l->data != wizUi.dlg)
            gtk_dialog_response(GTK_DIALOG(l->data), GTK_RESPONSE_CANCEL);
    g_list_free(tops);
    gtk_main_quit();
}

static void onQuit(GtkMenuItem *, gpointer) { quitNow(); }

static gboolean onTermSignal(gpointer) {
    quitNow();
    return G_SOURCE_REMOVE;
}

static void onSettings(GtkMenuItem *, gpointer) { showSettingsWindow(); }

static void onOpenConfig(GtkMenuItem *, gpointer) {
    flushPending();
    // xdg-open fails on non-existent files -> create it with the current (default)
    // values if needed (saveProfiles creates the directory).
    std::string path = cfgPath();
    if (access(path.c_str(), F_OK) != 0) {
        ProfileSet ps;
        spn::loadProfiles(path, &ps, nullptr);
        ps.setSectionFromSettings(0, ps.sectionSettings(0));
        std::string err;
        if (!spn::saveProfiles(path, ps, &err)) {
            fprintf(stderr, "dofscroll-tray: cannot create %s: %s\n", path.c_str(), err.c_str());
            return;
        }
        path = cfgPath();  // possibly a new path after creating
    }
    // Absolute path: a relative path / one starting with '-' (e.g. XDG_CONFIG_HOME="-x")
    // would be read as an option by xdg-open.
    std::string open_path = path;
    if (open_path.empty() || open_path[0] != '/') {
        char cwd[4096];
        open_path = (getcwd(cwd, sizeof(cwd)) ? std::string(cwd) : std::string()) + "/" + open_path;
    }
    // The editor/file manager must NOT run in the tray's sandbox: there ~/.config is
    // read-only (only the own config directory is writable), and e.g. kde-open5 aborts
    // with 'Configuration file ... not writable'; an editor started there also could not
    // save anything outside of it. So start it as a separate transient user service via
    // systemd-run (KillMode=process: an editor started by xdg-open survives its end).
    // Without systemd-run: directly.
    const std::string sdrun = resolveExe("systemd-run");
    if (sdrun[0] == '/') {
        spawnDetached({sdrun, "--user", "--collect", "--quiet", "-p", "KillMode=process", "--",
                       resolveExe("xdg-open"), open_path});
    } else {
        spawnDetached({"xdg-open", open_path});
    }
}

// ---------------------------------------------------------------------------
// Runtime files of the daemon (status, log) - read safely. Their contents are foreign
// data: display only as plain text (GTK labels without markup, length limited).
// ---------------------------------------------------------------------------

// Reads at most the last tail_max bytes. Refuses symlinks, non-regular files, foreign
// owners, group/other permissions and files > size_max. mtime/size optional.
static bool readRuntimeFile(const std::string &path, size_t size_max, size_t tail_max,
                            std::string *out, off_t *size_out = nullptr, long long *mtime_out = nullptr) {
    out->clear();
    if (path.empty()) return false;
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
              (st.st_mode & (S_IRWXG | S_IRWXO)) == 0 && st.st_size >= 0 &&
              size_t(st.st_size) <= size_max;
    if (ok) {
        if (size_out) *size_out = st.st_size;
        if (mtime_out) *mtime_out = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
        off_t off = size_t(st.st_size) > tail_max ? off_t(st.st_size - off_t(tail_max)) : 0;
        size_t want = size_t(st.st_size) - size_t(off);
        out->resize(want);
        size_t got = 0;
        while (got < want) {
            ssize_t n = pread(fd, &(*out)[got], want - got, off + off_t(got));
            if (n <= 0) break;
            got += size_t(n);
        }
        out->resize(got);
        if (off > 0) {  // started in the middle of a line: discard the truncated first line
            size_t nl = out->find('\n');
            *out = nl == std::string::npos ? std::string() : out->substr(nl + 1);
        }
    }
    close(fd);
    return ok;
}

// Sanitize foreign text for display: control characters -> space (with keep_nl, \n and
// \t stay), enforce valid UTF-8, truncate to max_chars characters.
static std::string displayText(const std::string &in, size_t max_chars, bool keep_nl = false) {
    std::string t = in;
    for (char &c : t) {
        unsigned char u = (unsigned char)c;
        if ((u < 0x20 || u == 0x7f) && !(keep_nl && (c == '\n' || c == '\t'))) c = ' ';
    }
    gchar *valid = g_utf8_make_valid(t.data(), gssize(t.size()));
    std::string r = valid;
    g_free(valid);
    if (g_utf8_strlen(r.c_str(), -1) > glong(max_chars)) {
        r.resize(size_t(g_utf8_offset_to_pointer(r.c_str(), glong(max_chars)) - r.c_str()));
        r += "...";
    }
    return r;
}

struct DaemonStatus {
    bool valid = false;  // file readable, plausible and fresh
    std::string profile, device, app, mode;
    bool enabled = true, paused = false;
    int devices_seen = 0;
};

static DaemonStatus readDaemonStatus() {
    DaemonStatus d;
    std::string raw;
    if (!readRuntimeFile(spn::statusFilePath(), spn::kStatusMaxBytes, spn::kStatusMaxBytes, &raw))
        return d;
    long long updated = 0;
    bool have_updated = false;
    std::istringstream is(raw);
    std::string line;
    while (std::getline(is, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "profile") d.profile = v;
        else if (k == "device") d.device = v;
        else if (k == "app") d.app = v;
        else if (k == "enabled") d.enabled = v != "0";
        else if (k == "paused") d.paused = v == "1";
        else if (k == "devices_seen") d.devices_seen = int(std::strtol(v.c_str(), nullptr, 10));
        else if (k == "mode") d.mode = v;  // optional: daemons without this key are fine
        else if (k == "updated") { updated = std::strtoll(v.c_str(), nullptr, 10); have_updated = true; }
    }
    long long age = (long long)time(nullptr) - updated;
    d.valid = have_updated && age <= 5 && age >= -5;
    return d;
}

// Display name of the active mode. Uses the daemon's "mode" status key if present, else
// resolves MODE from the config for the reported device/program. Config and mode files
// are re-read only when they change.
static std::string activeModeLabel(const DaemonStatus &d) {
    static ProfileSet cfg;
    static std::string cfg_content;
    static bool cfg_loaded = false;
    static spn::ModeSet modes;
    static std::string modes_stamp;
    static bool modes_loaded = false;

    const std::string dir = spn::modesDir();
    const std::string st = spn::modesStamp(dir);
    if (!modes_loaded || st != modes_stamp) {
        modes = spn::loadModes(dir, nullptr);
        modes_stamp = st;
        modes_loaded = true;
    }
    std::string id = d.mode;
    if (id.empty()) {
        std::string content;
        readFileContent(cfgPath(), &content);
        if (!cfg_loaded || content != cfg_content) {
            cfg = ProfileSet();
            spn::loadProfiles(cfgPath(), &cfg, nullptr);
            cfg_content = content;
            cfg_loaded = true;
        }
        id = cfg.resolve(d.device, d.app).mode;
    }
    return displayText(spn::modelist::modeLabel(modes, id), 40);
}

struct StatusUi {
    AppIndicator *indicator = nullptr;
    GtkWidget *main_item = nullptr, *info_item = nullptr, *warn_item = nullptr;
    std::string last;  // last displayed state (set labels only on change)
};
static StatusUi statusUi;

static gboolean onStatusTick(gpointer) {
    const DaemonStatus d = readDaemonStatus();
    std::string main_t, info_t, warn_t;
    if (!d.valid) {
        main_t = _("Daemon not running / no status data");
    } else {
        main_t = sfmt(_("Profile: %s"),
                      displayText(d.profile.empty() ? "global" : d.profile, 80).c_str());
        if (d.paused) main_t += _(" (paused)");
        if (!d.enabled) main_t += _(" (disabled)");
        const std::string mode = activeModeLabel(d);
        if (!mode.empty()) main_t += "  |  " + sfmt(_("Mode: %s"), mode.c_str());
        if (!d.app.empty()) info_t = sfmt(_("Program: %s"), displayText(d.app, 60).c_str());
        if (!d.device.empty())
            info_t += std::string(info_t.empty() ? "" : "  |  ") +
                      sfmt(_("Device: %s"), displayText(d.device, 60).c_str());
        if (d.devices_seen >= 2)
            warn_t = sfmt(_("%d SpaceMice connected, only '%s' is reported"), d.devices_seen,
                          displayText(d.device, 60).c_str());
    }
    const std::string state = main_t + "\n" + info_t + "\n" + warn_t;
    if (state != statusUi.last) {
        statusUi.last = state;
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusUi.main_item), main_t.c_str());
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusUi.info_item), info_t.c_str());
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusUi.warn_item), warn_t.c_str());
        gtk_widget_set_visible(statusUi.info_item, !info_t.empty());
        gtk_widget_set_visible(statusUi.warn_item, !warn_t.empty());
        if (statusUi.indicator)
            app_indicator_set_title(statusUi.indicator,
                                    (std::string("dofscroll - ") + main_t).c_str());
    }
    return G_SOURCE_CONTINUE;
}

// --- Log window ------------------------------------------------------------

struct LogUi {
    GtkWidget *win = nullptr, *view = nullptr;
    guint timer = 0;
    off_t size = -1;
    long long mtime = -1;
};
static LogUi logUi;

static void logReload(bool force) {
    if (!logUi.win) return;
    std::string raw;
    off_t size = 0;
    long long mtime = 0;
    const bool ok = readRuntimeFile(spn::logFilePath(), spn::kLogMaxBytes, 64 * 1024, &raw, &size, &mtime);
    if (!force && ok && size == logUi.size && mtime == logUi.mtime) return;
    if (!ok && !force && logUi.size == -2) return;  // the notice is already shown
    logUi.size = ok ? size : -2;
    logUi.mtime = ok ? mtime : -2;
    std::string text = ok ? displayText(raw, raw.size(), true)
                          : std::string(_("No log yet (daemon older than this version or not running)"));
    GtkTextView *tv = GTK_TEXT_VIEW(logUi.view);
    GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tv));
    const bool at_bottom = gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) >=
                           gtk_adjustment_get_upper(adj) - 4;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(buf, text.c_str(), -1);
    if (at_bottom) {
        g_idle_add(+[](gpointer) -> gboolean {
            if (logUi.view) {
                GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logUi.view));
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(b, &end);
                GtkTextMark *m = gtk_text_buffer_create_mark(b, nullptr, &end, FALSE);
                gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(logUi.view), m, 0.0, TRUE, 0.0, 1.0);
                gtk_text_buffer_delete_mark(b, m);
            }
            return G_SOURCE_REMOVE;
        }, nullptr);
    }
}

static void onLogCopy(GtkButton *, gpointer) {
    if (!logUi.view) return;
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logUi.view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(b, &start, &end);
    gchar *t = gtk_text_buffer_get_text(b, &start, &end, FALSE);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), t, -1);
    g_free(t);
}

static void showLogWindow() {
    if (logUi.win) {
        gtk_window_present(GTK_WINDOW(logUi.win));
        return;
    }
    logUi = LogUi{};
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    logUi.win = win;
    gtk_window_set_title(GTK_WINDOW(win), _("dofscroll - Log"));
    gtk_window_set_default_size(GTK_WINDOW(win), 760, 480);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    GtkWidget *view = gtk_text_view_new();
    logUi.view = view;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    GtkWidget *sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), view);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(btns), FALSE);
    GtkWidget *refresh = gtk_button_new_with_label(_("Refresh"));
    GtkWidget *copy = gtk_button_new_with_label(_("Copy"));
    GtkWidget *close = gtk_button_new_with_label(_("Close"));
    gtk_box_pack_start(GTK_BOX(btns), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), copy, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(btns), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btns, FALSE, FALSE, 0);
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) { logReload(true); }), nullptr);
    g_signal_connect(copy, "clicked", G_CALLBACK(onLogCopy), nullptr);
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
        if (logUi.win) gtk_widget_destroy(logUi.win);
    }), nullptr);
    g_signal_connect(win, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) {
        if (logUi.timer) g_source_remove(logUi.timer);
        logUi = LogUi{};
    }), nullptr);
    logUi.timer = g_timeout_add_seconds(1, +[](gpointer) -> gboolean {
        logReload(false);
        return G_SOURCE_CONTINUE;
    }, nullptr);
    gtk_widget_show_all(win);
    logReload(true);
}

static void onShowLog(GtkMenuItem *, gpointer) { showLogWindow(); }

static void onRestartDaemon(GtkMenuItem *, gpointer) {
    flushPending();
    restartDaemon();
}

// Own icon (puck + 4-way arrow). The icon directory is searched at run time instead of
// relying on one fixed installed path (a dead symlink under /usr/local once left the
// window with the default icon):
// 1. icons/ next to the resolved binary (= repo, symlink is resolved)
// 2. ~/.local/share/dofscroll-tray/icons  3. /usr/local/share/...
// "input-mouse" remains the fallback name if there is no icon anywhere.
static const char *ICON_NAME = "dofscroll-tray";

static std::string findIconDir() {
    std::vector<std::string> candidates;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string dir = dirname(exe);
        candidates.push_back(dir + "/icons");
        // Binary possibly in a subfolder of the repo (e.g. build/).
        candidates.push_back(dir + "/../icons");
    }
    if (const char *home = getenv("HOME"))
        candidates.push_back(std::string(home) + "/.local/share/dofscroll-tray/icons");
    candidates.push_back("/usr/local/share/dofscroll-tray/icons");
    for (const auto &dir : candidates)
        if (access((dir + "/" + ICON_NAME + ".svg").c_str(), R_OK) == 0) return dir;
    return "";
}

static void addMenuItem(GtkWidget *menu, const char *label, GCallback cb) {
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    g_signal_connect(item, "activate", cb, nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

int main(int argc, char **argv) {
    spn::i18n::init();
    spn::wiz::trHook() = spn::i18n::tr;  // wizard result messages are built GUI-free
    gtk_init(&argc, &argv);
    {
        std::string mlog;
        const bool ok = spn::migrateLegacyConfig(&mlog);
        fprintf(stderr, "dofscroll-tray: config migration: %s%s%s\n", ok ? "ok" : "failed",
                mlog.empty() ? "" : " - ", mlog.c_str());
        // First start: seed example config + modes (the daemon cannot write the config dir).
        std::string slog;
        if (spn::seedDefaults(&slog))
            fprintf(stderr, "dofscroll-tray: seeded defaults: %s\n", slog.c_str());
    }

    // Without this the settings window gets the generic GTK logo in the task bar instead
    // of the own icon, since GtkWindow (unlike AppIndicator) does not automatically get
    // the icon theme path.
    std::string icon_dir = findIconDir();
    if (!icon_dir.empty()) {
        gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), icon_dir.c_str());
        // Directly from file: sets _NET_WM_ICON, which task bars (KDE/X11) use,
        // independent of theme cache/name resolution.
        gtk_window_set_default_icon_from_file((icon_dir + "/" + ICON_NAME + ".svg").c_str(), nullptr);
    } else {
        gtk_window_set_default_icon_name("input-mouse");
    }

    AppIndicator *indicator = app_indicator_new("dofscroll-tray", "input-mouse",
                                                APP_INDICATOR_CATEGORY_HARDWARE);
    if (!icon_dir.empty()) app_indicator_set_icon_theme_path(indicator, icon_dir.c_str());
    app_indicator_set_icon_full(indicator, icon_dir.empty() ? "input-mouse" : ICON_NAME,
                                "dofscroll");
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator, "dofscroll");

    GtkWidget *menu = gtk_menu_new();
    // Status lines at the top (not clickable), fed by onStatusTick from the status file.
    auto addInfoItem = [menu](const char *label) {
        GtkWidget *item = gtk_menu_item_new_with_label(label);
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };
    statusUi.indicator = indicator;
    statusUi.main_item = addInfoItem(_("Determining status..."));
    statusUi.info_item = addInfoItem("");
    statusUi.warn_item = addInfoItem("");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addMenuItem(menu, _("Settings…"), G_CALLBACK(onSettings));
    addMenuItem(menu, _("Open config file"), G_CALLBACK(onOpenConfig));
    addMenuItem(menu, _("Restart daemon"), G_CALLBACK(onRestartDaemon));
    addMenuItem(menu, _("Show log..."), G_CALLBACK(onShowLog));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addMenuItem(menu, _("Quit"), G_CALLBACK(onQuit));
    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
    gtk_widget_set_visible(statusUi.info_item, FALSE);
    gtk_widget_set_visible(statusUi.warn_item, FALSE);
    onStatusTick(nullptr);
    g_timeout_add_seconds(1, onStatusTick, nullptr);

    g_unix_signal_add(SIGTERM, onTermSignal, nullptr);
    g_unix_signal_add(SIGINT, onTermSignal, nullptr);
    gtk_main();
    return 0;
}
