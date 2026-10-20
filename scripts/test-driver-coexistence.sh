#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Guided test (about 5 minutes): can dofscroll read the SpaceMouse at the same time as the official
# 3Dconnexion driver? Writes one text file (dofscroll-driver-test-report.txt in the current folder)
# to attach to an issue. For macOS (bash 3.2, BSD tools); it also runs on Linux for a dry run.
#
#   test-driver-coexistence.sh [--probe PATH] [--daemon PATH] [--report FILE] [--seconds N]
#                              [--non-interactive]
#
# dofscroll is meant to run next to the official driver. Whether both can read the device at the same
# time has NOT been verified on real hardware yet. The test:
#   A  reads the device with dofscroll-hidprobe while the driver runs (you move the cap for 10 s)
#   B  starts dofscroll next to the driver and asks a few yes/no questions
#   C  OPTIONAL, for diagnosis only, normal users do not need it: A again with the driver stopped.
#      The script never stops anything itself; it only tells you how if you want it.
# The script changes nothing: it does not stop, kill or configure the driver. The only process it
# may end is the dofscroll it started itself in test B. The report contains no user name and no
# personal paths (the home folder is written as ~).
#
# --non-interactive answers every question with "skipped" and waits for nobody (CI self-check).

SECONDS_TEST=10
PROBE=""
DAEMON=""
REPORT=""
NONINT=0
while [ $# -gt 0 ]; do
    case "$1" in
        --probe) PROBE="$2"; shift 2 ;;
        --daemon) DAEMON="$2"; shift 2 ;;
        --report) REPORT="$2"; shift 2 ;;
        --seconds) SECONDS_TEST="$2"; shift 2 ;;
        --non-interactive) NONINT=1; shift ;;
        -h|--help) sed -n '5,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done
case "$SECONDS_TEST" in
    ''|*[!0-9]*) echo "--seconds needs a number" >&2; exit 2 ;;
esac
[ "$SECONDS_TEST" -ge 2 ] || SECONDS_TEST=2
[ "$SECONDS_TEST" -le 120 ] || SECONDS_TEST=120

SD=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/dofscroll-coexist.XXXXXX") || { echo "cannot create a temporary folder" >&2; exit 1; }
REP="$WORK/report.txt"
VERDICTS="$WORK/verdicts.txt"
: > "$REP"
: > "$VERDICTS"
DAEMON_PID=""
cleanup() {
    # only the dofscroll this script started itself
    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then kill -TERM "$DAEMON_PID" 2>/dev/null; fi
    rm -rf "$WORK"
}
trap cleanup EXIT

line() { printf '%s\n' "$*" >> "$REP"; }
section() { line ""; line "=== $*"; echo ""; echo "=== $*"; }
verdict() { printf '%s\n' "$*" >> "$VERDICTS"; line "$*"; echo "  => $*"; }

# ask "question": sets ANSWER to yes, no or skipped
ask() {
    if [ "$NONINT" -eq 1 ]; then ANSWER=skipped; line "Q: $1 -> skipped (non-interactive)"; return; fi
    while :; do
        printf '%s [y/n/s=skip] ' "$1"
        read -r a || a=s
        case "$a" in
            y|Y|yes|YES) ANSWER=yes; line "Q: $1 -> yes"; return ;;
            n|N|no|NO) ANSWER=no; line "Q: $1 -> no"; return ;;
            s|S|skip|"") ANSWER=skipped; line "Q: $1 -> skipped"; return ;;
        esac
    done
}
wait_enter() {
    [ "$NONINT" -eq 1 ] && return
    printf '%s ' "$1"
    read -r _ignored || true
}

# run_limited OUTFILE TIMEOUT_SECONDS COMMAND...  (sets RC, TIMED_OUT)
run_limited() {
    _out=$1; _to=$2; shift 2
    TIMED_OUT=0
    "$@" > "$_out" 2>&1 &
    _pid=$!
    _i=0
    while kill -0 "$_pid" 2>/dev/null; do
        if [ "$_i" -ge "$_to" ]; then kill "$_pid" 2>/dev/null; TIMED_OUT=1; break; fi
        sleep 1
        _i=$((_i + 1))
    done
    wait "$_pid" 2>/dev/null
    RC=$?
}

