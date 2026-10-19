# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# CPack configuration: RPM, DEB and TGZ for Linux.
set(CPACK_PACKAGE_NAME "dofscroll")
set(CPACK_PACKAGE_VENDOR "Lukas Hois-Zelinka")
set(CPACK_PACKAGE_CONTACT "Lukas Hois-Zelinka")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SpaceMouse to scroll/zoom/key bridge for X11")
set(CPACK_PACKAGE_DESCRIPTION
    "dofscroll turns a 3Dconnexion SpaceMouse (via spacenavd/libspnav) into X11 scroll, zoom, key and mouse-button events. Includes an optional GTK tray with a settings window.")
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
# subpackage: the GTK tray ships in that package too). RPM names the Qt GUI runtime explicitly
# (it carries the xcb platform plugin, which rpm's library scan cannot see); DEB gets the
# libqt6* packages from SHLIBDEPS and the plugin lives in libqt6gui6.
if(TARGET dofscroll-ui AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd, qt6-qtbase-gui")
  if(TARGET Qt6::Svg)  # the icon is an SVG: needs the Qt SVG image plugin at run time
    string(APPEND CPACK_RPM_PACKAGE_REQUIRES ", qt6-qtsvg")
  endif()
endif()

include(CPack)
