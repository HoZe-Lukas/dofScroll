# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# systemd user units and udev rules: templates systemd/*.service.in get the real
# binary directory of the install prefix. systemd and udev only read <prefix>/lib/...,
# never lib64, so these directories do NOT use CMAKE_INSTALL_LIBDIR.
# Final install prefix of packages (CPack uses it as CPACK_PACKAGING_INSTALL_PREFIX).
set(DOFSCROLL_PACKAGING_PREFIX /usr)
set(DOFSCROLL_UNITS dofscroll)
if(DOFSCROLL_BUILD_TRAY)
  list(APPEND DOFSCROLL_UNITS dofscroll-tray)
endif()
if(TARGET dofscroll-ui)  # Qt UI unit, switched with the daemon by "Start at login" (W18)
  list(APPEND DOFSCROLL_UNITS dofscroll-ui)
endif()

# Developer units (symlink flow, `install-user-links`): binaries are linked to /usr/local/bin.
set(DOFSCROLL_BINDIR /usr/local/bin)
include(${CMAKE_SOURCE_DIR}/cmake/ExecEscape.cmake)
dofscroll_exec_escape("${DOFSCROLL_BINDIR}" DOFSCROLL_EXEC_BINDIR)
set(DOFSCROLL_EXEC_Q "${DOFSCROLL_EXEC_BINDIR_Q_}")
foreach(u ${DOFSCROLL_UNITS})
  configure_file(${CMAKE_SOURCE_DIR}/systemd/${u}.service.in
                 ${CMAKE_BINARY_DIR}/systemd-dev/${u}.service @ONLY)
endforeach()

# Installed units: generated at install time, so that cmake --install --prefix and CPack
# (CPACK_PACKAGING_INSTALL_PREFIX) both see the final prefix.
install(CODE "set(DOFSCROLL_UNITS \"${DOFSCROLL_UNITS}\")
set(DOFSCROLL_SRC \"${CMAKE_SOURCE_DIR}\")
set(DOFSCROLL_BINDIR_REL \"${CMAKE_INSTALL_BINDIR}\")
set(DOFSCROLL_PACKAGING_PREFIX \"${DOFSCROLL_PACKAGING_PREFIX}\")
include(\"${CMAKE_SOURCE_DIR}/cmake/InstallUnits.cmake\")")

# The uinput rule is NOT installed into the active udev directory: it opens /dev/uinput to
# the logged-in seat user and must be an explicit opt-in. It ships as an example under
# share/dofscroll/udev; `install-udev` (or a manual copy) activates it.
file(GLOB UDEV_RULES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/packaging/*.rules)
if(UDEV_RULES)
  install(FILES ${UDEV_RULES} DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll/udev)
endif()
