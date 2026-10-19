# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Included from cmake_install.cmake: renders systemd/*.service.in for the install prefix.
# The prefix the files will have on the target system. Under CPack, CMAKE_INSTALL_PREFIX is
# the staging directory (<build>/_CPack_Packages/...), which must never end up in a unit;
# DOFSCROLL_PACKAGING_PREFIX (= CPACK_PACKAGING_INSTALL_PREFIX, baked in at configure time,
# the CPack variable itself is not visible in the install script) is the final location
# there. CPack staging is recognised by its fixed directory name. A plain
# `cmake --install --prefix X` has no CPack variable and keeps using X.
set(_final_prefix "${CMAKE_INSTALL_PREFIX}")
if(DOFSCROLL_PACKAGING_PREFIX AND CMAKE_INSTALL_PREFIX MATCHES "/_CPack_Packages/")
  set(_final_prefix "${DOFSCROLL_PACKAGING_PREFIX}")
endif()
if(IS_ABSOLUTE "${DOFSCROLL_BINDIR_REL}")
  set(DOFSCROLL_BINDIR "${DOFSCROLL_BINDIR_REL}")
else()
  set(DOFSCROLL_BINDIR "${_final_prefix}/${DOFSCROLL_BINDIR_REL}")
endif()
# Quoted and escaped for ExecStart= (spaces, % and $ in the prefix, docs/encoding.md).
include("${DOFSCROLL_SRC}/cmake/ExecEscape.cmake")
dofscroll_exec_escape("${DOFSCROLL_BINDIR}" DOFSCROLL_EXEC_BINDIR)
set(DOFSCROLL_EXEC_Q "${DOFSCROLL_EXEC_BINDIR_Q_}")
set(_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
file(MAKE_DIRECTORY "${_dir}")
foreach(u ${DOFSCROLL_UNITS})
  configure_file("${DOFSCROLL_SRC}/systemd/${u}.service.in" "${_dir}/${u}.service" @ONLY)
  message(STATUS "Installing: ${_dir}/${u}.service")
  list(APPEND CMAKE_INSTALL_MANIFEST_FILES "${_dir}/${u}.service")
endforeach()