# keeps the useful lines of hidprobe output (drops the live "tx=" refresh lines, splits on CR)
probe_lines() {
    tr '\r' '\n' < "$1" | sed -e 's/[[:space:]]*$//' -e '/^$/d' -e '/^[[:space:]]*tx=/d'
}
# result_field FILE NAME: value of NAME= in the RESULT line (quoted or plain)
result_field() {
    tr '\r' '\n' < "$1" | grep '^RESULT ' | tail -n 1 | sed -n -e "s/.* $2=\"\\([^\"]*\\)\".*/\\1/p" -e "s/.* $2=\\([^ \"]*\\).*/\\1/p" | head -n 1
}

# ---- locate the programs -------------------------------------------------------------------------
if [ -z "$PROBE" ]; then
    for c in "$SD/dofscroll-hidprobe" "$SD/../daemon/dofscroll-hidprobe" "$SD/../dofscroll-hidprobe" "$SD/../build/dofscroll-hidprobe" "$SD/../build/tools/hidprobe/dofscroll-hidprobe"; do
        if [ -x "$c" ]; then PROBE="$c"; break; fi
    done
    [ -n "$PROBE" ] || PROBE=$(command -v dofscroll-hidprobe 2>/dev/null || true)
fi
if [ -z "$PROBE" ] || [ ! -x "$PROBE" ]; then
    echo "dofscroll-hidprobe not found. Run this script from the dofscroll folder inside the disk image or the app (see docs/devices.md), or pass --probe PATH." >&2
    exit 2
fi
PROBE_DIR=$(cd "$(dirname "$PROBE")" && pwd)
if [ -z "$DAEMON" ]; then
    for c in "$PROBE_DIR/dofscroll" "$SD/../build/dofscroll"; do
        if [ -x "$c" ]; then DAEMON="$c"; break; fi
    done
fi
DEVICES_DIR="$PROBE_DIR/devices"
[ -d "$DEVICES_DIR" ] || DEVICES_DIR="$SD/../devices"
[ -n "$REPORT" ] || REPORT="$(pwd)/dofscroll-driver-test-report.txt"
OSNAME=$(uname -s)

echo "dofscroll driver coexistence test"
echo "dofscroll is meant to run next to the official 3Dconnexion driver. Whether both can read the"
echo "SpaceMouse at the same time is not verified yet on real machines: this test finds out."
echo "Nothing is changed: the script never stops or configures the driver. About 5 minutes."
echo ""

line "dofscroll driver coexistence test report ($OSNAME)"
line "date (UTC): $(date -u '+%Y-%m-%d %H:%M')"
line 'Questions for the maintainer: see docs/troubleshooting.md, "The official 3Dconnexion driver and dofscroll".'

# ---- (a) environment ---------------------------------------------------------------------------------
section "System"
if command -v sw_vers >/dev/null 2>&1; then
    line "OS: $(sw_vers -productName) $(sw_vers -productVersion) (build $(sw_vers -buildVersion)), $(uname -m)"
else
    line "OS: $(uname -sr) $(uname -m)"
fi
line "shell: bash $BASH_VERSION"
line "hidprobe: $("$PROBE" --version 2>&1 | head -n 1)"
if [ -n "$DAEMON" ]; then
    line "dofscroll: $("$DAEMON" --version 2>&1 | head -n 1)"
else
    line "dofscroll: daemon not found next to hidprobe (test B will be skipped)"
fi

section "SpaceMouse devices (dofscroll-hidprobe --list)"
run_limited "$WORK/list.txt" 30 "$PROBE" --list --devices-dir "$DEVICES_DIR"
probe_lines "$WORK/list.txt" >> "$REP"
DEVICE_FOUND=0
grep -q '\[matches "' "$WORK/list.txt" && DEVICE_FOUND=1
if [ "$DEVICE_FOUND" -eq 0 ]; then
    line "No SpaceMouse with a devices/*.conf description found."
    echo "No known SpaceMouse found. Plug it in (receiver or cable) and run the test again."
