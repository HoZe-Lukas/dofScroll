# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# CMake build for dofscroll.
#
#   cmake -S . -B build && cmake --build build -j4 && ctest --test-dir build
#   cmake --build build --target check-fast     # -Werror build + tests (pre-commit gate)
#   cmake --build build --target check          # + ASan/UBSan, cppcheck, hardening check
#   cmake --build build --target check-config   # validate the example configs
#   cmake --build build --target check-deps     # report missing packages
#   cmake --build build --target install-user-links | install-udev | uninstall
#   cmake --install build --prefix /usr         # or: cpack --config build/CPackConfig.cmake
#
# Options: DOFSCROLL_TRAY (ON, needs GTK3 + appindicator), DOFSCROLL_HARDEN (ON),
#          DOFSCROLL_SANITIZE (OFF, ASan/UBSan), DOFSCROLL_WERROR (OFF),
#          DOFSCROLL_CORE_ONLY (ON outside Linux: core library + tests, no daemon/tray; the
#          Windows and macOS backends are added by cmake/windows.cmake and cmake/macos.cmake).
#
# Packaging notes: Linux packages (RPM, DEB, TGZ) are produced by CPack, see
# packaging/cpack/DofscrollCPack.cmake. Windows and macOS packages are not
# produced by this build (see docs/windows-mac.md).

cmake_minimum_required(VERSION 3.16)
project(dofscroll VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "SpaceMouse to scroll/zoom/key bridge for X11")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -O2 without NDEBUG: the tests rely on assert(), which NDEBUG would remove.
if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob1 /Zi" CACHE STRING "" FORCE)
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O2" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "" FORCE)
endif()

option(DOFSCROLL_TRAY     "Build the GTK tray (needs gtk3 + appindicator)" ON)
option(DOFSCROLL_HARDEN   "Enable hardening compiler/linker flags" ON)
option(DOFSCROLL_SANITIZE "Build with ASan/UBSan" OFF)
option(DOFSCROLL_WERROR   "Treat warnings as errors" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(DOFSCROLL_CORE_ONLY "Build only the core library and the tests" OFF)
else()
  option(DOFSCROLL_CORE_ONLY "Build only the core library and the tests" ON)
endif()

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)
include(CTest)
if(NOT DOFSCROLL_CORE_ONLY)
  find_package(PkgConfig REQUIRED)
endif()
find_package(Threads REQUIRED)

