# Builds tigervnc_core: the C ABI wrapper (see include/tigervnc_core.h)
# around TigerVNC's common/{core,rdr,network,rfb} protocol engine.
# This is a real (non-throwaway) top-level CMake project, structured the
# same way as the phase-1 spikes in third_party/{ios,android}: `common`
# and `cmake` below are symlinks to the TigerVNC source tree's
# directories of the same name, because common/CMakeLists.txt requires
# ${CMAKE_SOURCE_DIR}/common to resolve to the real common/ dir -- see
# DESIGN.md's iOS/Android spike findings for why. Deliberately does not use
# TigerVNC's own top-level CMakeLists.txt (pulls in X11/FLTK/server
# deps this doesn't need).
#
# Build for a target platform by pointing CMAKE_TOOLCHAIN_FILE at one of
# third_party/ios/ios-arm64.toolchain.cmake or
# third_party/android/android-arm64.toolchain.cmake (run that platform's
# build-deps.sh first so the toolchain's sysroot has pixman/jpeg/gnutls/
# nettle). For a host (macOS) build for fast local iteration, install
# the same deps via Homebrew (pixman jpeg-turbo gnutls nettle) and
# configure with no toolchain file.
cmake_minimum_required(VERSION 3.10.0)
project(tigervnc_core CXX C)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)
include(CMakeMacroLibtoolFile)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# core/ requires these even with NLS off (see common/core/CMakeLists.txt);
# left empty since ENABLE_NLS is not defined for this build.
set(Intl_INCLUDE_DIR "")
set(Intl_LIBRARIES "")

# common/core/i18n.cxx's getlocaledir() falls back to
# CMAKE_INSTALL_FULL_LOCALEDIR on generic Unix (macOS/iOS/Android each
# have their own branch that doesn't need it -- see DESIGN.md's spike
# findings). Normally supplied by tigervnc's own top-level CMakeLists.txt
# via GNUInstallDirs, which this minimal wrapper deliberately doesn't use
# (see this file's top comment). Only reached by host Linux builds of
# core/tests -- never actually read since ENABLE_NLS is off, but must be
# defined to compile.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
  add_compile_definitions(CMAKE_INSTALL_FULL_LOCALEDIR="")
endif()

find_package(ZLIB REQUIRED)
find_package(Pixman REQUIRED)
find_package(JPEG REQUIRED)
find_package(GnuTLS REQUIRED)
find_package(Nettle REQUIRED)
find_package(Threads REQUIRED)

# Decodes the TigerVNC-native audio extension's Opus-encoded payload
# back to PCM in VncClientImpl::handleAudioData() -- see
# rfb/audioTypes.h. No CMake config/module ships for Opus, only a
# pkg-config .pc file, same as the top-level CMakeLists.txt works around
# for it. For Android and iOS, third_party/{android,ios}/build-deps*.sh
# cross-compile libopus into their sysroots.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

if(GNUTLS_FOUND)
  set(HAVE_GNUTLS 1)
  add_compile_definitions(HAVE_GNUTLS=1)
endif()
if(NETTLE_FOUND)
  set(HAVE_NETTLE 1)
  add_compile_definitions(HAVE_NETTLE=1)
endif()

add_subdirectory(common)

add_library(tigervnc_core STATIC
  src/tigervnc_core.cxx
  src/VncClientImpl.cxx
)
target_include_directories(tigervnc_core
  PUBLIC include
  PRIVATE src
)
target_include_directories(tigervnc_core SYSTEM PRIVATE ${NETTLE_INCLUDE_DIRS} ${OPUS_INCLUDE_DIRS})
target_link_libraries(tigervnc_core
  PRIVATE rfbclient rfb rdr network core
  PRIVATE Threads::Threads
  PRIVATE ${OPUS_LIBRARIES}
)

# Host-only test tools (a real RFB server + a smoke-test client), for
# fast local iteration -- not meant to cross-compile for iOS/Android.
# Needs libvncserver: `brew install libvncserver`.
option(TIGERVNC_CORE_BUILD_TESTS "Build core/tests (host-only, needs libvncserver)" OFF)
if(TIGERVNC_CORE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
