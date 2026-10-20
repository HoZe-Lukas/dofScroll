#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Linux packages: copy the Qt libraries that dofscroll-ui needs, every library they need in turn, and
# the Qt plugins into a private directory, so the package works without the Qt of the system and with
# as few system libraries as possible (docs/installation.md, docs/release-process.md).
#
#   bundle-qt-linux.sh <qt-prefix> <dest-dir> <dofscroll-ui> [<license-dir>]
#
#   qt-prefix    Qt of packaging/qt.env, for example /opt/qt/current
#   dest-dir     <staging>/usr/lib/dofscroll: gets lib/ and plugins/
#   dofscroll-ui the built executable (its dependencies are followed with ldd)
#   license-dir  optional: bundled-libraries.txt (library, source, version) and the license files of
#                the distribution packages that provided the libraries
#
# Only glibc and the C++ runtime stay on the system (every distribution has them, and the Qt
# libraries need no newer GLIBCXX than Rocky Linux 9 has). Everything else, including libX11 and the
# xcb libraries, OpenGL stubs, glib, dbus and fontconfig, is copied, so the interface starts on a
# system with nothing installed but the C library.
# Run it on the oldest target distribution (Rocky Linux 9 for RPM and TGZ, Debian 12 for the DEB):
# libraries copied from there run on every newer distribution.
set -eu

qt="${1:?Qt prefix}"; dest="${2:?destination}"; exe="${3:?dofscroll-ui}"; lic="${4:-}"

# Not copied: provided by every system that can run the package (the daemon links X11 as well).
system_re='^(ld-linux.*|libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|libutil\.so\.1|libresolv\.so\.2|libanl\.so\.1|libnsl\.so\.1|libstdc\+\+\.so\.6|libgcc_s\.so\.1)$'

# Qt plugins (directory/file below <qt>/plugins). xcb (X11, XWayland) and wayland for the two kinds of
# session, offscreen for the self-test; the portal theme gives dark mode and the platform look without
# KDE or GNOME plugins; SVG for the icons.
plugins=(
  platforms/libqxcb.so
  platforms/libqoffscreen.so
  platforms/libqwayland-generic.so
  platforms/libqwayland-egl.so
  platformthemes/libqxdgdesktopportal.so
  platforminputcontexts/libcomposeplatforminputcontextplugin.so
  platforminputcontexts/libibusplatforminputcontextplugin.so
  imageformats/libqsvg.so
  imageformats/libqgif.so
  imageformats/libqico.so
  imageformats/libqjpeg.so
  iconengines/libqsvgicon.so
  wayland-decoration-client/libadwaita.so
  wayland-decoration-client/libbradient.so
  wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
  wayland-graphics-integration-client/libshm-emulation-server.so
  wayland-shell-integration/libxdg-shell.so
  wayland-shell-integration/libwl-shell-plugin.so
  wayland-shell-integration/libfullscreen-shell-v1.so
  wayland-shell-integration/libivi-shell.so
  wayland-shell-integration/libqt-shell.so
)

[ -x "$exe" ] || { echo "bundle-qt: $exe is not an executable" >&2; exit 1; }
command -v patchelf >/dev/null || { echo "bundle-qt: patchelf is required" >&2; exit 1; }
rm -rf "$dest/lib" "$dest/plugins"
mkdir -p "$dest/lib"

queue=("$exe")
for p in "${plugins[@]}"; do
  src="$qt/plugins/$p"
  [ -f "$src" ] || { echo "bundle-qt: missing plugin $src" >&2; exit 1; }
  mkdir -p "$dest/plugins/$(dirname "$p")"
  cp -L "$src" "$dest/plugins/$p"
  queue+=("$dest/plugins/$p")
done

export LD_LIBRARY_PATH="$qt/lib:$dest/lib"
missing=0
declare -A sources   # soname -> resolved source file
i=0
while [ "$i" -lt "${#queue[@]}" ]; do
  f="${queue[$i]}"; i=$((i + 1))
  while read -r name target; do
    [[ "$name" =~ $system_re ]] && continue
    if [ "$target" = "not" ]; then
      echo "bundle-qt: $(basename "$f") needs $name, which is not installed in this build environment" >&2
      missing=1; continue
    fi
    [ -e "$dest/lib/$name" ] && continue
    cp -L "$target" "$dest/lib/$name"
    sources["$name"]="$(readlink -f "$target")"
    queue+=("$dest/lib/$name")
  done < <(ldd "$f" | awk '/=>/ { if ($3 == "not") print $1, "not"; else print $1, $3 }')
done
[ "$missing" -eq 0 ] || exit 1

# The package carries the ICU license text of the ICU version that Qt ships: a Qt update that brings
# another ICU needs the new text in third-party/licenses first.
for name in "${!sources[@]}"; do
  case "$name" in libicuuc.so.*)
    icu="$(basename "${sources[$name]}")"; icu="${icu#libicuuc.so.}"
    [ -f "$(cd "$(dirname "$0")/.." && pwd)/third-party/licenses/ICU-$icu-LICENSE.txt" ] || {
      echo "bundle-qt: Qt ships ICU $icu: add third-party/licenses/ICU-$icu-LICENSE.txt and update THIRD-PARTY-NOTICES.md" >&2; exit 1; } ;;
  esac
done

# Smaller files; a file that strip cannot handle stays as it is.
strip_safe() {
  cp "$1" "$1.orig"
  if (strip --strip-unneeded "$1" 2>/dev/null) 2>/dev/null; then rm -f "$1.orig"
  else mv "$1.orig" "$1"; echo "bundle-qt: kept $(basename "$1") unstripped"; fi
}