fi

section "Official 3Dconnexion driver"
DRIVER_PROCS=$(pgrep -il 3Dconnexion 2>/dev/null | sed 's/^[0-9]* *//' | sort -u | tr '\n' ',' | sed 's/,$//; s/,/, /g')
if [ -n "$DRIVER_PROCS" ]; then
    line "driver processes running: $DRIVER_PROCS"
    DRIVER_ACTIVE=1
else
    line "driver processes running: none matching 3Dconnexion (3DconnexionHelper, 3Dconnexion ...)"
    DRIVER_ACTIVE=0
fi
DRIVER_VERSION=""
for app in "/Applications/3Dconnexion Software.app" "/Applications/3DconnexionSoftware.app" "/Applications/3Dconnexion.app" \
           "/Library/Application Support/3Dconnexion/3DconnexionHelper.app" "/Library/Application Support/3Dconnexion/3Dconnexion Software.app"; do
    if [ -f "$app/Contents/Info.plist" ]; then
        v=$(defaults read "$app/Contents/Info" CFBundleShortVersionString 2>/dev/null || true)
        if [ -n "$v" ]; then DRIVER_VERSION="$v ($(basename "$app"))"; break; fi
    fi
done
if [ -n "$DRIVER_VERSION" ]; then
    line "driver version: $DRIVER_VERSION"
else
    line "driver version: not found (no 3Dconnexion app bundle in /Applications or /Library/Application Support/3Dconnexion)"
    if [ -d "/Library/Application Support/3Dconnexion" ]; then
        line "  /Library/Application Support/3Dconnexion contains: $(ls "/Library/Application Support/3Dconnexion" 2>/dev/null | tr '\n' ' ')"
    fi
fi
if [ "$DRIVER_ACTIVE" -eq 1 ]; then verdict "Driver running: YES"; else verdict "Driver running: NO"; fi

# ---- tests with hidprobe -------------------------------------------------------------------------------
# test_reading LABEL: sets T_OPENED T_REPORTS T_AXES T_ERROR
test_reading() {
    T_OPENED=no; T_REPORTS=0; T_AXES=""; T_ERROR=""
    echo ""
    if [ "$NONINT" -eq 0 ]; then
        echo "Test $1: when you press Enter, move the SpaceMouse cap for $SECONDS_TEST seconds:"
        echo "  push it in all directions, tilt it, twist it, and press its buttons."
        wait_enter "Press Enter to start"
        _secs=$SECONDS_TEST
    else
        _secs=2
    fi
    run_limited "$WORK/check.txt" $((_secs + 40)) "$PROBE" --check --seconds "$_secs" --devices-dir "$DEVICES_DIR"
    probe_lines "$WORK/check.txt" >> "$REP"
    [ "$TIMED_OUT" -eq 1 ] && line "hidprobe did not finish in time and was ended by the script"
    line "hidprobe exit code: $RC (0 = reports received, 3 = opened but no report, 1 = could not open, 2 = usage)"
    T_OPENED=$(result_field "$WORK/check.txt" opened)
    [ -n "$T_OPENED" ] || T_OPENED=no
    T_ERROR=$(result_field "$WORK/check.txt" error)
    n=$(result_field "$WORK/check.txt" reports)
    case "$n" in ''|*[!0-9]*) n=0 ;; esac
    T_REPORTS=$n
    T_AXES=$(result_field "$WORK/check.txt" axes_moved)
    if [ "$DEVICE_FOUND" -eq 0 ]; then T_OPENED=no; T_ERROR="no device found"; fi
}

reading_verdicts() { # LETTER STATE
    if [ "$T_OPENED" = yes ]; then verdict "$1: device opened while $2: YES"; else verdict "$1: device opened while $2: NO"; fi
    [ -n "$T_ERROR" ] && line "$1: error text: $T_ERROR"
    if [ "$T_REPORTS" -gt 0 ]; then verdict "$1: reports received while $2: YES ($T_REPORTS reports)"; else verdict "$1: reports received while $2: NO"; fi
    line "$1: axes seen: ${T_AXES:-none}"
}

