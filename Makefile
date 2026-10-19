# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Makefile for dofscroll (daemon) and dofscroll-tray (tray + settings)
# (an equivalent CMake build lives in CMakeLists.txt)
#
# Targets: all daemon tray test clean install uninstall install-udev install-user-links check-deps
# Examples:
#   make                 # build both binaries in the repo root
#   make daemon          # daemon only (no GTK)
#   make test            # build and run tests/test_*.cpp
#   make -j4 BUILDDIR=/tmp/b BIN_DIR=/tmp/bin   # redirect output paths

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -MMD -MP -Isrc

# --- Hardening (on by default for all binaries and tests) ---
# Separate variables so that a CXXFLAGS given on the command line does not
# lose them by accident. Disable only deliberately: make HARDEN_CXXFLAGS= HARDEN_LDFLAGS=
# Compiler probe: only use a flag if the compiler knows it.
cc_supports = $(shell echo 'int main(){return 0;}' | $(CXX) -x c++ $(1) -c -o /dev/null - >/dev/null 2>&1 && echo $(1))
# _FORTIFY_SOURCE only works with optimization (otherwise a compiler warning)
HARDEN_FORTIFY := $(if $(filter -O1 -O2 -O3 -Os -Og,$(CXXFLAGS)),-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
HARDEN_CXXFLAGS ?= $(HARDEN_FORTIFY) -fstack-protector-strong -fPIE -D_GLIBCXX_ASSERTIONS -Wformat -Wformat-security \
                   $(call cc_supports,-fstack-clash-protection) $(call cc_supports,-fcf-protection)
HARDEN_LDFLAGS  ?= -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -Wl,--as-needed

BUILDDIR ?= build
BIN_DIR  ?= .
# separate directory for sanitizer builds (never overwrites BUILDDIR)
ASAN_BUILDDIR ?= build-asan

# Install paths reach the shell via the environment ("$$VAR"), never as text in the recipe.
PREFIX      ?= /usr/local
DESTDIR     ?=
BINDIR      ?= $(PREFIX)/bin
DATADIR     ?= $(PREFIX)/share/dofscroll-tray
SYSTEMD_DIR ?= $(PREFIX)/lib/systemd/user
EXDIR       ?= $(PREFIX)/share/dofscroll/examples
UDEV_DIR    ?= /etc/udev/rules.d
UDEV_RULES  ?= $(wildcard packaging/*.rules)

export DESTDIR BINDIR DATADIR EXDIR SYSTEMD_DIR UDEV_DIR UDEV_RULES

DAEMON := $(BIN_DIR)/dofscroll
TRAY   := $(BIN_DIR)/dofscroll-tray
export DAEMON TRAY

# shared core (also linked by the tests)
CORE_SRC   := src/config.cpp src/dsp.cpp src/device_count.cpp src/modes.cpp src/seed.cpp
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

# The tray object needs the GTK includes, all others do not
$(TRAY_OBJ): $(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(GTK_CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) -c -o $@ $<

# Tests: one main per file, link only the core
$(BUILDDIR)/tests/%: tests/%.cpp $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HARDEN_CXXFLAGS) $(HARDEN_LDFLAGS) -MF $@.d -o $@ $< $(CORE_OBJ) -lpthread

test: $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; $$t; done; \
	echo "All tests passed."

# --- ASan/UBSan: everything in $(ASAN_BUILDDIR), any finding aborts ---
ASAN_FLAGS := -std=c++17 -O1 -g -fsanitize=address,undefined \
              -fno-omit-frame-pointer -fno-sanitize-recover=undefined
ASAN_TESTS := $(TEST_SRC:tests/%.cpp=$(ASAN_BUILDDIR)/tests/%)

test-asan:
	@set -e; mkdir -p $(ASAN_BUILDDIR)/tests; \
	for t in $(TEST_SRC); do \
	  b=$(ASAN_BUILDDIR)/tests/$$(basename $$t .cpp); \
	  echo "== building $$b"; \
	  $(CXX) $(ASAN_FLAGS) -Wall -Wextra -Isrc -o $$b $$t $(CORE_SRC) -lpthread; \
	done; \
	for t in $(TEST_SRC); do \
	  b=$(ASAN_BUILDDIR)/tests/$$(basename $$t .cpp); \
	  echo "== $$b"; \
	  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 $$b; \
	done; \
	echo "All sanitizer tests passed."

# --- static analysis (without cppcheck locally: notice only, exit 0) ---
lint:
	@if command -v cppcheck >/dev/null 2>&1; then \
	  cppcheck --enable=warning,performance,portability --error-exitcode=1 \
	    --inline-suppr \
	    --suppress=assertWithSideEffect:tests/* -Isrc src tests; \
	else \
	  echo "lint: cppcheck not installed - skipped (CI installs it)."; \
	fi

# --- syntax-only check of everything with -Werror (no artifacts) ---
werror:
	@set -e; for f in $(DAEMON_SRC) $(TEST_SRC) tests/check_config.cpp; do \
	  echo "werror: $$f"; \
	  $(CXX) -std=c++17 -Wall -Wextra -Werror -Isrc -fsyntax-only $$f; \
	done; \
	for f in $(TRAY_SRC); do \
	  echo "werror: $$f"; \
	  $(CXX) -std=c++17 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -fsyntax-only $$f; \
	done; echo "werror: ok"

# Validate a config file: make check-config FILE=examples/dofscroll-pro.conf
# FILE is passed to the shell ONLY via the environment ("$$FILE"), never expanded
# into the recipe text (otherwise shell injection via FILE='x"; cmd; "').
export FILE
check-config: $(BUILDDIR)/tests/check_config
	@if [ -z "$$FILE" ]; then echo "Usage: make check-config FILE=path"; exit 2; fi
	@$(BUILDDIR)/tests/check_config "$$FILE"

# --- Hardening check of the built binaries (honors BIN_DIR) ---
check-hardening: $(DAEMON) $(TRAY)
	sh scripts/check-hardening.sh $(DAEMON) $(TRAY)

# --- Central gate: runs locally without root; overall exit != 0 on any failure ---
# REQUIRE_ASAN=1 (CI): a missing libasan is an error, otherwise only a warning.
REQUIRE_ASAN ?= 0
# check builds into its own directory so the root binaries (possibly running services) stay untouched
CHECK_BIN_DIR ?= $(BUILDDIR)/bin
CHECK_CONFIG_FILE ?= examples/dofscroll-pro.conf
export CHECK_CONFIG_FILE

check:
	@fail=0; \
	step() { n=$$1; echo; echo "=== check: $$n ==="; shift; "$$@" || { echo "!!! FAILED: $$n"; fail=1; }; }; \
	step werror $(MAKE) --no-print-directory werror; \
	step test $(MAKE) --no-print-directory test; \
	echo; echo "=== check: test-asan ==="; \
	if echo 'int main(){}' | $(CXX) -x c++ -fsanitize=address,undefined -o /dev/null - >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory test-asan || { echo "!!! FAILED: test-asan"; fail=1; }; \
	elif [ "$(REQUIRE_ASAN)" = "1" ]; then \
	  echo "!!! FAILED: libasan/libubsan not available, but REQUIRE_ASAN=1"; fail=1; \
	else \
	  echo "WARNING: libasan/libubsan not available - test-asan skipped (CI: REQUIRE_ASAN=1)."; \
	fi; \
	step lint $(MAKE) --no-print-directory lint; \
	step check-config $(MAKE) --no-print-directory check-config FILE="$$CHECK_CONFIG_FILE"; \
	step build $(MAKE) --no-print-directory all BIN_DIR=$(CHECK_BIN_DIR); \
	step check-hardening $(MAKE) --no-print-directory check-hardening BIN_DIR=$(CHECK_BIN_DIR); \
	echo; if [ $$fail -ne 0 ]; then echo "check: FAILED"; exit 1; fi; \
	echo "check: all ok"

# Fast gate for the pre-commit hook
check-fast:
	@fail=0; \
	step() { echo; echo "=== check-fast: $$1 ==="; shift; "$$@" || { echo "!!! FAILED: $$1"; fail=1; }; }; \
	step werror $(MAKE) --no-print-directory werror; \
	step test $(MAKE) --no-print-directory test; \
	step check-config $(MAKE) --no-print-directory check-config FILE="$$CHECK_CONFIG_FILE"; \
	echo; if [ $$fail -ne 0 ]; then echo "check-fast: FAILED"; exit 1; fi; \
	echo "check-fast: all ok"

clean:
	rm -rf $(BUILDDIR) $(ASAN_BUILDDIR)
	rm -f $(DAEMON) $(TRAY)

# --- Installation (real copy; for the symlink variant see install-user-links) ---
install: all
	install -d "$$DESTDIR$$BINDIR" "$$DESTDIR$$DATADIR/icons" "$$DESTDIR$$SYSTEMD_DIR"
	install -m 755 "$$DAEMON" "$$TRAY" "$$DESTDIR$$BINDIR/"
	install -m 644 icons/dofscroll-tray.svg "$$DESTDIR$$DATADIR/icons/"
	install -d "$$DESTDIR$$EXDIR/modes" "$$DESTDIR$$EXDIR/default"
	install -m 644 examples/*.conf "$$DESTDIR$$EXDIR/"
	install -m 644 examples/default/*.conf "$$DESTDIR$$EXDIR/default/"
	if ls examples/modes/* >/dev/null 2>&1; then install -m 644 examples/modes/* "$$DESTDIR$$EXDIR/modes/"; fi
	install -m 644 systemd/dofscroll.service systemd/dofscroll-tray.service \
	    "$$DESTDIR$$SYSTEMD_DIR/"

uninstall:
	rm -f "$$DESTDIR$$BINDIR/dofscroll" "$$DESTDIR$$BINDIR/dofscroll-tray"
	rm -f "$$DESTDIR$$DATADIR/icons/dofscroll-tray.svg"
	rm -rf "$$DESTDIR$$EXDIR"
	-rmdir "$$DESTDIR$$DATADIR/icons" "$$DESTDIR$$DATADIR" 2>/dev/null
	rm -f "$$DESTDIR$$SYSTEMD_DIR/dofscroll.service" \
	      "$$DESTDIR$$SYSTEMD_DIR/dofscroll-tray.service"

# Optional udev rule(s) from packaging/*.rules (needs root)
# $$UDEV_RULES is deliberately unquoted (a list); word splitting is not a command-injection path.
install-udev:
	@if [ -z "$$UDEV_RULES" ]; then echo "No packaging/*.rules present."; exit 1; fi
	install -d "$$DESTDIR$$UDEV_DIR"
	install -m 644 $$UDEV_RULES "$$DESTDIR$$UDEV_DIR/"
	@echo "Afterwards: sudo udevadm control --reload && sudo udevadm trigger"

# Only prints the commands (readme.md section 6) - runs nothing with sudo
install-user-links:
	@echo "# Symlink installation (readme.md section 6); run the root part yourself:"
	@echo 'sudo ln -sf "$(CURDIR)/dofscroll" /usr/local/bin/dofscroll'
	@echo 'sudo ln -sf "$(CURDIR)/dofscroll-tray" /usr/local/bin/dofscroll-tray'
	@echo 'sudo mkdir -p /usr/local/share/dofscroll-tray/icons'
	@echo 'sudo ln -sf "$(CURDIR)/icons/dofscroll-tray.svg" /usr/local/share/dofscroll-tray/icons/dofscroll-tray.svg'
	@echo 'sudo systemctl enable --now spacenavd'
	@echo "# as a normal user:"
	@echo 'mkdir -p ~/.config/systemd/user'
	@echo 'ln -sf "$(CURDIR)/systemd/dofscroll.service" ~/.config/systemd/user/dofscroll.service'
	@echo 'ln -sf "$(CURDIR)/systemd/dofscroll-tray.service" ~/.config/systemd/user/dofscroll-tray.service'
	@echo 'systemctl --user daemon-reload'
	@echo 'systemctl --user enable --now dofscroll.service dofscroll-tray.service'

check-deps:
	@miss=0; \
	for p in x11 xtst $(GTK_PKGS); do \
	  if pkg-config --exists $$p 2>/dev/null; then echo "ok      $$p"; \
	  else echo "MISSING $$p"; miss=1; fi; done; \
	if echo '#include <spnav.h>' | $(CXX) -x c++ -fsyntax-only - 2>/dev/null; \
	  then echo "ok      libspnav (spnav.h)"; else echo "MISSING libspnav (spnav.h)"; miss=1; fi; \
	if [ $$miss -ne 0 ]; then \
	  echo; echo "Install the missing packages (RHEL 9 + EPEL):"; \
	  echo "  sudo dnf install -y epel-release"; \
	  echo "  sudo dnf install -y gcc-c++ make pkgconf-pkg-config spacenavd libspnav-devel \\"; \
	  echo "      libX11-devel libXtst-devel gtk3-devel libappindicator-gtk3-devel"; \
	  echo "(GTK/appindicator only for the tray; 'make daemon' does not need them.)"; \
	  exit 1; fi; echo "All dependencies present."

-include $(DAEMON_OBJ:.o=.d) $(TRAY_OBJ:.o=.d) $(TEST_BIN:=.d)
