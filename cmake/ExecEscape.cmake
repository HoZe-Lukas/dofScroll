# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# dofscroll_exec_escape(<in> <out>): escapes a directory for use inside the double quotes of a
# systemd ExecStart= / ExecStopPost= word: backslash and double quote are escaped, '%' (specifier)
# and '$' (variable expansion) are doubled. <out> is the escaped text; <out>_Q_ is a double quote
# when the word must be quoted (it contains a space or an escaped character), else empty, so plain
# paths such as /usr/bin stay unquoted. Templates: ExecStart=@Q@@DIR@/dofscroll@Q@.
# Non-ASCII characters need nothing (unit files are UTF-8).
function(dofscroll_exec_escape in out)
  set(s "${in}")
  string(REPLACE "\\" "\\\\" s "${s}")
  string(REPLACE "\"" "\\\"" s "${s}")
  string(REPLACE "%" "%%" s "${s}")
  string(REPLACE "$" "$$" s "${s}")
  set(q "")
  if(NOT "${s}" STREQUAL "${in}" OR "${in}" MATCHES "[ \t]")
    set(q "\"")
  endif()
  set(${out} "${s}" PARENT_SCOPE)
  set(${out}_Q_ "${q}" PARENT_SCOPE)
endfunction()