section "TEST A: driver running, hidprobe reads the device"
test_reading A
A_OPENED=$T_OPENED; A_REPORTS=$T_REPORTS
reading_verdicts A "the driver runs"
if [ "$DRIVER_ACTIVE" -eq 1 ] && [ "$A_OPENED" = yes ] && [ "$A_REPORTS" -eq 0 ] && [ "$NONINT" -eq 0 ]; then
    line "A: note: the device opened but nothing arrived. Either the cap was not moved, or the driver keeps the reports for itself."
fi
if grep -qi 'Input Monitoring' "$WORK/check.txt" 2>/dev/null; then
    line "A: note: Input Monitoring is missing for the program that runs this script (usually Terminal): System Settings > Privacy & Security > Input Monitoring."
fi

# ---- test B ----------------------------------------------------------------------------------------------
section "TEST B: driver and dofscroll together"
RT_DIR="$HOME/Library/Caches/dofscroll"
LOG_FILE="$RT_DIR/dofscroll.log"
STATUS_FILE="$RT_DIR/dofscroll-status"
B_DONE=0
B_DOUBLED=skipped; B_CAD=skipped; B_REACTS=skipped
if [ -z "$DAEMON" ]; then
    line "B: skipped (dofscroll daemon not found)"
elif [ "$DEVICE_FOUND" -eq 0 ]; then
    line "B: skipped (no SpaceMouse found)"
elif [ "$NONINT" -eq 1 ]; then
    line "B: skipped (non-interactive)"
else
    LOG_START=0
    [ -f "$LOG_FILE" ] && LOG_START=$(wc -l < "$LOG_FILE" | tr -d ' ')
    if pgrep -x dofscroll >/dev/null 2>&1; then
        line "B: a dofscroll daemon was already running; the script uses it and does not start or stop one"
    else
        "$DAEMON" > "$WORK/daemon.out" 2>&1 &
        DAEMON_PID=$!
        line "B: the script started dofscroll for this test (it ends it again afterwards)"
        sleep 4
    fi
    echo ""
    echo "dofscroll and the 3Dconnexion driver are now running together. Please try:"
    echo "  1. Open a web page or a long document in a browser, put the mouse pointer over it"
    echo "     and push/tilt the SpaceMouse cap for about 10 seconds."
    echo "  2. Then do the same in your CAD program (rotate, pan and zoom a model)."
    wait_enter "Press Enter when you have tried both"
    ask "In the browser, did it scroll TWICE as much, or jump or stutter (as if two programs scrolled)?"; B_DOUBLED=$ANSWER
    ask "Did the CAD program still react normally to the SpaceMouse (rotate, pan, zoom)?"; B_CAD=$ANSWER
    ask "Did dofscroll scroll or zoom in the browser (as your dofscroll settings say)?"; B_REACTS=$ANSWER
    B_DONE=1
fi
if [ "$B_DONE" -eq 1 ]; then
    line ""
    line "dofscroll log (new lines only, lines about the device and errors):"
    SHOWN=0
    if [ -n "$DAEMON_PID" ] && [ -s "$WORK/daemon.out" ]; then LOGSRC="$WORK/daemon.out"; SKIP=0
    else LOGSRC="$LOG_FILE"; SKIP=$LOG_START; fi
    if [ -f "$LOGSRC" ]; then
        tail -n +$((SKIP + 1)) "$LOGSRC" | grep -i -E 'device|hid|cannot|error|connected|spacemouse|driver|exclusive|attached|removed|input' | head -n 40 > "$WORK/log.sel"
        if [ -s "$WORK/log.sel" ]; then sed 's/^/  /' "$WORK/log.sel" >> "$REP"; SHOWN=1; fi
    fi
    [ "$SHOWN" -eq 1 ] || line "  (no matching lines; log file: ~/Library/Caches/dofscroll/dofscroll.log)"
    line "dofscroll status file:"
    if [ -f "$STATUS_FILE" ]; then
        grep -E '^(device|devices_seen|input_issue|enabled|paused|updated)=' "$STATUS_FILE" | sed 's/^/  /' >> "$REP"
    else
        line "  (no status file)"
    fi
    if [ -n "$DAEMON_PID" ]; then
        kill -TERM "$DAEMON_PID" 2>/dev/null
        DAEMON_PID=""
        line "B: the dofscroll started by the script was asked to quit"
    fi
    for pair in "scrolling doubled:$B_DOUBLED" "CAD program still reacts normally:$B_CAD" "dofscroll reacts while the driver runs:$B_REACTS"; do
        name=${pair%%:*}; ans=${pair#*:}
        case "$ans" in yes) t=YES ;; no) t=NO ;; *) t="NOT ANSWERED" ;; esac
        verdict "B: $name: $t"
    done
