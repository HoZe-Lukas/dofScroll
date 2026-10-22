# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Build image of the Rocky Linux 9 CI jobs: every dependency that the jobs used to install with
# dnf on each run (build + ctest, ASan/UBSan, cppcheck, CodeQL, RPM package). Built and pushed to
# ghcr.io by .github/workflows/ci-image.yml; the jobs only pull it. Two targets:
#   base  the dependencies of the daemon, plus the analysis and packaging tools
#   qt    base plus Qt6 (the RPM job builds the Qt interface; the other jobs must not see Qt)
FROM rockylinux:9@sha256:d7be1c094cc5845ee815d4632fe377514ee6ebcf8efaed6892889657e5ddaaa6 AS base
# Links the package to the repository (visibility, permissions).
LABEL org.opencontainers.image.source="https://github.com/HoZe-Lukas/dofScroll"
LABEL org.opencontainers.image.description="Build image for the dofscroll CI (Rocky Linux 9)"
RUN dnf install -y epel-release \
 && dnf install -y gcc-c++ make cmake git gettext pkgconf-pkg-config spacenavd \
      libspnav-devel libX11-devel libXtst-devel \
      libasan libubsan cppcheck rpm-build cpio desktop-file-utils \
 && dnf clean all \
 && rm -rf /var/cache/dnf

# The base image is pinned by digest (scripts/pin-ci-images.sh refreshes it); aqtinstall is installed
# with hashes (aqt-requirements.txt, scripts/update-aqt-requirements.sh).
# Qt: the version of packaging/qt.env, from download.qt.io with aqtinstall. The Linux packages
# BUNDLE this Qt (and the libraries it needs, scripts/bundle-qt-linux.sh), so they do not depend on
# the Qt of the system: building against the same Qt that is shipped avoids a requirement such as
# libQt6Core.so.6(Qt_6.10) on the user's machine (#116). The runtime libraries below are only
# installed so that the bundle script finds (and copies) them; the build context is packaging/.
# Qt is in /opt/qt/current (CMAKE_PREFIX_PATH); EPEL's Qt is not used. To change the version:
# packaging/qt.env, docs/release-process.md.
FROM base AS qt
COPY qt.env ci/aqt-requirements.txt /opt/
RUN dnf install -y python3-pip patchelf file libglvnd-devel mesa-libGL-devel libxkbcommon-devel \
      fontconfig-devel freetype-devel libX11-devel dbus-devel \
      fontconfig freetype harfbuzz libpng libxml2 dbus-libs glib2 libxkbcommon libxkbcommon-x11 \
      libxcb xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util-wm xcb-util-cursor \
      libglvnd-glx libglvnd-egl libglvnd-opengl libwayland-client libwayland-cursor libwayland-egl \
      libXext \
 && . /opt/qt.env \
 && pip3 install --no-cache-dir --require-hashes --no-deps -r /opt/aqt-requirements.txt \
 && aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O /opt/qt \
 && ln -s "/opt/qt/${QT_VERSION}/gcc_64" /opt/qt/current \
 && pip3 uninstall -y aqtinstall \
 && dnf clean all \
 && rm -rf /var/cache/dnf /root/.cache /root/aqtinstall.log
ENV CMAKE_PREFIX_PATH=/opt/qt/current
ENV PATH=/opt/qt/current/bin:${PATH}
