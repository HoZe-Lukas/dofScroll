#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Builds the unsigned (ad-hoc signed) macOS disk image from a finished CMake build.
#
#   build-dmg.sh [--build DIR] [--out DIR]
#
#   --build DIR  CMake build directory with dofscroll, dofscroll-hidprobe and dofscroll-ui (default: build)
#   --out DIR    output directory (default: dist-installers)
#
# dofscroll.app (UI, LSUIElement) with the daemon, dofscroll-hidprobe, devices, examples and the
# LaunchAgent scripts inside Contents/Resources (the guided driver test in Contents/Resources/diagnostics),
# Qt frameworks from macdeployqt (needs it in PATH), result dofscroll-<version>-macos-arm64.dmg.
# The interface and the daemon are both required: there is no daemon-only image, a missing dofscroll-ui
# or macdeployqt is an error.
# Ad-hoc signing (codesign --sign -) is what lets arm64 code run; it is not a Developer ID signature.
set -eu

BUILD=build
OUT=dist-installers
while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        *) echo "build-dmg: unknown option '$1'" >&2; exit 2 ;;
    esac
done

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
VERSION=$(sed -n 's/^project(dofscroll VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
[ -n "$VERSION" ] || { echo "build-dmg: no project version in CMakeLists.txt" >&2; exit 1; }
[ -f "$BUILD/dofscroll" ] || { echo "build-dmg: $BUILD/dofscroll not built" >&2; exit 1; }
[ -f "$BUILD/dofscroll-ui" ] || { echo "build-dmg: $BUILD/dofscroll-ui not built (every image contains the interface and the daemon; build with -DDOFSCROLL_UI=ON)" >&2; exit 1; }
command -v macdeployqt >/dev/null 2>&1 || { echo "build-dmg: macdeployqt not in PATH" >&2; exit 1; }
[ -f "$BUILD/dofscroll-hidprobe" ] || { echo "build-dmg: $BUILD/dofscroll-hidprobe not built" >&2; exit 1; }
[ -f "$ROOT/scripts/test-driver-coexistence.sh" ] || { echo "build-dmg: scripts/test-driver-coexistence.sh missing" >&2; exit 1; }

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAGE="$OUT/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

NAME="dofscroll-$VERSION-macos-arm64"
APP="$STAGE/dofscroll.app"
RES="$APP/Contents/Resources"
mkdir -p "$APP/Contents/MacOS" "$RES"
cp "$BUILD/dofscroll-ui" "$APP/Contents/MacOS/dofscroll-ui"
# German catalog: dofscroll-ui looks for dofscroll_<lang>.qm next to its executable.
for qm in "$BUILD"/dofscroll_*.qm; do
    [ -f "$qm" ] && cp "$qm" "$APP/Contents/MacOS/"
done
sed "s|@VERSION@|$VERSION|g" "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"
DAEMON_DIR="$RES/daemon"

# Daemon with the data directories it looks for next to its executable.
mkdir -p "$DAEMON_DIR"
cp "$BUILD/dofscroll" "$DAEMON_DIR/dofscroll"
# Diagnostic tool and the guided driver coexistence test: next to the daemon (they find ./devices
# there). In the app they sit in Contents/Resources/daemon and Contents/Resources/diagnostics.
cp "$BUILD/dofscroll-hidprobe" "$DAEMON_DIR/dofscroll-hidprobe"
mkdir -p "$RES/diagnostics"
cp "$ROOT/scripts/test-driver-coexistence.sh" "$RES/diagnostics/test-driver-coexistence.sh"
chmod 755 "$RES/diagnostics/test-driver-coexistence.sh"
cp -R "$ROOT/devices" "$DAEMON_DIR/devices"
cp -R "$ROOT/examples" "$DAEMON_DIR/examples"
mkdir -p "$RES/launchagent"
cp "$HERE/install-launchagent.sh" "$HERE/uninstall-launchagent.sh" "$HERE/io.github.dofscroll.daemon.plist.in" \
    "$RES/launchagent/"

# Icon: SVG -> PNG (Quick Look) -> .icns (iconutil). Optional: the app works without it.
ICONSET="$OUT/dofscroll.iconset"
rm -rf "$ICONSET" "$OUT/icon-src"
mkdir -p "$ICONSET" "$OUT/icon-src"
if qlmanage -t -s 1024 -o "$OUT/icon-src" "$ROOT/icons/dofscroll-app.svg" >/dev/null 2>&1 \
    && [ -f "$OUT/icon-src/dofscroll-app.svg.png" ]; then
    src="$OUT/icon-src/dofscroll-app.svg.png"
    for s in 16 32 128 256 512; do
        sips -z "$s" "$s" "$src" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
        sips -z $((s * 2)) $((s * 2)) "$src" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$RES/dofscroll.icns" || echo "build-dmg: iconutil failed, no icon" >&2
else
    echo "build-dmg: could not render the SVG icon, building without an icon" >&2
fi
rm -rf "$ICONSET" "$OUT/icon-src"

macdeployqt "$APP" -always-overwrite

# Ad-hoc signing, inside out: the daemon first, then the bundle (which seals the frameworks too).
codesign --force --sign - "$DAEMON_DIR/dofscroll"
codesign --force --sign - "$DAEMON_DIR/dofscroll-hidprobe"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$STAGE/"
sed "s|@VERSION@|$VERSION|g" "$HERE/README-first-run.txt" > "$STAGE/README-first-run.txt"
ln -s /Applications "$STAGE/Applications"

# hdiutil sometimes fails with "Resource busy" on CI runners; retry up to 3 times.
attempt=1
while :; do
    rm -f "$OUT/$NAME.dmg"
    if hdiutil create -volname "dofscroll $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$OUT/$NAME.dmg"; then
        break
    fi
    if [ "$attempt" -ge 3 ]; then
        echo "build-dmg: hdiutil failed after $attempt attempts" >&2
        exit 1
    fi
    echo "build-dmg: hdiutil failed (attempt $attempt of 3), retrying" >&2
    attempt=$((attempt + 1))
    sleep 5
done
echo "build-dmg: $OUT/$NAME.dmg"
