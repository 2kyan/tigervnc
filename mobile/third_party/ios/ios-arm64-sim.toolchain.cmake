# CMake toolchain file for cross-compiling to the iOS Simulator on
# Apple Silicon (arm64). Sibling of ios-arm64.toolchain.cmake (device
# build) -- same reasoning, different SDK. Needed to actually run and
# see the app in Simulator without a paid developer account / physical
# device provisioning.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

execute_process(
  COMMAND xcrun --sdk iphonesimulator --show-sdk-path
  OUTPUT_VARIABLE _ios_sim_sdk_path
  OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_OSX_SYSROOT "${_ios_sim_sdk_path}" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot-sim" "${_ios_sim_sdk_path}")
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot-sim")