fi

# ---- test C --------------------------------------------------------------------------------------------------
section "TEST C (optional, for diagnosis only): driver stopped"
C_DONE=0; C_REPORTS=0
if [ "$NONINT" -eq 1 ] || [ "$DRIVER_ACTIVE" -eq 0 ]; then
    line "C: skipped"
else
    echo ""
    echo "Test C is for diagnosis only. Normal users do not need to do it: dofscroll is meant to run"
    echo "next to the driver, and this script never stops the driver. If the maintainer asked you to,"
    echo "you can quit the 3Dconnexion helper yourself (its icon in the menu bar, or the 3Dconnexion"
    echo "pane in System Settings) and start it again afterwards."
    ask "Do you want to do Test C now (you quit the driver yourself, then answer yes)?"
    if [ "$ANSWER" = yes ]; then
        echo "Quit the driver now, then come back here."
        wait_enter "Press Enter when the driver is stopped"
        STILL=$(pgrep -il 3Dconnexion 2>/dev/null | sed 's/^[0-9]* *//' | sort -u | tr '\n' ',' | sed 's/,$//; s/,/, /g')
        line "C: driver processes still running: ${STILL:-none}"
        test_reading C
        C_DONE=1; C_REPORTS=$T_REPORTS
        reading_verdicts C "the driver is stopped"
        echo "You can start the driver again now."
    else
        line "C: skipped"
    fi
fi

# ---- summary -----------------------------------------------------------------------------------------------------
section "Summary"
cat "$VERDICTS" >> "$REP"
if [ "$C_DONE" -eq 1 ] && [ "$A_REPORTS" -eq 0 ] && [ "$C_REPORTS" -gt 0 ]; then
    line "Reading: no reports while the driver runs, reports with the driver stopped: the driver keeps the device for itself (this is a bug of dofscroll to be fixed)."
fi

# ---- write the report (no user name, no personal paths) --------------------------------------------------
esc() { printf '%s' "$1" | sed 's/[][\\.*^$|/]/\\&/g'; }
# scrub: home folder -> ~, user name -> <user>, host name -> <host> (only when long enough to be unambiguous)
scrub() {
    set --
    [ "${#HOME}" -gt 3 ] && set -- "$@" -e "s|$(esc "$HOME")|~|g"
    [ "${#USER}" -gt 1 ] && set -- "$@" -e "s|$(esc "$USER")|<user>|g"
    _h=$(hostname 2>/dev/null || true)
    [ "${#_h}" -gt 1 ] && set -- "$@" -e "s|$(esc "$_h")|<host>|g"
    _hs=$(hostname -s 2>/dev/null || true)
    [ "${#_hs}" -gt 1 ] && set -- "$@" -e "s|$(esc "$_hs")|<host>|g"
    if [ $# -eq 0 ]; then cat; else sed "$@"; fi
}
if scrub < "$REP" > "$REPORT" 2>/dev/null; then
    echo ""
    echo "Report written to: $(printf '%s' "$REPORT" | scrub)"
    echo "Please read it once (it has no user name and no personal paths), then attach this file to"
    echo "issue #37 (macOS) of the dofscroll repository, or paste its text into a comment."
else
    echo "Could not write $REPORT; here is the report:"
    scrub < "$REP"
    exit 1
fi
exit 0
