# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Build image of the Rocky Linux 9 CI jobs: every dependency that the jobs used to install with
# dnf on each run (build + ctest, ASan/UBSan, cppcheck, CodeQL, RPM package). Built and pushed to
# ghcr.io by .github/workflows/ci-image.yml; the jobs only pull it. Two targets:
#   base  the dependencies of the daemon and the GTK tray, plus the analysis and packaging tools
#   qt    base plus Qt6 (the RPM job builds the Qt interface; the other jobs must not see Qt)
FROM rockylinux:9 AS base
# Links the package to the repository (visibility, permissions).
LABEL org.opencontainers.image.source="https://github.com/HoZe-Lukas/dofScroll"
LABEL org.opencontainers.image.description="Build image for the dofscroll CI (Rocky Linux 9)"
RUN dnf install -y epel-release \
 && dnf install -y gcc-c++ make cmake git gettext pkgconf-pkg-config spacenavd \
      libspnav-devel libX11-devel libXtst-devel gtk3-devel libappindicator-gtk3-devel \
      libasan libubsan cppcheck rpm-build cpio desktop-file-utils \
 && dnf clean all \
 && rm -rf /var/cache/dnf

FROM base AS qt
RUN dnf install -y qt6-qtbase-devel qt6-qtsvg-devel qt6-linguist \
 && dnf clean all \
 && rm -rf /var/cache/dnf
