# Builds the FLTK copy in 3rdparty/fltk at configure time and installs
# it into the build tree, so that it can be picked up by find_package()
# like a system installation would be.
#
# This is done as a separate CMake project (rather than via
# add_subdirectory()) to keep our compiler flags (e.g. -Werror) and
# install rules away from FLTK.

set(BUNDLED_FLTK_SOURCE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/fltk)
set(BUNDLED_FLTK_BINARY_DIR ${CMAKE_BINARY_DIR}/3rdparty/fltk-build)
set(BUNDLED_FLTK_PREFIX ${CMAKE_BINARY_DIR}/3rdparty/fltk)

set(BUNDLED_FLTK_OPTIONS "" CACHE STRING
  "Extra CMake arguments for the bundled FLTK (;-separated list)")

if(NOT EXISTS ${BUNDLED_FLTK_SOURCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "Bundled FLTK not found in ${BUNDLED_FLTK_SOURCE_DIR}. "
    "Run 'git submodule update --init 3rdparty/fltk'.")
endif()

if(CMAKE_BUILD_TYPE)
  set(BUNDLED_FLTK_BUILD_TYPE ${CMAKE_BUILD_TYPE})
else()
  set(BUNDLED_FLTK_BUILD_TYPE Release)
endif()

set(BUNDLED_FLTK_ARGS
  -G ${CMAKE_GENERATOR}
  -DCMAKE_BUILD_TYPE=${BUNDLED_FLTK_BUILD_TYPE}
  -DCMAKE_INSTALL_PREFIX=${BUNDLED_FLTK_PREFIX}
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DFLTK_BUILD_SHARED_LIBS=OFF
  -DFLTK_BUILD_FLUID=OFF
  -DFLTK_BUILD_FLTK_OPTIONS=OFF
  -DFLTK_BUILD_FORMS=OFF
  -DFLTK_BUILD_GL=OFF
  -DFLTK_BUILD_TEST=OFF
  -DFLTK_BUILD_EXAMPLES=OFF
  -DFLTK_BUILD_HTML_DOCS=OFF
  -DFLTK_BUILD_PDF_DOCS=OFF
  ${BUNDLED_FLTK_OPTIONS})

# Only rebuild when the FLTK revision or our arguments change
execute_process(COMMAND git -C ${BUNDLED_FLTK_SOURCE_DIR} rev-parse HEAD
  OUTPUT_VARIABLE BUNDLED_FLTK_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(SHA1 BUNDLED_FLTK_HASH "${BUNDLED_FLTK_REVISION};${BUNDLED_FLTK_ARGS}")
set(BUNDLED_FLTK_STAMP ${BUNDLED_FLTK_PREFIX}/.tigervnc-stamp)

if(EXISTS ${BUNDLED_FLTK_STAMP})
  file(READ ${BUNDLED_FLTK_STAMP} BUNDLED_FLTK_OLD_HASH)
endif()

if(NOT BUNDLED_FLTK_OLD_HASH STREQUAL BUNDLED_FLTK_HASH)
  message(STATUS "Building bundled FLTK in ${BUNDLED_FLTK_BINARY_DIR}")

  file(REMOVE_RECURSE ${BUNDLED_FLTK_BINARY_DIR} ${BUNDLED_FLTK_PREFIX})
  file(MAKE_DIRECTORY ${BUNDLED_FLTK_BINARY_DIR})

  execute_process(COMMAND ${CMAKE_COMMAND} ${BUNDLED_FLTK_ARGS}
                    -S ${BUNDLED_FLTK_SOURCE_DIR} -B ${BUNDLED_FLTK_BINARY_DIR}
    RESULT_VARIABLE BUNDLED_FLTK_RESULT)
  if(NOT BUNDLED_FLTK_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to configure bundled FLTK")
  endif()

  include(ProcessorCount)
  ProcessorCount(BUNDLED_FLTK_JOBS)
  if(BUNDLED_FLTK_JOBS EQUAL 0)
    set(BUNDLED_FLTK_JOBS 1)
  endif()

  execute_process(COMMAND ${CMAKE_COMMAND} --build ${BUNDLED_FLTK_BINARY_DIR}
                    --config ${BUNDLED_FLTK_BUILD_TYPE}
                    --parallel ${BUNDLED_FLTK_JOBS}
    RESULT_VARIABLE BUNDLED_FLTK_RESULT)
  if(NOT BUNDLED_FLTK_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build bundled FLTK")
  endif()

  execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUNDLED_FLTK_BINARY_DIR}
                    --config ${BUNDLED_FLTK_BUILD_TYPE}
    OUTPUT_QUIET
    RESULT_VARIABLE BUNDLED_FLTK_RESULT)
  if(NOT BUNDLED_FLTK_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install bundled FLTK")
  endif()

  file(WRITE ${BUNDLED_FLTK_STAMP} ${BUNDLED_FLTK_HASH})
endif()

find_package(FLTK 1.4 CONFIG REQUIRED
  PATHS ${BUNDLED_FLTK_PREFIX} NO_DEFAULT_PATH)
message(STATUS "Using bundled FLTK ${FLTK_VERSION}")
