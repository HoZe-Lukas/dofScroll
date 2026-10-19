# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Script mode, driven by the check / check-fast targets:
#   cmake -DMODE=fast|full -DSRC=<repo> -DBIN=<build dir> -DWITH_TRAY=ON|OFF
#         [-DREQUIRE_ASAN=1] -P cmake/Check.cmake
# fast: -Werror build + all tests (incl. example config validation)
# full: fast + ASan/UBSan tests + cppcheck + hardening check of the -Werror build.
# Everything is built in subdirectories of BIN; the main build is never touched.
if(NOT DEFINED CORE_ONLY)
  set(CORE_ONLY OFF)
endif()
set(fail 0)
set(CT ${CMAKE_COMMAND})
get_filename_component(CTEST_DIR "${CMAKE_COMMAND}" DIRECTORY)
find_program(CTEST_EXE ctest HINTS "${CTEST_DIR}")
if(NOT CTEST_EXE)
  set(CTEST_EXE ctest)
endif()

function(step name)
  message("\n=== check (${MODE}): ${name} ===")
  execute_process(${ARGN} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message("!!! FAILED: ${name}")
    set(fail 1 PARENT_SCOPE)
  endif()
endfunction()

set(WD ${BIN}/check-werror)
step(werror-configure COMMAND ${CT} -S ${SRC} -B ${WD} -DDOFSCROLL_WERROR=ON
     -DDOFSCROLL_TRAY=${WITH_TRAY} -DDOFSCROLL_CORE_ONLY=${CORE_ONLY})
step(werror-build COMMAND ${CT} --build ${WD} -j4)
step(test COMMAND ${CTEST_EXE} --test-dir ${WD} --output-on-failure)

if(MODE STREQUAL "full")
  # ASan/UBSan: only if the toolchain has the runtimes
  find_program(CXX_EXE NAMES c++ g++ clang++)
  if(CMAKE_CXX_COMPILER)
    set(CXX_EXE ${CMAKE_CXX_COMPILER})
  endif()
  file(WRITE ${BIN}/asan_probe.cpp "int main(){}\n")
  execute_process(COMMAND ${CXX_EXE} -fsanitize=address,undefined -o ${BIN}/asan_probe
                  ${BIN}/asan_probe.cpp RESULT_VARIABLE asan_rc OUTPUT_QUIET ERROR_QUIET)
  if(asan_rc EQUAL 0)
    set(AD ${BIN}/check-asan)
    step(asan-configure COMMAND ${CT} -S ${SRC} -B ${AD} -DDOFSCROLL_SANITIZE=ON
         -DDOFSCROLL_TRAY=OFF -DDOFSCROLL_CORE_ONLY=${CORE_ONLY})
    step(asan-build COMMAND ${CT} --build ${AD} -j4)
    step(asan-test COMMAND ${CTEST_EXE} --test-dir ${AD} --output-on-failure)
  elseif(REQUIRE_ASAN)
    message("!!! FAILED: libasan/libubsan not available, but REQUIRE_ASAN=1")
    set(fail 1)
  else()
    message("WARNING: libasan/libubsan not available - ASan step skipped (CI: -DREQUIRE_ASAN=1).")
  endif()

  find_program(CPPCHECK_EXE cppcheck)
  if(CPPCHECK_EXE)
    step(cppcheck WORKING_DIRECTORY ${SRC}
         COMMAND ${CPPCHECK_EXE} --enable=warning,performance,portability --error-exitcode=1
                 --inline-suppr --suppress=assertWithSideEffect:tests/* -Isrc src tests)
  else()
    message("\ncppcheck not installed - skipped (CI installs it).")
  endif()

  set(bins ${WD}/dofscroll)
  if(EXISTS ${WD}/dofscroll-tray)
    list(APPEND bins ${WD}/dofscroll-tray)
  endif()
  step(check-hardening COMMAND sh ${SRC}/scripts/check-hardening.sh ${bins})
endif()

if(fail)
  message(FATAL_ERROR "check (${MODE}): FAILED")
endif()
message("\ncheck (${MODE}): all ok")
