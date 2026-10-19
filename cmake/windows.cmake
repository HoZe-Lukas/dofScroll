# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Windows daemon (included from the top-level CMakeLists.txt when WIN32). Builds dofscroll.exe, a
# GUI-subsystem program (no console window) from src/platform/win/*.cpp; the shared core and the
# tests are defined by the top-level file. See docs/port-windows.md.
option(DOFSCROLL_BUILD_DAEMON "Build the Windows daemon (dofscroll.exe)" ON)

if(DOFSCROLL_BUILD_DAEMON)
  file(GLOB WIN_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp)
  add_executable(dofscroll WIN32 ${WIN_SRC})
  target_link_libraries(dofscroll PRIVATE dofscroll_core hid setupapi user32 advapi32 shell32 bcrypt)
  if(MINGW)
    # wWinMain entry point (UTF-16 command line) instead of WinMain
    target_link_options(dofscroll PRIVATE -municode)
    # self-contained executable: no dependency on the MinGW runtime DLLs next to it
    target_link_options(dofscroll PRIVATE -static)
  endif()

  # The program looks for ./devices (HID descriptions) and ./examples (first-start seeding) next to
  # the executable; copy them into the build output so a build tree is runnable as it is.
  add_custom_command(TARGET dofscroll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/devices $<TARGET_FILE_DIR:dofscroll>/devices
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:dofscroll>/examples
    COMMENT "Copying devices/ and examples/ next to dofscroll.exe" VERBATIM)

  install(TARGETS dofscroll RUNTIME DESTINATION bin)
  install(DIRECTORY devices/ DESTINATION bin/devices FILES_MATCHING PATTERN "*.conf")
  install(DIRECTORY examples/ DESTINATION bin/examples)
endif()
