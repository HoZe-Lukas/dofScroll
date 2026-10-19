# Makefile fuer dofscroll (Daemon) und dofscroll-tray (Tray + Einstellungen)
#
# Ziele: all daemon tray test clean install uninstall install-udev install-user-links check-deps
# Beispiele:
#   make                 # beide Binaries im Repo-Root bauen
#   make daemon          # nur Daemon (ohne GTK)
#   make test            # tests/test_*.cpp bauen und ausfuehren
#   make -j4 BUILDDIR=/tmp/b BIN_DIR=/tmp/bin   # Ausgabepfade umbiegen

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -MMD -MP -Isrc

# --- Hardening (standardmaessig fuer alle Binaries und Tests) ---
# Eigene Variablen, damit ein per Kommandozeile gesetztes CXXFLAGS sie nicht
# versehentlich verliert. Abschalten nur bewusst: make HARDEN_CXXFLAGS= HARDEN_LDFLAGS=
# Compilerprobe: Flag nur verwenden, wenn der Compiler es kennt.
cc_supports = $(shell echo 'int main(){return 0;}' | $(CXX) -x c++ $(1) -c -o /dev/null - >/dev/null 2>&1 && echo $(1))
# _FORTIFY_SOURCE wirkt nur mit Optimierung (sonst Compiler-Warnung)
HARDEN_FORTIFY := $(if $(filter -O1 -O2 -O3 -Os -Og,$(CXXFLAGS)),-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
HARDEN_CXXFLAGS ?= $(HARDEN_FORTIFY) -fstack-protector-strong -fPIE -D_GLIBCXX_ASSERTIONS -Wformat -Wformat-security \
                   $(call cc_supports,-fstack-clash-protection) $(call cc_supports,-fcf-protection)
HARDEN_LDFLAGS  ?= -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -Wl,--as-needed

BUILDDIR ?= build
BIN_DIR  ?= .
# eigenes Verzeichnis fuer Sanitizer-Builds (ueberschreibt nie BUILDDIR)
ASAN_BUILDDIR ?= build-asan

# Installationspfade gehen per Umgebung an die Shell ("$$VAR"), nie als Text ins Rezept.
PREFIX      ?= /usr/local
DESTDIR     ?=
BINDIR      ?= $(PREFIX)/bin
DATADIR     ?= $(PREFIX)/share/dofscroll-tray
SYSTEMD_DIR ?= $(PREFIX)/lib/systemd/user
UDEV_DIR    ?= /etc/udev/rules.d
UDEV_RULES  ?= $(wildcard packaging/*.rules)

export DESTDIR BINDIR DATADIR SYSTEMD_DIR UDEV_DIR UDEV_RULES

DAEMON := $(BIN_DIR)/dofscroll
TRAY   := $(BIN_DIR)/dofscroll-tray
export DAEMON TRAY

# gemeinsamer Kern (auch von den Tests gelinkt)
# every src/*.cpp except the two programs and the X11/uinput/active-window backends
# (wildcard on purpose: new core files need no Makefile edit)
CORE_SRC   := $(filter-out src/dofscroll.cpp src/dofscroll_tray.cpp src/output.cpp \
              src/output_x11.cpp src/output_uinput.cpp src/activewin.cpp, $(wildcard src/*.cpp))
DAEMON_SRC := src/dofscroll.cpp src/output.cpp src/output_x11.cpp \
              src/output_uinput.cpp src/activewin.cpp $(CORE_SRC)
TRAY_SRC   := src/dofscroll_tray.cpp

DAEMON_OBJ := $(DAEMON_SRC:src/%.cpp=$(BUILDDIR)/%.o)
TRAY_OBJ   := $(TRAY_SRC:src/%.cpp=$(BUILDDIR)/%.o)
CORE_OBJ   := $(CORE_SRC:src/%.cpp=$(BUILDDIR)/%.o)

TEST_SRC := $(wildcard tests/test_*.cpp)
TEST_BIN := $(TEST_SRC:tests/%.cpp=$(BUILDDIR)/tests/%)

DAEMON_LIBS := -lX11 -lXtst -lspnav -lpthread
GTK_PKGS    := gtk+-3.0 appindicator3-0.1
GTK_CFLAGS   = $(shell pkg-config --cflags $(GTK_PKGS))
GTK_LIBS     = $(shell pkg-config --libs $(GTK_PKGS))

.PHONY: all daemon tray test clean install uninstall install-udev \
        install-user-links check-deps test-asan lint werror check-config \
        check check-fast check-hardening
.DEFAULT_GOAL := all

all: daemon tray
daemon: $(DAEMON)
tray: $(TRAY)

$(DAEMON): $(DAEMON_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(LDFLAGS) $(HARDEN_LDFLAGS) -o $@ $^ $(DAEMON_LIBS)

$(TRAY): $(TRAY_OBJ) $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(LDFLAGS) $(HARDEN_LDFLAGS) -o $@ $^ $(GTK_LIBS) -lspnav

# Tray-Objekt braucht die GTK-Includes, alle anderen nicht
$(TRAY_OBJ): $(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(GTK_CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) -c -o $@ $<

# Tests: eigenes main je Datei, linken nur config + dsp
$(BUILDDIR)/tests/%: tests/%.cpp $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(HARDEN_LDFLAGS) -MF $@.d -o $@ $< $(CORE_OBJ) -lpthread

test: $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; $$t; done; \
	echo "Alle Tests erfolgreich."

# --- ASan/UBSan: alles in $(ASAN_BUILDDIR), Fund = Abbruch ---
ASAN_FLAGS := -std=c++17 -O1 -g -fsanitize=address,undefined \
              -fno-omit-frame-pointer -fno-sanitize-recover=undefined
ASAN_TESTS := $(TEST_SRC:tests/%.cpp=$(ASAN_BUILDDIR)/tests/%)

test-asan:
	@set -e; mkdir -p $(ASAN_BUILDDIR)/tests; \
	for t in $(TEST_SRC); do \
	  b=$(ASAN_BUILDDIR)/tests/$$(basename $$t .cpp); \
	  echo "== bauen $$b"; \
	  $(CXX) $(ASAN_FLAGS) -Wall -Wextra -Isrc -o $$b $$t $(CORE_SRC) -lpthread; \
	done; \
	for t in $(TEST_SRC); do \
	  b=$(ASAN_BUILDDIR)/tests/$$(basename $$t .cpp); \
	  echo "== $$b"; \
	  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 $$b; \
	done; \
	echo "Alle Sanitizer-Tests erfolgreich."

# --- statische Analyse (lokal ohne cppcheck: nur Hinweis, Exit 0) ---
lint:
	@if command -v cppcheck >/dev/null 2>&1; then \
	  cppcheck --enable=warning,performance,portability --error-exitcode=1 \
	    --inline-suppr \
	    --suppress=assertWithSideEffect:tests/* -Isrc src tests; \
	else \
	  echo "lint: cppcheck nicht installiert - uebersprungen (in der CI wird es installiert)."; \
	fi

# --- alles mit -Werror nur syntaktisch pruefen (keine Artefakte) ---
werror:
	@set -e; for f in $(DAEMON_SRC) $(TEST_SRC) tests/check_config.cpp; do \
	  echo "werror: $$f"; \
	  $(CXX) -std=c++17 -Wall -Wextra -Werror -Isrc -fsyntax-only $$f; \
	done; \
	for f in $(TRAY_SRC); do \
	  echo "werror: $$f"; \
	  $(CXX) -std=c++17 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -fsyntax-only $$f; \
	done; echo "werror: ok"

# Konfigurationsdatei validieren: make check-config FILE=examples/dofscroll-pro.conf
# FILE wird NUR per Umgebung ("$$FILE") an die Shell gereicht, nie in den Rezepttext
# expandiert (sonst Shell-Injection ueber FILE='x"; cmd; "').
export FILE
check-config: $(BUILDDIR)/tests/check_config
	@if [ -z "$$FILE" ]; then echo "Aufruf: make check-config FILE=pfad"; exit 2; fi
	@$(BUILDDIR)/tests/check_config "$$FILE"

# --- Hardening-Pruefung der gebauten Binaries (BIN_DIR beachten) ---
check-hardening: $(DAEMON) $(TRAY)
	sh scripts/check-hardening.sh $(DAEMON) $(TRAY)

# --- Zentrales Gate: lokal ohne Root; Gesamt-Exit != 0 bei jedem Fehler ---
# REQUIRE_ASAN=1 (CI): fehlendes libasan ist ein Fehler, sonst nur Warnung.
REQUIRE_ASAN ?= 0
# check baut in eigenes Verzeichnis, damit Root-Binaries (ggf. laufende Dienste) unberuehrt bleiben
CHECK_BIN_DIR ?= $(BUILDDIR)/bin
CHECK_CONFIG_FILE ?= examples/dofscroll-pro.conf
export CHECK_CONFIG_FILE

check:
	@fail=0; \
	step() { n=$$1; echo; echo "=== check: $$n ==="; shift; "$$@" || { echo "!!! FEHLGESCHLAGEN: $$n"; fail=1; }; }; \
	step werror $(MAKE) --no-print-directory werror; \
	step test $(MAKE) --no-print-directory test; \
	echo; echo "=== check: test-asan ==="; \
	if echo 'int main(){}' | $(CXX) -x c++ -fsanitize=address,undefined -o /dev/null - >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory test-asan || { echo "!!! FEHLGESCHLAGEN: test-asan"; fail=1; }; \
	elif [ "$(REQUIRE_ASAN)" = "1" ]; then \
	  echo "!!! FEHLGESCHLAGEN: libasan/libubsan nicht verfuegbar, aber REQUIRE_ASAN=1"; fail=1; \
	else \
	  echo "WARNUNG: libasan/libubsan nicht verfuegbar - test-asan uebersprungen (CI: REQUIRE_ASAN=1)."; \
	fi; \
	step lint $(MAKE) --no-print-directory lint; \
	step check-config $(MAKE) --no-print-directory check-config FILE="$$CHECK_CONFIG_FILE"; \
	step build $(MAKE) --no-print-directory all BIN_DIR=$(CHECK_BIN_DIR); \
	step check-hardening $(MAKE) --no-print-directory check-hardening BIN_DIR=$(CHECK_BIN_DIR); \
	echo; if [ $$fail -ne 0 ]; then echo "check: FEHLGESCHLAGEN"; exit 1; fi; \
	echo "check: alles ok"

# Schnelles Gate fuer den Pre-commit-Hook
check-fast:
	@fail=0; \
	step() { echo; echo "=== check-fast: $$1 ==="; shift; "$$@" || { echo "!!! FEHLGESCHLAGEN: $$1"; fail=1; }; }; \
	step werror $(MAKE) --no-print-directory werror; \
	step test $(MAKE) --no-print-directory test; \
	step check-config $(MAKE) --no-print-directory check-config FILE="$$CHECK_CONFIG_FILE"; \
	echo; if [ $$fail -ne 0 ]; then echo "check-fast: FEHLGESCHLAGEN"; exit 1; fi; \
	echo "check-fast: alles ok"

clean:
	rm -rf $(BUILDDIR) $(ASAN_BUILDDIR)
	rm -f $(DAEMON) $(TRAY)

# --- Installation (echte Kopie; fuer die Symlink-Variante siehe install-user-links) ---
install: all
	install -d "$$DESTDIR$$BINDIR" "$$DESTDIR$$DATADIR/icons" "$$DESTDIR$$SYSTEMD_DIR"
	install -m 755 "$$DAEMON" "$$TRAY" "$$DESTDIR$$BINDIR/"
	install -m 644 icons/dofscroll-tray.svg "$$DESTDIR$$DATADIR/icons/"
	install -m 644 systemd/dofscroll.service systemd/dofscroll-tray.service \
	    "$$DESTDIR$$SYSTEMD_DIR/"

uninstall:
	rm -f "$$DESTDIR$$BINDIR/dofscroll" "$$DESTDIR$$BINDIR/dofscroll-tray"
	rm -f "$$DESTDIR$$DATADIR/icons/dofscroll-tray.svg"
	-rmdir "$$DESTDIR$$DATADIR/icons" "$$DESTDIR$$DATADIR" 2>/dev/null
	rm -f "$$DESTDIR$$SYSTEMD_DIR/dofscroll.service" \
	      "$$DESTDIR$$SYSTEMD_DIR/dofscroll-tray.service"

# Optionale udev-Regel(n) aus packaging/*.rules (braucht root)
# $$UDEV_RULES ist bewusst ungequotet (Liste); Wort-Trennung ist kein Befehls-Injection-Pfad.
install-udev:
	@if [ -z "$$UDEV_RULES" ]; then echo "Keine packaging/*.rules vorhanden."; exit 1; fi
	install -d "$$DESTDIR$$UDEV_DIR"
	install -m 644 $$UDEV_RULES "$$DESTDIR$$UDEV_DIR/"
	@echo "Danach: sudo udevadm control --reload && sudo udevadm trigger"

# Gibt nur die Befehle aus (readme.md Abschnitt 6) - fuehrt nichts mit sudo aus
install-user-links:
	@echo "# Symlink-Installation (readme.md Abschnitt 6); root-Teil bitte selbst ausfuehren:"
	@echo 'sudo ln -sf "$(CURDIR)/dofscroll" /usr/local/bin/dofscroll'
	@echo 'sudo ln -sf "$(CURDIR)/dofscroll-tray" /usr/local/bin/dofscroll-tray'
	@echo 'sudo mkdir -p /usr/local/share/dofscroll-tray/icons'
	@echo 'sudo ln -sf "$(CURDIR)/icons/dofscroll-tray.svg" /usr/local/share/dofscroll-tray/icons/dofscroll-tray.svg'
	@echo 'sudo systemctl enable --now spacenavd'
	@echo "# als normaler Benutzer:"
	@echo 'mkdir -p ~/.config/systemd/user'
	@echo 'ln -sf "$(CURDIR)/systemd/dofscroll.service" ~/.config/systemd/user/dofscroll.service'
	@echo 'ln -sf "$(CURDIR)/systemd/dofscroll-tray.service" ~/.config/systemd/user/dofscroll-tray.service'
	@echo 'systemctl --user daemon-reload'
	@echo 'systemctl --user enable --now dofscroll.service dofscroll-tray.service'

check-deps:
	@miss=0; \
	for p in x11 xtst $(GTK_PKGS); do \
	  if pkg-config --exists $$p 2>/dev/null; then echo "ok      $$p"; \
	  else echo "FEHLT   $$p"; miss=1; fi; done; \
	if echo '#include <spnav.h>' | $(CXX) -x c++ -fsyntax-only - 2>/dev/null; \
	  then echo "ok      libspnav (spnav.h)"; else echo "FEHLT   libspnav (spnav.h)"; miss=1; fi; \
	if [ $$miss -ne 0 ]; then \
	  echo; echo "Nachinstallieren (RHEL 9 + EPEL):"; \
	  echo "  sudo dnf install -y epel-release"; \
	  echo "  sudo dnf install -y gcc-c++ make pkgconf-pkg-config spacenavd libspnav-devel \\"; \
	  echo "      libX11-devel libXtst-devel gtk3-devel libappindicator-gtk3-devel"; \
	  echo "(GTK/appindicator nur fuer den Tray; 'make daemon' braucht sie nicht.)"; \
	  exit 1; fi; echo "Alle Abhaengigkeiten vorhanden."

-include $(DAEMON_OBJ:.o=.d) $(TRAY_OBJ:.o=.d) $(TEST_BIN:=.d)
