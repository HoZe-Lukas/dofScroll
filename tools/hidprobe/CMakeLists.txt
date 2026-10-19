# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# dofscroll-hidprobe: read-only HID diagnostic. Added from the top level CMakeLists.txt
# with: if(DOFSCROLL_HIDPROBE) add_subdirectory(tools/hidprobe) endif()
# Only the Linux (hidraw) backend exists so far; a Windows or macOS backend implements
# hidprobe::HidBackend (hidprobe_backend.hpp) and is added to the source list below.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HIDPROBE_DEFAULT ON)
else()
  set(HIDPROBE_DEFAULT OFF)
endif()
option(DOFSCROLL_HIDPROBE "Build dofscroll-hidprobe (read-only HID diagnostic)" ${HIDPROBE_DEFAULT})
if(NOT DOFSCROLL_HIDPROBE)
  return()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HIDPROBE_PLATFORM_SRC reader_linux.cpp)
else()
  message(WARNING "dofscroll-hidprobe has no backend for ${CMAKE_SYSTEM_NAME}: not built")
  return()
endif()

add_executable(dofscroll-hidprobe main.cpp ${HIDPROBE_PLATFORM_SRC})
target_include_directories(dofscroll-hidprobe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dofscroll-hidprobe PRIVATE dofscroll_core)
install(TARGETS dofscroll-hidprobe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
