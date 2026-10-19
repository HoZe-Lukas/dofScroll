#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Builds the unsigned (ad-hoc signed) macOS disk image from a finished CMake build.
#
#   build-dmg.sh [--build DIR] [--out DIR]
#
#   --build DIR  CMake build directory with dofscroll and optionally dofscroll-ui (default: build)
#   --out DIR    output directory (default: dist-installers)
#
# With dofscroll-ui present: dofscroll.app (UI, LSUIElement) with the daemon, devices, examples and
# the LaunchAgent scripts inside Contents/Resources, Qt frameworks from macdeployqt (needs it in
# PATH), result dofscroll-<version>-macos-arm64.dmg. Without the UI: a plain folder with the
# daemon, result dofscroll-<version>-macos-arm64-daemon-only.dmg.
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

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAGE="$OUT/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"

HAS_UI=0
[ -f "$BUILD/dofscroll-ui" ] && HAS_UI=1

if [ "$HAS_UI" -eq 1 ]; then
    NAME="dofscroll-$VERSION-macos-arm64"
    APP="$STAGE/dofscroll.app"
    RES="$APP/Contents/Resources"
    mkdir -p "$APP/Contents/MacOS" "$RES"
    cp "$BUILD/dofscroll-ui" "$APP/Contents/MacOS/dofscroll-ui"
    sed "s|@VERSION@|$VERSION|g" "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"
    DAEMON_DIR="$RES/daemon"
else
    NAME="dofscroll-$VERSION-macos-arm64-daemon-only"
    DAEMON_DIR="$STAGE/dofscroll"
    RES="$DAEMON_DIR"
fi

# Daemon with the data directories it looks for next to its executable.
mkdir -p "$DAEMON_DIR"
cp "$BUILD/dofscroll" "$DAEMON_DIR/dofscroll"
cp -R "$ROOT/devices" "$DAEMON_DIR/devices"
cp -R "$ROOT/examples" "$DAEMON_DIR/examples"
mkdir -p "$RES/launchagent"
cp "$HERE/install-launchagent.sh" "$HERE/uninstall-launchagent.sh" "$HERE/io.github.dofscroll.daemon.plist.in" \
    "$RES/launchagent/"

# Icon: SVG -> PNG (Quick Look) -> .icns (iconutil). Optional: the app works without it.
if [ "$HAS_UI" -eq 1 ]; then
    ICONSET="$OUT/dofscroll.iconset"
    rm -rf "$ICONSET" "$OUT/icon-src"
    mkdir -p "$ICONSET" "$OUT/icon-src"
    if qlmanage -t -s 1024 -o "$OUT/icon-src" "$ROOT/icons/dofscroll-tray.svg" >/dev/null 2>&1 \
        && [ -f "$OUT/icon-src/dofscroll-tray.svg.png" ]; then
        src="$OUT/icon-src/dofscroll-tray.svg.png"
        for s in 16 32 128 256 512; do
            sips -z "$s" "$s" "$src" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
            sips -z $((s * 2)) $((s * 2)) "$src" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
        done
        iconutil -c icns "$ICONSET" -o "$RES/dofscroll.icns" || echo "build-dmg: iconutil failed, no icon" >&2
    else
        echo "build-dmg: could not render the SVG icon, building without an icon" >&2
    fi
    rm -rf "$ICONSET" "$OUT/icon-src"
fi

if [ "$HAS_UI" -eq 1 ]; then
    command -v macdeployqt >/dev/null 2>&1 || { echo "build-dmg: macdeployqt not in PATH" >&2; exit 1; }
    macdeployqt "$APP" -always-overwrite
fi

# Ad-hoc signing, inside out: the daemon first, then the bundle (which seals the frameworks too).
codesign --force --sign - "$DAEMON_DIR/dofscroll"
if [ "$HAS_UI" -eq 1 ]; then
    codesign --force --deep --sign - "$APP"
    codesign --verify --deep --strict "$APP"
fi

cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$STAGE/"
sed "s|@VERSION@|$VERSION|g" "$HERE/README-first-run.txt" > "$STAGE/README-first-run.txt"
[ "$HAS_UI" -eq 1 ] && ln -s /Applications "$STAGE/Applications"

rm -f "$OUT/$NAME.dmg"
hdiutil create -volname "dofscroll $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$OUT/$NAME.dmg"
echo "build-dmg: $OUT/$NAME.dmg"
