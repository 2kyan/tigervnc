# CMake toolchain file for cross-compiling to Android (arm64-v8a), built
# on top of the NDK's own toolchain file. Used both for third_party
# dependency builds and, later, for core/.
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
  message(FATAL_ERROR "ANDROID_NDK_HOME must be set (e.g. /opt/homebrew/share/android-ndk)")
endif()

set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-24)
# Static libstdc++ so the spike has no runtime .so to bundle yet; revisit
# in phase 3 if multiple shared libs end up needing one shared copy.
set(ANDROID_STL c++_static)

include("$ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake")

# Point CMake at our vendored, Android-built dependency install prefix.
list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/sysroot")
