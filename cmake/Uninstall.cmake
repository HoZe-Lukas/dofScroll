# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Script mode: cmake -DMANIFEST=<build>/install_manifest.txt -DPREFIX=<install prefix> -P cmake/Uninstall.cmake
# Removes the files recorded by the last `cmake --install` (honors the DESTDIR environment variable).
# Directories that became empty are removed only below PREFIX and only when a path component of the directory
# (relative to PREFIX) is exactly "dofscroll" (share/dofscroll, share/dofscroll/examples, share/doc/dofscroll, ...);
# the prefix itself and anything above it are never touched.
cmake_policy(SET CMP0057 NEW)  # IN_LIST
if(NOT EXISTS "${MANIFEST}")
  message(FATAL_ERROR "${MANIFEST} not found: run `cmake --install` first")
endif()
if(NOT PREFIX)
  message(FATAL_ERROR "PREFIX is not set: pass -DPREFIX=<install prefix> (the uninstall target does)")
endif()
get_filename_component(prefix "$ENV{DESTDIR}${PREFIX}" ABSOLUTE)
file(STRINGS "${MANIFEST}" files)
foreach(f ${files})
  set(f "$ENV{DESTDIR}${f}")
  if(EXISTS "${f}" OR IS_SYMLINK "${f}")
    message("-- Removing ${f}")
    file(REMOVE "${f}")
    get_filename_component(d "${f}" DIRECTORY)
    # drop now-empty dofscroll directories (never anything else, never the prefix or above)
    while(NOT "${d}" STREQUAL "${prefix}" AND IS_DIRECTORY "${d}")
      file(RELATIVE_PATH rel "${prefix}" "${d}")
      if(rel MATCHES "^\\.\\./" OR rel STREQUAL "..")
        break()  # not below the prefix
      endif()
      string(REPLACE "/" ";" parts "${rel}")
      if(NOT "dofscroll" IN_LIST parts)
        break()
      endif()
      file(GLOB rest "${d}/*")
      if(rest)
        break()
      endif()
      file(REMOVE_RECURSE "${d}")
      get_filename_component(d "${d}" DIRECTORY)
    endwhile()
  endif()
endforeach()
