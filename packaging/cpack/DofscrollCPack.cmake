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
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_REQUIRES "spacenavd")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/lib /usr/lib/systemd /usr/lib/systemd/user /usr/lib/udev /usr/lib/udev/rules.d)

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "spacenavd")

include(CPack)