# ---- flags ------------------------------------------
add_library(dofscroll_flags INTERFACE)
if(MSVC)
  target_compile_options(dofscroll_flags INTERFACE /W4 /utf-8 /permissive- /EHsc)
  target_compile_definitions(dofscroll_flags INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  if(DOFSCROLL_WERROR)
    target_compile_options(dofscroll_flags INTERFACE /WX)
  endif()
else()
  target_compile_options(dofscroll_flags INTERFACE -Wall -Wextra)
  if(DOFSCROLL_WERROR)
    target_compile_options(dofscroll_flags INTERFACE -Werror)
  endif()
endif()
if(WIN32 AND NOT MSVC)
  target_compile_definitions(dofscroll_flags INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Source files and string literals are UTF-8 (the MSVC equivalent is /utf-8 above).
    target_compile_options(dofscroll_flags INTERFACE -finput-charset=UTF-8 -fexec-charset=UTF-8)
  endif()
endif()
# The hardening flags below are GCC/Clang on Linux (ELF linker options); see docs/security.md
# for the Windows (set in cmake/windows.cmake) and macOS notes.
if(DOFSCROLL_HARDEN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_options(dofscroll_flags INTERFACE
    -fstack-protector-strong -D_GLIBCXX_ASSERTIONS -Wformat -Wformat-security)
  # _FORTIFY_SOURCE only works with optimization
  target_compile_options(dofscroll_flags INTERFACE
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<BOOL:${DOFSCROLL_SANITIZE}>>>:-U_FORTIFY_SOURCE>
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<BOOL:${DOFSCROLL_SANITIZE}>>>:-D_FORTIFY_SOURCE=2>)
  foreach(f -fstack-clash-protection -fcf-protection)
    string(MAKE_C_IDENTIFIER "HAVE${f}" var)
    check_cxx_compiler_flag(${f} ${var})
    if(${var})
      target_compile_options(dofscroll_flags INTERFACE ${f})
    endif()
  endforeach()
  target_link_options(dofscroll_flags INTERFACE
    -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -Wl,--as-needed)
endif()
if(DOFSCROLL_SANITIZE)
  target_compile_options(dofscroll_flags INTERFACE
    -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
    -fno-sanitize-recover=undefined)
  target_link_options(dofscroll_flags INTERFACE -fsanitize=address,undefined)
endif()

# ---- sources (globbed: new files need no CMake edit) --------------------------------------
file(GLOB ALL_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
set(PROGRAM_SRC ${CMAKE_SOURCE_DIR}/src/dofscroll.cpp ${CMAKE_SOURCE_DIR}/src/dofscroll_tray.cpp)
file(GLOB BACKEND_SRC CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/output*.cpp ${CMAKE_SOURCE_DIR}/src/activewin.cpp)
set(CORE_SRC ${ALL_SRC})
list(REMOVE_ITEM CORE_SRC ${PROGRAM_SRC} ${BACKEND_SRC})
# File-system layer: one implementation per OS family (src/platform/fs.hpp).
# Live feed (src/live_feed/): portable server/client plus one transport per OS family.
list(APPEND CORE_SRC ${CMAKE_SOURCE_DIR}/src/live_feed/live_feed.cpp)
if(WIN32)
  list(APPEND CORE_SRC ${CMAKE_SOURCE_DIR}/src/platform/fs_win.cpp
       ${CMAKE_SOURCE_DIR}/src/live_feed/live_feed_win.cpp)
else()
  list(APPEND CORE_SRC ${CMAKE_SOURCE_DIR}/src/platform/fs_posix.cpp
       ${CMAKE_SOURCE_DIR}/src/platform/unix_socket.cpp
       ${CMAKE_SOURCE_DIR}/src/live_feed/live_feed_posix.cpp)
endif()

add_library(dofscroll_core STATIC ${CORE_SRC})
target_include_directories(dofscroll_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dofscroll_core PUBLIC dofscroll_flags Threads::Threads)
if(WIN32)
  target_link_libraries(dofscroll_core PUBLIC advapi32 bcrypt)
endif()

set(DOFSCROLL_BUILD_TRAY OFF)
if(NOT DOFSCROLL_CORE_ONLY)
# ---- daemon -----------------------------------------------------------------
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xtst)
find_library(SPNAV_LIB spnav REQUIRED)
find_path(SPNAV_INCLUDE spnav.h REQUIRED)

file(GLOB LINUX_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/linux/*.cpp)
add_executable(dofscroll src/dofscroll.cpp ${BACKEND_SRC} ${LINUX_SRC})
target_include_directories(dofscroll PRIVATE ${SPNAV_INCLUDE})
target_link_libraries(dofscroll PRIVATE dofscroll_core PkgConfig::X11 ${SPNAV_LIB})

# ---- tray (optional) --------------------------------------------------------
if(DOFSCROLL_TRAY)
  pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0 appindicator3-0.1)
  if(GTK3_FOUND)
    set(DOFSCROLL_BUILD_TRAY ON)
    add_executable(dofscroll-tray src/dofscroll_tray.cpp)
    target_include_directories(dofscroll-tray PRIVATE ${SPNAV_INCLUDE})
    target_link_libraries(dofscroll-tray PRIVATE dofscroll_core PkgConfig::GTK3 ${SPNAV_LIB})
  else()
    message(WARNING "gtk3/appindicator not found: dofscroll-tray is not built")
  endif()
endif()
endif()  # NOT DOFSCROLL_CORE_ONLY

# ---- Windows / macOS daemon (backends live in src/platform/<os>/, see docs/port-architecture.md) ----
if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/cmake/windows.cmake)
  include(${CMAKE_SOURCE_DIR}/cmake/windows.cmake)
elseif(APPLE AND EXISTS ${CMAKE_SOURCE_DIR}/cmake/macos.cmake)
  include(${CMAKE_SOURCE_DIR}/cmake/macos.cmake)
endif()

include(cmake/Ui.cmake)  # optional Qt6 UI (dofscroll-ui), see docs/ui-options.md
add_subdirectory(tools/hidprobe) # optional diagnostic, option DOFSCROLL_HIDPROBE (see there)

# ---- tests ------------------------------------------------------------------
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
set(TEST_TARGETS)
if(BUILD_TESTING)
  foreach(t ${TEST_SRC})
    get_filename_component(name ${t} NAME_WE)
    # key_tables.hpp maps to X11 keysyms and Linux KEY_* codes (Linux headers only)
    if(name STREQUAL "test_keys" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
      continue()
    endif()
    # tools/hidprobe reads reports through POSIX fds (poll/pipe); a Windows reader is future work
    if(name STREQUAL "test_hidprobe" AND WIN32)
      continue()
    endif()
    # test_mode_list uses mkdtemp/stat/0600 permissions (POSIX); the Windows file layer has its own tests
    if(name STREQUAL "test_mode_list" AND WIN32)
      continue()
    endif()
    # RuntimeDir (openat-based runtime files of the Linux daemon) lives in a Linux platform file
    if(name STREQUAL "test_runtime_dir" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
      continue()
    endif()
    add_executable(${name} ${t})
    if(name STREQUAL "test_runtime_dir")
      target_sources(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/linux/runtime_files.cpp)
    endif()
    target_link_libraries(${name} PRIVATE dofscroll_core)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
      "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=halt_on_error=1:detect_leaks=1")
    list(APPEND TEST_TARGETS ${name})
  endforeach()

  add_test(NAME check_ports_trigger COMMAND ${CMAKE_COMMAND}
           -DFILE=${CMAKE_SOURCE_DIR}/.github/workflows/ports.yml
           -P ${CMAKE_SOURCE_DIR}/tests/check_ports_trigger.cmake)

  # config validation: examples/*.conf and examples/default/dofscroll.conf
  add_executable(check_config tests/check_config.cpp)
  target_link_libraries(check_config PRIVATE dofscroll_core)
  set_target_properties(check_config PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  file(GLOB EXAMPLE_CONFS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*.conf)
  list(APPEND EXAMPLE_CONFS ${CMAKE_SOURCE_DIR}/examples/default/dofscroll.conf)
  set(CHECK_CONFIG_COMMANDS)
  foreach(c ${EXAMPLE_CONFS})
    get_filename_component(cname ${c} NAME_WE)
    add_test(NAME check_config_${cname} COMMAND check_config ${c})
    list(APPEND CHECK_CONFIG_COMMANDS COMMAND check_config ${c})
  endforeach()
  add_custom_target(check-config ${CHECK_CONFIG_COMMANDS} DEPENDS check_config
                    COMMENT "Validating example configs" VERBATIM)
endif()

# ---- quality gates and helper targets -----
# check-fast: -Werror build + tests; check: + ASan/UBSan, cppcheck, hardening check.
# They configure/build in <build>/check-werror and <build>/check-asan.
option(DOFSCROLL_REQUIRE_ASAN "check: missing libasan/libubsan is an error (CI)" OFF)
set(CHECK_COMMON -DSRC=${CMAKE_SOURCE_DIR} -DBIN=${CMAKE_BINARY_DIR}
    -DWITH_TRAY=${DOFSCROLL_TRAY} -DCORE_ONLY=${DOFSCROLL_CORE_ONLY} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
if(DOFSCROLL_REQUIRE_ASAN)
  list(APPEND CHECK_COMMON -DREQUIRE_ASAN=1)
endif()
add_custom_target(check-fast COMMAND ${CMAKE_COMMAND} -DMODE=fast ${CHECK_COMMON}
                  -P ${CMAKE_SOURCE_DIR}/cmake/Check.cmake
                  COMMENT "Quick gate: -Werror build + tests" USES_TERMINAL VERBATIM)
add_custom_target(check COMMAND ${CMAKE_COMMAND} -DMODE=full ${CHECK_COMMON}
                  -P ${CMAKE_SOURCE_DIR}/cmake/Check.cmake
                  COMMENT "Full gate: werror, tests, ASan/UBSan, cppcheck, hardening"
                  USES_TERMINAL VERBATIM)
find_program(CPPCHECK_EXECUTABLE cppcheck)
if(CPPCHECK_EXECUTABLE)
  add_custom_target(lint COMMAND ${CPPCHECK_EXECUTABLE} --enable=warning,performance,portability
                    --error-exitcode=1 --inline-suppr --suppress=assertWithSideEffect:tests/*
                    -Isrc src tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    COMMENT "cppcheck" USES_TERMINAL VERBATIM)
else()
  add_custom_target(lint COMMAND ${CMAKE_COMMAND} -E echo "lint: cppcheck not installed - skipped")
endif()
add_custom_target(check-deps COMMAND ${CMAKE_COMMAND} -DWITH_TRAY=${DOFSCROLL_TRAY}
                  -DCXX=${CMAKE_CXX_COMPILER} -P ${CMAKE_SOURCE_DIR}/cmake/CheckDeps.cmake
                  USES_TERMINAL VERBATIM)
add_custom_target(install-user-links COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}
                  -DBIN=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/InstallUserLinks.cmake
                  USES_TERMINAL VERBATIM)
add_custom_target(install-udev COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}
                  -P ${CMAKE_SOURCE_DIR}/cmake/InstallUdev.cmake USES_TERMINAL VERBATIM)
add_custom_target(uninstall COMMAND ${CMAKE_COMMAND}
                  -DMANIFEST=${CMAKE_BINARY_DIR}/install_manifest.txt
                  -P ${CMAKE_SOURCE_DIR}/cmake/Uninstall.cmake USES_TERMINAL VERBATIM)

# ---- install ----------------------------------------------------------------
if(NOT DOFSCROLL_CORE_ONLY)
  install(TARGETS dofscroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(DOFSCROLL_BUILD_TRAY)
  install(TARGETS dofscroll-tray RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES icons/dofscroll-tray.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll-tray/icons)
endif()

file(GLOB EXAMPLE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/examples/*.conf)
install(FILES ${EXAMPLE_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll/examples)
install(DIRECTORY examples/default DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll/examples)
install(DIRECTORY devices/ DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll/devices FILES_MATCHING PATTERN "*.conf")
if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/examples/modes)
  install(DIRECTORY examples/modes DESTINATION ${CMAKE_INSTALL_DATADIR}/dofscroll/examples)
endif()

if(NOT DOFSCROLL_CORE_ONLY)
include(cmake/Systemd.cmake)  # units go to <prefix>/lib (never lib64), udev rule only as example
if(BUILD_TESTING)
  add_test(NAME install_layout COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
           -DTMP=${CMAKE_BINARY_DIR}/install_layout -P ${CMAKE_SOURCE_DIR}/cmake/InstallLayoutTest.cmake)
  add_test(NAME package_layout COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
           -DSRC=${CMAKE_SOURCE_DIR} -DCMAKE_CPACK_COMMAND=${CMAKE_CPACK_COMMAND}
           -DTMP=${CMAKE_BINARY_DIR}/package_layout -P ${CMAKE_SOURCE_DIR}/cmake/PackageLayoutTest.cmake)
endif()
endif()  # NOT DOFSCROLL_CORE_ONLY

install(FILES LICENSE readme.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# translations: po/*.po -> share/locale/<lang>/LC_MESSAGES/dofscroll.mo
# (only if po/ and msgfmt exist)
find_program(MSGFMT_EXECUTABLE msgfmt)
if(MSGFMT_EXECUTABLE AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/po)
  file(GLOB PO_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/po/*.po)
  set(MO_FILES)
  foreach(po ${PO_FILES})
    get_filename_component(lang ${po} NAME_WE)
    set(mo ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES/dofscroll.mo)
    add_custom_command(OUTPUT ${mo}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/${lang}/LC_MESSAGES
      COMMAND ${MSGFMT_EXECUTABLE} -o ${mo} ${po}
      DEPENDS ${po} VERBATIM)
    list(APPEND MO_FILES ${mo})
    install(FILES ${mo} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES)
  endforeach()
  if(MO_FILES)
    add_custom_target(translations ALL DEPENDS ${MO_FILES})
  endif()
endif()

include(packaging/cpack/DofscrollCPack.cmake)
