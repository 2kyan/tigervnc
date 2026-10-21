# Minimal CMake toolchain file for cross-compiling to iOS device (arm64).
# Used both for third_party dependency builds and, later, for core/.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

execute_process(
  COMMAND xcrun --sdk iphoneos --show-sdk-path
  OUTPUT_VARIABLE _ios_sdk_path
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_OSX_SYSROOT "${_ios_sdk_path}" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Point CMake at our vendored, iOS-built dependency install prefix, and at
# the SDK itself (for system libs like libz that aren't in our sysroot).
set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot" "${_ios_sdk_path}")
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot")
