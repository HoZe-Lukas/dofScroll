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
#          DOFSCROLL_SANITIZE (OFF, ASan/UBSan), DOFSCROLL_WERROR (OFF).
#
# Packaging notes: Linux packages (RPM, DEB, TGZ) are produced by CPack, see
# packaging/cpack/DofscrollCPack.cmake. Windows and macOS packages are not
# produced by this build: the daemon depends on X11, libspnav and uinput
# (see docs/windows-mac.md).

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
set(CMAKE_CXX_FLAGS_RELEASE "-O2" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "" FORCE)

option(DOFSCROLL_TRAY     "Build the GTK tray (needs gtk3 + appindicator)" ON)
option(DOFSCROLL_HARDEN   "Enable hardening compiler/linker flags" ON)
option(DOFSCROLL_SANITIZE "Build with ASan/UBSan" OFF)
option(DOFSCROLL_WERROR   "Treat warnings as errors" OFF)

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# ---- flags ------------------------------------------
add_library(dofscroll_flags INTERFACE)
target_compile_options(dofscroll_flags INTERFACE -Wall -Wextra)
if(DOFSCROLL_WERROR)
  target_compile_options(dofscroll_flags INTERFACE -Werror)
endif()
if(DOFSCROLL_HARDEN)
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

add_library(dofscroll_core STATIC ${CORE_SRC})
target_include_directories(dofscroll_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dofscroll_core PUBLIC dofscroll_flags Threads::Threads)

# ---- daemon -----------------------------------------------------------------
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11 xtst)
find_library(SPNAV_LIB spnav REQUIRED)
find_path(SPNAV_INCLUDE spnav.h REQUIRED)

file(GLOB LINUX_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/linux/*.cpp)
add_executable(dofscroll src/dofscroll.cpp ${BACKEND_SRC} ${LINUX_SRC})
target_include_directories(dofscroll PRIVATE ${SPNAV_INCLUDE})
target_link_libraries(dofscroll PRIVATE dofscroll_core PkgConfig::X11 ${SPNAV_LIB})

# ---- tray (optional) --------------------------------------------------------
set(DOFSCROLL_BUILD_TRAY OFF)
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

add_subdirectory(tools/hidprobe)  # optional diagnostic, option DOFSCROLL_HIDPROBE (see there)

# ---- tests ------------------------------------------------------------------
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
set(TEST_TARGETS)
if(BUILD_TESTING)
  foreach(t ${TEST_SRC})
    get_filename_component(name ${t} NAME_WE)
    add_executable(${name} ${t})
    target_link_libraries(${name} PRIVATE dofscroll_core)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
      "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=halt_on_error=1:detect_leaks=1")
    list(APPEND TEST_TARGETS ${name})
  endforeach()

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
    -DWITH_TRAY=${DOFSCROLL_TRAY} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
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
install(TARGETS dofscroll RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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

include(cmake/Systemd.cmake)  # units + udev rule go to <prefix>/lib (never lib64)
if(BUILD_TESTING)
  add_test(NAME install_layout COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
           -DTMP=${CMAKE_BINARY_DIR}/install_layout -P ${CMAKE_SOURCE_DIR}/cmake/InstallLayoutTest.cmake)
endif()

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
