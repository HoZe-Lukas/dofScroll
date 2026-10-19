# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#
# Qt6 Widgets user interface (dofscroll-ui). Included from CMakeLists.txt. Standalone: needs
# Qt and the core library (config, file layer; no daemon code), so the UI jobs can build just this target (see docs/ui-options.md).
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  option(DOFSCROLL_UI "Build the Qt6 user interface (experimental)" ON)
else()
  option(DOFSCROLL_UI "Build the Qt6 user interface (experimental)" OFF)
endif()

if(DOFSCROLL_UI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  find_package(Qt6 QUIET COMPONENTS Svg)   # SVG icon rendering; optional
  # Platform autostart implementation (one interface, src/platform/autostart_platform.hpp)
  if(WIN32)
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/win/autostart_win.cpp)
  elseif(APPLE)
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/mac/autostart_mac.cpp)
  else()
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/linux/autostart_linux.cpp)
  endif()
  qt_add_executable(dofscroll-ui
    src/ui/qt/main.cpp
    src/ui/qt/main_window.cpp
    src/ui/qt/main_window.hpp
    src/ui/qt/tray.cpp
    src/ui/qt/tray.hpp
    src/ui/qt/ui_i18n.hpp
    src/ui/qt/qt_action_runner.cpp
    src/ui/qt/qt_action_runner.hpp
    src/ui/qt/pages/page_registry.cpp
    src/ui/qt/pages/page_registry.hpp
    src/ui/qt/pages/channels_page.cpp
    src/ui/qt/pages/channels_page.hpp
    src/ui/qt/pages/general_page.cpp
    src/ui/qt/pages/general_page.hpp
    ${DOFSCROLL_UI_AUTOSTART_SRC}
    src/ui/qt/resources.qrc)
  set_target_properties(dofscroll-ui PROPERTIES AUTOMOC ON AUTORCC ON
                        WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
  target_include_directories(dofscroll-ui PRIVATE src/ui/qt src/ui/qt/pages)
  target_compile_features(dofscroll-ui PRIVATE cxx_std_17)
  target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_VERSION="${PROJECT_VERSION}")
  target_link_libraries(dofscroll-ui PRIVATE Qt6::Widgets dofscroll_core)
  if(TARGET Qt6::Svg)
    target_link_libraries(dofscroll-ui PRIVATE Qt6::Svg)
  endif()

  # Translations: po/de.po -> dofscroll_de.qm next to the executable (lconvert, Qt Linguist
  # tools). Optional: without lconvert the UI stays English.
  find_program(DOFSCROLL_LCONVERT NAMES lconvert lconvert-qt6
    HINTS ${Qt6_DIR}/../../../bin ${QT6_INSTALL_PREFIX}/bin ${Qt6_DIR}/../../../libexec
          /usr/lib/qt6/bin /usr/lib64/qt6/bin)
  if(DOFSCROLL_LCONVERT)
    add_custom_command(TARGET dofscroll-ui POST_BUILD
      COMMAND ${DOFSCROLL_LCONVERT} -i ${CMAKE_SOURCE_DIR}/po/de.po
              -o $<TARGET_FILE_DIR:dofscroll-ui>/dofscroll_de.qm
      COMMENT "po/de.po -> dofscroll_de.qm")
  else()
    message(STATUS "lconvert not found: dofscroll-ui is built without translations")
  endif()

  install(TARGETS dofscroll-ui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()
