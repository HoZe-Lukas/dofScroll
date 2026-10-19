# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Windows daemon (included from the top-level CMakeLists.txt when WIN32). Builds dofscroll.exe, a
# GUI-subsystem program (no console window) from src/platform/win/*.cpp; the shared core and the
# tests are defined by the top-level file. See docs/port-windows.md.
option(DOFSCROLL_BUILD_DAEMON "Build the Windows daemon (dofscroll.exe)" ON)

if(DOFSCROLL_BUILD_DAEMON)
  file(GLOB WIN_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp)
  # Application manifest as a resource (asInvoker, longPathAware, UTF-8 active code page).
  enable_language(RC)
  set(WIN_RC ${CMAKE_SOURCE_DIR}/packaging/windows/dofscroll.rc)
  set_source_files_properties(${WIN_RC} PROPERTIES OBJECT_DEPENDS
    ${CMAKE_SOURCE_DIR}/packaging/windows/dofscroll.manifest)
  add_executable(dofscroll WIN32 ${WIN_SRC} ${WIN_RC})
  target_include_directories(dofscroll PRIVATE ${CMAKE_SOURCE_DIR}/packaging/windows)
  target_link_libraries(dofscroll PRIVATE dofscroll_core hid setupapi user32 advapi32 shell32 bcrypt)
  if(MSVC)
    # The manifest comes from the .rc: no linker-generated one (duplicate resource otherwise).
    # Control-flow guard is NOT an MSVC default; DYNAMICBASE/NXCOMPAT/HIGHENTROPYVA are made explicit.
    # DEPENDENTLOADFLAG 0x800 = LOAD_LIBRARY_SEARCH_SYSTEM32 for every implicit and delay load.
    target_compile_options(dofscroll PRIVATE /guard:cf /GS)
    target_link_options(dofscroll PRIVATE /MANIFEST:NO /GUARD:CF /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT
      /DEPENDENTLOADFLAG:0x800)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(dofscroll PRIVATE /HIGHENTROPYVA)
    endif()
  endif()
  if(MINGW)
    # ASLR, DEP, high-entropy ASLR (64-bit) and a stack protector (needs libssp on some toolchains).
    target_link_options(dofscroll PRIVATE -Wl,--dynamicbase -Wl,--nxcompat)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(dofscroll PRIVATE -Wl,--high-entropy-va)
    endif()
    include(CheckCXXSourceCompiles)
    set(_ssp_src "int main(int argc, char **argv) { char b[64]; for (int i = 0; i < argc; ++i) b[i] = argv[0][i]; return b[argc]; }")
    set(CMAKE_REQUIRED_FLAGS "-fstack-protector-strong")
    check_cxx_source_compiles("${_ssp_src}" DOFSCROLL_SSP_BUILTIN)
    set(CMAKE_REQUIRED_LIBRARIES ssp)
    check_cxx_source_compiles("${_ssp_src}" DOFSCROLL_SSP_LIB)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_FLAGS)
    if(DOFSCROLL_SSP_BUILTIN)
      target_compile_options(dofscroll PRIVATE -fstack-protector-strong)
      target_link_options(dofscroll PRIVATE -fstack-protector-strong)
    elseif(DOFSCROLL_SSP_LIB)
      target_compile_options(dofscroll PRIVATE -fstack-protector-strong)
      target_link_libraries(dofscroll PRIVATE ssp)
    else()
      message(WARNING "dofscroll: stack protector not available with this MinGW toolchain")
    endif()
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

# W18: round trip of the autostart code on the HKCU Run key (value names dofscroll-test*, removed afterwards)
if(BUILD_TESTING)
  add_executable(autostart_win_roundtrip tests/autostart_win_roundtrip.cpp src/platform/win/autostart_win.cpp)
  target_link_libraries(autostart_win_roundtrip PRIVATE dofscroll_core advapi32)
  set_target_properties(autostart_win_roundtrip PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME autostart_win_roundtrip COMMAND autostart_win_roundtrip)
endif()
