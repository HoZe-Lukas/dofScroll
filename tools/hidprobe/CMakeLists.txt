# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# dofscroll-hidprobe: read-only HID diagnostic. Added from the top level CMakeLists.txt
# with: if(DOFSCROLL_HIDPROBE) add_subdirectory(tools/hidprobe) endif()
# One backend per OS implements hidprobe::HidBackend (hidprobe_backend.hpp): reader_linux.cpp
# (hidraw), reader_win.cpp (SetupAPI + hid.dll), reader_mac.cpp (IOHIDManager). The Windows and
# macOS packages ship it together with scripts/test-driver-coexistence.* (docs/devices.md).

option(DOFSCROLL_HIDPROBE "Build dofscroll-hidprobe (read-only HID diagnostic)" ON)
if(NOT DOFSCROLL_HIDPROBE)
  return()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HIDPROBE_PLATFORM_SRC reader_linux.cpp)
elseif(WIN32)
  set(HIDPROBE_PLATFORM_SRC reader_win.cpp)
elseif(APPLE)
  set(HIDPROBE_PLATFORM_SRC reader_mac.cpp)
else()
  message(WARNING "dofscroll-hidprobe has no backend for ${CMAKE_SYSTEM_NAME}: not built")
  return()
endif()

add_executable(dofscroll-hidprobe main.cpp ${HIDPROBE_PLATFORM_SRC})
# next to dofscroll (build/ or build/Release/): the installer scripts, the packaging scripts and the CI
# checks expect it there, not under build/tools/hidprobe/
set_target_properties(dofscroll-hidprobe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(dofscroll-hidprobe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dofscroll-hidprobe PRIVATE dofscroll_core)
if(WIN32)
  target_link_libraries(dofscroll-hidprobe PRIVATE hid setupapi)
  if(MSVC)
    # same hardening as the daemon (cmake/windows.cmake); DEPENDENTLOADFLAG 0x800 = System32 only
    target_compile_options(dofscroll-hidprobe PRIVATE /guard:cf /GS)
    target_link_options(dofscroll-hidprobe PRIVATE /GUARD:CF /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT
      /DEPENDENTLOADFLAG:0x800)
  endif()
  if(MINGW)
    target_link_options(dofscroll-hidprobe PRIVATE -Wl,--dynamicbase -Wl,--nxcompat -static)
  endif()
elseif(APPLE)
  target_link_libraries(dofscroll-hidprobe PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()
install(TARGETS dofscroll-hidprobe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
