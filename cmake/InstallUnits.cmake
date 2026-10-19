# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Included from cmake_install.cmake: renders systemd/*.service.in for the install prefix.
if(IS_ABSOLUTE "${DOFSCROLL_BINDIR_REL}")
  set(DOFSCROLL_BINDIR "${DOFSCROLL_BINDIR_REL}")
else()
  set(DOFSCROLL_BINDIR "${CMAKE_INSTALL_PREFIX}/${DOFSCROLL_BINDIR_REL}")
endif()
set(_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
file(MAKE_DIRECTORY "${_dir}")
foreach(u ${DOFSCROLL_UNITS})
  configure_file("${DOFSCROLL_SRC}/systemd/${u}.service.in" "${_dir}/${u}.service" @ONLY)
  message(STATUS "Installing: ${_dir}/${u}.service")
  list(APPEND CMAKE_INSTALL_MANIFEST_FILES "${_dir}/${u}.service")
endforeach()
