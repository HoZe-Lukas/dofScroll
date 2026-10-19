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
  # Client side of the platform layer (platformPaths, requestReload; shared with the daemon, no
  # input backends) and platform autostart implementation (one interface, src/platform/autostart_platform.hpp)
  if(WIN32)
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/win/autostart_win.cpp)
    set(DOFSCROLL_UI_CLIENT_SRC src/platform/win/client_win.cpp)
  elseif(APPLE)
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/mac/autostart_mac.cpp)
    set(DOFSCROLL_UI_CLIENT_SRC src/platform/mac/client_mac.cpp)
  else()
    set(DOFSCROLL_UI_AUTOSTART_SRC src/platform/linux/autostart_linux.cpp)
    set(DOFSCROLL_UI_CLIENT_SRC src/platform/linux/client_linux.cpp)
  endif()
  qt_add_executable(dofscroll-ui
    src/ui/qt/main.cpp
    src/ui/qt/main_window.cpp
    src/ui/qt/main_window.hpp
    src/ui/qt/vertical_tabs.hpp
    src/ui/qt/theme.cpp
    src/ui/qt/theme.hpp
    src/ui/qt/tray.cpp
    src/ui/qt/tray.hpp
    src/ui/qt/ui_i18n.hpp
    src/ui/qt/ui_context.hpp
    src/ui/qt/qt_action_runner.cpp
    src/ui/qt/qt_action_runner.hpp
    src/ui/qt/pages/page_registry.cpp
    src/ui/qt/pages/page_registry.hpp
    src/ui/qt/pages/axes_page.cpp
    src/ui/qt/pages/axes_page.hpp
    src/ui/qt/pages/channels_page.cpp
    src/ui/qt/pages/channels_page.hpp
    src/ui/qt/pages/general_page.cpp
    src/ui/qt/pages/general_page.hpp
    src/ui/qt/pages/modes_page.cpp
    src/ui/qt/pages/overview_page.cpp
    src/ui/qt/pages/overview_page.hpp
    src/ui/qt/pages/modes_page.hpp
    src/ui/qt/pages/profiles_page.cpp
    src/ui/qt/pages/profiles_page.hpp
    src/ui/qt/pages/profiles_env.hpp
    src/ui/qt/pages/add_app_dialog.cpp
    src/ui/qt/pages/add_app_dialog.hpp
    src/ui/qt/pages/add_device_dialog.cpp
    src/ui/qt/pages/add_device_dialog.hpp
    src/ui/qt/platform_hooks.cpp
    src/ui/qt/platform_hooks.hpp
    ${DOFSCROLL_UI_AUTOSTART_SRC}
    ${DOFSCROLL_UI_CLIENT_SRC}
    src/ui/qt/resources.qrc)
  # The toolkit-free models use the core library (config, modes, fs); the autostart code of the
  # current OS comes from src/platform/<os>/ (self-contained, no input/output backends).
  set_target_properties(dofscroll-ui PROPERTIES AUTOMOC ON AUTORCC ON
                        WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
  target_include_directories(dofscroll-ui PRIVATE src/ui/qt src/ui/qt/pages)
  target_compile_features(dofscroll-ui PRIVATE cxx_std_17)
  target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_VERSION="${PROJECT_VERSION}")
  target_link_libraries(dofscroll-ui PRIVATE Qt6::Widgets dofscroll_core)
  if(TARGET Qt6::Svg)
    target_link_libraries(dofscroll-ui PRIVATE Qt6::Svg)
  endif()

  # Platform seams the pages use (running-programs list, foreground program for "capture next
  # focused program"). Each OS adds its provider when it is available in this tree; otherwise a
  # stub keeps the UI linkable and the page shows a "not available" note.
  set(DOFSCROLL_UI_RUNNING_APPS_STUB ON)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(DOFSCROLL_UI_X11 QUIET IMPORTED_TARGET x11)
    endif()
    if(DOFSCROLL_UI_X11_FOUND)
      target_sources(dofscroll-ui PRIVATE src/platform/linux/running_apps_x11.cpp src/activewin.cpp)
      target_link_libraries(dofscroll-ui PRIVATE PkgConfig::DOFSCROLL_UI_X11)
      target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_UI_X11=1)
      set(DOFSCROLL_UI_RUNNING_APPS_STUB OFF)
    endif()
  elseif(WIN32)
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/platform/win/running_apps_win.cpp)
      target_sources(dofscroll-ui PRIVATE src/platform/win/running_apps_win.cpp
                     src/platform/win/win_activewin.cpp)
      target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_UI_WIN_AW=1)
      target_link_libraries(dofscroll-ui PRIVATE user32 version dwmapi)
      set(DOFSCROLL_UI_RUNNING_APPS_STUB OFF)
    endif()
  elseif(APPLE)
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/platform/mac/running_apps_mac.mm AND CMAKE_OBJCXX_COMPILER)
      target_sources(dofscroll-ui PRIVATE src/platform/mac/running_apps_mac.mm
                     src/platform/mac/mac_activewin.mm)
      target_link_libraries(dofscroll-ui PRIVATE "-framework AppKit")
      target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_UI_MAC_AW=1)
      set(DOFSCROLL_UI_RUNNING_APPS_STUB OFF)
    endif()
  endif()
  if(DOFSCROLL_UI_RUNNING_APPS_STUB)
    target_compile_definitions(dofscroll-ui PRIVATE DOFSCROLL_UI_RUNNING_APPS_STUB=1)
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
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Desktop integration (freedesktop): menu entry and icon. The user unit
    # dofscroll-ui.service is installed by cmake/Systemd.cmake. The German catalog goes next to
    # the binary because dofscroll-ui looks for dofscroll_<lang>.qm in its own directory.
    install(FILES packaging/linux/dofscroll.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/dofscroll-tray.svg RENAME dofscroll.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    if(DOFSCROLL_LCONVERT)
      install(FILES $<TARGET_FILE_DIR:dofscroll-ui>/dofscroll_de.qm
              DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
  endif()
endif()
