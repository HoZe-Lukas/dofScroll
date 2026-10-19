# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# ctest: .github/workflows/ports.yml must not run on every branch push (failure mails for work in
# progress): push only on main and tags, plus pull_request and workflow_dispatch.
#   cmake -DFILE=<ports.yml> -P tests/check_ports_trigger.cmake
file(READ "${FILE}" yml)
string(REGEX MATCH "\non:\n(.*)\npermissions:" _m "${yml}")
set(on "${CMAKE_MATCH_1}")
foreach(want "push:" "branches: \\[main\\]" "tags: \\[\"\\*\"\\]" "pull_request:" "workflow_dispatch:")
  if(NOT on MATCHES "${want}")
    message(FATAL_ERROR "ports.yml trigger section lacks '${want}'")
  endif()
endforeach()
if(on MATCHES "\\*\\*")
  message(FATAL_ERROR "ports.yml must not trigger on every branch ('**')")
endif()
message(STATUS "ports.yml trigger section OK")
