# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Build image of the Rocky Linux 9 CI jobs: every dependency that the jobs used to install with
# dnf on each run (build + ctest, ASan/UBSan, cppcheck, CodeQL, RPM package). Built and pushed to
# ghcr.io by .github/workflows/ci-image.yml; the jobs only pull it. Two targets:
#   base  the dependencies of the daemon, plus the analysis and packaging tools
#   qt    base plus Qt6 (the RPM job builds the Qt interface; the other jobs must not see Qt)
FROM rockylinux:9 AS base
# Links the package to the repository (visibility, permissions).
LABEL org.opencontainers.image.source="https://github.com/HoZe-Lukas/dofScroll"
LABEL org.opencontainers.image.description="Build image for the dofscroll CI (Rocky Linux 9)"
RUN dnf install -y epel-release \
 && dnf install -y gcc-c++ make cmake git gettext pkgconf-pkg-config spacenavd \
      libspnav-devel libX11-devel libXtst-devel \
      libasan libubsan cppcheck rpm-build cpio desktop-file-utils \
 && dnf clean all \
 && rm -rf /var/cache/dnf

# Qt is pinned to 6.6.3 (aqtinstall, from download.qt.io), the OLDEST Qt 6 the RPM supports. A
# program built against Qt 6.N carries the symbol qt_version_tag@Qt_6.N, so the RPM requires
# libQt6Core.so.6(Qt_6.N): building against the newest EPEL Qt (6.10) made the 1.0.0 RPM
# uninstallable on RHEL 9 systems with Qt 6.6. Built against 6.6 it installs with every Qt >= 6.6
# (the soname libQt6*.so.6 stays the same) and runs with Qt 6.10 as well. The Qt libraries stay in
# /opt/qt and are used for building only: CPack removes the build RPATH on install and the RPM
# depends on the system's libQt6*.so.6 (qt6-qtbase-gui and qt6-qtsvg from EPEL). EPEL keeps only
# its newest Qt, and the koji builds of 6.6.2 cannot be installed on a current Rocky 9 (qt6-linguist
# needs LLVM 17), so aqtinstall is the reproducible source. To change the version: docs/release-process.md.
FROM base AS qt
ARG QT_VERSION=6.6.3
ARG AQT_VERSION=3.1.19
RUN dnf install -y python3-pip libglvnd-devel mesa-libGL-devel libxkbcommon-devel fontconfig-devel \
      freetype-devel libX11-devel dbus-devel \
 && pip3 install --no-cache-dir "aqtinstall==${AQT_VERSION}" \
 && aqt install-qt linux desktop "${QT_VERSION}" gcc_64 -O /opt/qt \
 && pip3 uninstall -y aqtinstall \
 && dnf clean all \
 && rm -rf /var/cache/dnf /root/.cache /root/aqtinstall.log
ENV CMAKE_PREFIX_PATH=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH=/opt/qt/${QT_VERSION}/gcc_64/bin:${PATH}
