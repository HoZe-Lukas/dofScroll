# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# CPack configuration: RPM, DEB and TGZ for Linux.
set(CPACK_PACKAGE_NAME "dofscroll")
set(CPACK_PACKAGE_VENDOR "Lukas Hois-Zelinka")
set(CPACK_PACKAGE_CONTACT "Lukas Hois-Zelinka")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SpaceMouse to scroll/zoom/key bridge for X11")
set(CPACK_PACKAGE_DESCRIPTION
    "dofscroll turns a 3Dconnexion SpaceMouse (via spacenavd/libspnav) into X11 scroll, zoom, key and mouse-button events. Includes a Qt tray icon with a settings window (dofscroll-ui) when built with Qt6.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "TGZ;RPM;DEB")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_PACKAGING_INSTALL_PREFIX ${DOFSCROLL_PACKAGING_PREFIX})  # also used by cmake/InstallUnits.cmake

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd")
# Units live in /usr/lib (not lib64): these system directories must not be owned by the RPM.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/lib /usr/lib/systemd /usr/lib/systemd/user)

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "spacenavd")

# The Qt interface is part of the same package when the build found Qt6 (single package, no
# subpackage). RPM names the Qt GUI runtime explicitly
# (it carries the xcb platform plugin, which rpm's library scan cannot see); DEB gets the
# libqt6* packages from SHLIBDEPS and the plugin lives in libqt6gui6. The RPM job builds against
# Qt 6.6 (the oldest supported on the RHEL family), so the RPM requires Qt 6.6 or newer and rpm's
# own scan requires libQt6Core.so.6(Qt_6.6) at most (checked in linux-packages.yml).
set(DOFSCROLL_RPM_QT_MIN 6.6)
if(TARGET dofscroll-ui AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd, qt6-qtbase-gui >= ${DOFSCROLL_RPM_QT_MIN}")
  if(TARGET Qt6::Svg)  # the icon is an SVG: needs the Qt SVG image plugin at run time
    string(APPEND CPACK_RPM_PACKAGE_REQUIRES ", qt6-qtsvg >= ${DOFSCROLL_RPM_QT_MIN}")
  endif()
endif()

# Upgrade step: after an UPGRADE (not a fresh install) restart the running user units of every
# logged-in user and move users of the removed GTK tray unit to dofscroll-ui.service, best effort, never failing the transaction (packaging/cpack/postinst; the file
# must be called postinst for DEB, RPM runs the same file as %post).
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/packaging/cpack/postinst)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_SOURCE_DIR}/packaging/cpack/postinst)

include(CPack)
