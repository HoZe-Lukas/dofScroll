# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# systemd user units and udev rules: templates systemd/*.service.in get the real
# binary directory of the install prefix. systemd and udev only read <prefix>/lib/...,
# never lib64, so these directories do NOT use CMAKE_INSTALL_LIBDIR.
set(DOFSCROLL_UNITS dofscroll)
if(DOFSCROLL_BUILD_TRAY)
  list(APPEND DOFSCROLL_UNITS dofscroll-tray)
endif()

# Developer units (symlink flow, `install-user-links`): binaries are linked to /usr/local/bin.
set(DOFSCROLL_BINDIR /usr/local/bin)
foreach(u ${DOFSCROLL_UNITS})
  configure_file(${CMAKE_SOURCE_DIR}/systemd/${u}.service.in
                 ${CMAKE_BINARY_DIR}/systemd-dev/${u}.service @ONLY)
endforeach()

# Installed units: generated at install time, so that cmake --install --prefix and CPack
# (CPACK_PACKAGING_INSTALL_PREFIX) both see the final prefix.
install(CODE "set(DOFSCROLL_UNITS \"${DOFSCROLL_UNITS}\")
set(DOFSCROLL_SRC \"${CMAKE_SOURCE_DIR}\")
set(DOFSCROLL_BINDIR_REL \"${CMAKE_INSTALL_BINDIR}\")
include(\"${CMAKE_SOURCE_DIR}/cmake/InstallUnits.cmake\")")

file(GLOB UDEV_RULES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/packaging/*.rules)
if(UDEV_RULES)
  install(FILES ${UDEV_RULES} DESTINATION lib/udev/rules.d)
endif()