# Relative lookup: libraries find each other next to themselves, plugins two levels up (lib/).
for f in "$dest"/lib/*; do
  strip_safe "$f"   # before patchelf: strip can break patched files
  patchelf --set-rpath '$ORIGIN' "$f"
done
find "$dest/plugins" -name '*.so' | while read -r f; do
  strip_safe "$f"
  patchelf --set-rpath '$ORIGIN/../../lib' "$f"
done
chmod 0644 "$dest"/lib/* 2>/dev/null || true

# Check: with no search path of the environment, everything must resolve inside the bundle or in the
# system list above, and no Qt library may come from anywhere else.
unset LD_LIBRARY_PATH
bad=0
while read -r f; do
  out="$(ldd "$f" 2>&1)" || true
  if echo "$out" | grep -q 'not found'; then
    echo "bundle-qt: unresolved in $f:" >&2; echo "$out" | grep 'not found' >&2; bad=1
  fi
  if echo "$out" | grep -E 'libQt6|libicu' | grep -v "$dest" >/dev/null; then
    echo "bundle-qt: $f resolves Qt or ICU outside the bundle" >&2; bad=1
  fi
done < <(find "$dest/lib" "$dest/plugins" -type f)
[ "$bad" -eq 0 ] || exit 1

echo "bundle-qt: $(ls "$dest/lib" | wc -l) libraries and $(find "$dest/plugins" -type f | wc -l) plugins in $dest ($(du -sh "$dest" | cut -f1))"

# Inventory (library, source package, version, license, upstream) and the license files of the
# distribution packages. Qt and ICU are not from a distribution: their texts are in the repository
# (third-party/licenses). The inventory feeds THIRD-PARTY-NOTICES.md.
vendored="$(cd "$(dirname "$0")/.." && pwd)/third-party/licenses/distro"
copy_rpm_licenses() {   # <package> <destination directory>
  local pkg="$1" out="$2" srcname l
  srcname="$(rpm -q --qf '%{SOURCERPM}' "$pkg" | sed 's/-[^-]*-[^-]*\.src\.rpm$//')"
  mkdir -p "$out"
  # the package's own license files, then those of the other packages built from the same source
  { rpm -qL "$pkg"; ls -d /usr/share/licenses/"$pkg"/* /usr/share/licenses/"$srcname"*/* 2>/dev/null; } | sort -u | while read -r l; do
    [ -f "$l" ] && cp -L "$l" "$out/" 2>/dev/null || true
  done
  if [ -z "$(ls "$out" 2>/dev/null)" ]; then
    for l in /usr/share/doc/"$pkg"/{COPYING*,LICENSE*,LICENCE*,COPYRIGHT*} /usr/share/doc/"$srcname"*/{COPYING*,LICENSE*,LICENCE*,COPYRIGHT*}; do
      [ -f "$l" ] && cp -L "$l" "$out/" 2>/dev/null || true
    done
  fi
  # a few runtime packages carry no license file: the repository holds the upstream text
  if [ -z "$(ls "$out" 2>/dev/null)" ] && [ -d "$vendored/$srcname" ]; then cp -L "$vendored/$srcname"/* "$out/"; fi
  if [ -z "$(ls "$out" 2>/dev/null)" ]; then
    echo "bundle-qt: no license file for $pkg (source $srcname): add the upstream text to third-party/licenses/distro/$srcname/" >&2
    return 1
  fi
}
if [ -n "$lic" ]; then
  mkdir -p "$lic"
  inv="$lic/bundled-libraries.txt"
  printf '# library | source package | version | license | upstream\n' > "$inv"
  qtreal="$(readlink -f "$qt")"
  for name in $(printf '%s\n' "${!sources[@]}" | sort); do
    src="${sources[$name]}"; owner=""; ver=""; license=""; url=""
    case "$src" in
      "$qtreal"/*|/opt/qt/*)
        owner="qt.io"; ver="$(basename "$(dirname "$qtreal")")"
        case "$name" in
          libicu*) license="ICU (Unicode License)"; url="https://icu.unicode.org/" ;;
          *) license="LGPL-3.0-only"; url="https://www.qt.io/download-open-source" ;;
        esac ;;
      *)
        if command -v rpm >/dev/null && rpm -qf "$src" >/dev/null 2>&1; then
          owner="$(rpm -qf "$src" --qf '%{NAME}\n' | head -1)"
          ver="$(rpm -q --qf '%{VERSION}-%{RELEASE}' "$owner")"
          license="$(rpm -q --qf '%{LICENSE}' "$owner")"; url="$(rpm -q --qf '%{URL}' "$owner")"
          copy_rpm_licenses "$owner" "$lic/$owner" || exit 1
        elif command -v dpkg >/dev/null && dpkg -S "$src" >/dev/null 2>&1; then
          owner="$(dpkg -S "$src" | head -1 | cut -d: -f1)"
          ver="$(dpkg-query -W -f='${Version}' "$owner")"
          url="$(dpkg-query -W -f='${Homepage}' "$owner")"; license="see $owner/copyright"
          mkdir -p "$lic/$owner"
          [ -f "/usr/share/doc/$owner/copyright" ] && cp -L "/usr/share/doc/$owner/copyright" "$lic/$owner/copyright"
        fi ;;
    esac
    printf '%s | %s | %s | %s | %s\n' "$name" "$owner" "$ver" "$license" "$url" >> "$inv"
  done
  echo "bundle-qt: inventory in $inv"
fi
