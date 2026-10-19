# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Script mode: cmake -DSRC=<repo> [-DUDEV_DIR=/etc/udev/rules.d] -P cmake/InstallUdev.cmake
# Installs packaging/*.rules (needs root for the default directory).
if(NOT UDEV_DIR)
  set(UDEV_DIR /etc/udev/rules.d)
endif()
file(GLOB rules "${SRC}/packaging/*.rules")
if(NOT rules)
  message(FATAL_ERROR "No packaging/*.rules present.")
endif()
file(MAKE_DIRECTORY "$ENV{DESTDIR}${UDEV_DIR}")
foreach(r ${rules})
  file(COPY "${r}" DESTINATION "$ENV{DESTDIR}${UDEV_DIR}" FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endforeach()
message("Afterwards: sudo udevadm control --reload && sudo udevadm trigger")
