#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Debian 12 counterpart of the `qt` target of rocky9.Dockerfile, run as root inside the DEB build
# container (linux-packages.yml): installs the libraries that the bundled Qt needs at run time (so that
# scripts/bundle-qt-linux.sh finds and copies them), patchelf, and Qt of packaging/qt.env from
# download.qt.io with aqtinstall. Qt ends up in /opt/qt/current. Usage: debian12-qt.sh <repo root>
set -eu
root="${1:?repository root}"
. "$root/packaging/qt.env"
export DEBIAN_FRONTEND=noninteractive

retry() { for i in 1 2 3; do timeout 600 "$@" && return 0; echo "attempt $i/3 failed: $*" >&2; sleep 10; done; return 1; }

retry apt-get update
retry apt-get install -y --no-install-recommends python3-venv python3-pip patchelf file \
    libgl1-mesa-dev libxkbcommon-dev libfontconfig1-dev libfreetype-dev libx11-dev libdbus-1-dev \
    libfontconfig1 libfreetype6 libharfbuzz0b libpng16-16 libxml2 libdbus-1-3 libglib2.0-0 \
    libxkbcommon0 libxkbcommon-x11-0 libxcb1 libx11-6 libx11-xcb1 libxext6 libxau6 \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-render0 \
    libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxcb-randr0 libxcb-util1 \
    libegl1 libgl1 libopengl0 libglx0 libwayland-client0 libwayland-cursor0 libwayland-egl1 zlib1g

python3 -m venv /opt/aqt-venv
retry /opt/aqt-venv/bin/pip install --no-cache-dir "aqtinstall==${AQT_VERSION}"
retry /opt/aqt-venv/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O /opt/qt
ln -sfn "/opt/qt/${QT_VERSION}/gcc_64" /opt/qt/current
echo "Qt ${QT_VERSION} in /opt/qt/current"
