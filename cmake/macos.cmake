# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# macOS daemon, included by the top-level CMakeLists.txt when APPLE. Backends: src/platform/mac/
# (IOHID input, CGEvent output, NSWorkspace active app in Objective-C++, Unix-socket reload).
# Code signing, hardened runtime and notarization are not part of this file (package W4).
#
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
#   build/dofscroll --version
#
# Option DOFSCROLL_BUILD_DAEMON (ON) builds the daemon; OFF builds only the core and the tests.

option(DOFSCROLL_BUILD_DAEMON "Build the macOS daemon (needs the macOS SDK)" ON)

if(DOFSCROLL_BUILD_DAEMON)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

  file(GLOB MAC_SRC CONFIGURE_DEPENDS
       ${CMAKE_SOURCE_DIR}/src/platform/mac/*.cpp
       ${CMAKE_SOURCE_DIR}/src/platform/mac/*.mm)

  add_executable(dofscroll ${MAC_SRC})
  target_link_libraries(dofscroll PRIVATE dofscroll_core
    "-framework IOKit"
    "-framework ApplicationServices"
    "-framework CoreGraphics"
    "-framework CoreFoundation"
    "-framework AppKit")

  # Hardening of the daemon (the Linux flags in CMakeLists.txt are ELF specific): stack protector,
  # fortified libc calls with optimization, format-string warnings, position independent
  # executable (ASLR). Hardened runtime and signing belong to the installer package (W4).
  if(DOFSCROLL_HARDEN)
    set_target_properties(dofscroll PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(dofscroll PRIVATE -fstack-protector-strong -Wformat -Wformat-security
      $<$<NOT:$<OR:$<CONFIG:Debug>,$<BOOL:${DOFSCROLL_SANITIZE}>>>:-D_FORTIFY_SOURCE=2>)
    target_link_options(dofscroll PRIVATE -Wl,-pie)
  endif()

  # Run from the build tree: the device descriptions and the example config sit next to the binary
  # (mac_platform.cpp looks for <exe dir>/devices; seeding looks for <exe dir>/examples).
  add_custom_command(TARGET dofscroll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/devices $<TARGET_FILE_DIR:dofscroll>/devices
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:dofscroll>/examples
    COMMENT "Copying devices/ and examples/ next to dofscroll")

  install(TARGETS dofscroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # <prefix>/bin/../share/dofscroll/devices is the second search location of the daemon.
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/devices DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll)
endif()

# W17: running-programs provider (src/platform/mac/running_apps_mac.mm, already in MAC_SRC through
# the glob above); the self-test runs it on the runner (asserts only: no crash, a list).
if(DOFSCROLL_BUILD_DAEMON AND BUILD_TESTING)
  add_executable(running_apps_selftest ${CMAKE_SOURCE_DIR}/tests/selftest_running_apps.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/mac/running_apps_mac.mm)
  target_link_libraries(running_apps_selftest PRIVATE dofscroll_core "-framework AppKit")
  set_target_properties(running_apps_selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME running_apps_selftest COMMAND running_apps_selftest)
endif()

# W18: renders the LaunchAgent plists into a temporary directory (nothing is loaded into launchd)
if(BUILD_TESTING)
  add_executable(autostart_mac_render tests/autostart_mac_render.cpp src/platform/mac/autostart_mac.cpp)
  target_link_libraries(autostart_mac_render PRIVATE dofscroll_core)
  set_target_properties(autostart_mac_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME autostart_mac_render COMMAND autostart_mac_render)
endif()
