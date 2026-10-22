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
# subpackage). With DOFSCROLL_BUNDLE_QT (the release packages) Qt and the libraries it needs are in
# /usr/lib/dofscroll (cmake/Ui.cmake, scripts/bundle-qt-linux.sh), so nothing is required from the
# system's Qt: the RPM and DEB name only the system libraries that stay outside the bundle. rpm must
# neither offer the private libraries to other packages nor require what the bundle brings itself.
if(TARGET dofscroll-ui AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(DOFSCROLL_BUNDLE_QT)
    # spacenavd is the device service (the daemon needs it); the libraries of the daemon (libspnav,
    # libX11, libXtst, libstdc++) are found by rpm itself; the interface needs nothing else.
    set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd")
    set(CPACK_RPM_SPEC_MORE_DEFINE "%global __provides_exclude_from ^/usr/lib/dofscroll/.*$
%global __requires_exclude_from ^/usr/lib/dofscroll/.*$
%global __requires_exclude ^lib(Qt6|GLX|OpenGL).*$
%global _build_id_links none")   # the bundled copies would link the same build ids as the system libraries
    # dpkg-shlibdeps resolves the bundled libraries in their private directory (no dependency)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "/usr/lib/dofscroll/lib")
  else()
    # Qt of the system (distribution builds from source): RPM names the Qt GUI runtime explicitly
    # (it carries the xcb platform plugin, which rpm's library scan cannot see); DEB gets the libqt6*
    # packages from SHLIBDEPS and the plugin lives in libqt6gui6.
    set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd, qt6-qtbase-gui")
    if(TARGET Qt6::Svg)  # the icon is an SVG: needs the Qt SVG image plugin at run time
      string(APPEND CPACK_RPM_PACKAGE_REQUIRES ", qt6-qtsvg")
    endif()
  endif()
endif()

# Upgrade steps, only for an UPGRADE (not a fresh install), best effort, never failing the transaction:
# packaging/cpack/preinst stops the running user units (interface, then daemon) of every logged-in user before
# the files are replaced, packaging/cpack/postinst starts the same ones again afterwards and moves users of the
# removed GTK tray unit to dofscroll-ui.service. The files must be called preinst and postinst for DEB; RPM runs
# them as %pre and %post.
set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/packaging/cpack/preinst)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/packaging/cpack/postinst)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_SOURCE_DIR}/packaging/cpack/preinst
                                       ${CMAKE_SOURCE_DIR}/packaging/cpack/postinst)

include(CPack)
