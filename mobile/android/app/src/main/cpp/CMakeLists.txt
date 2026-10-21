# Builds libvncjni.so: the JNI glue (VncBridge.cpp) linking tigervnc_core
# and its dependency chain as prebuilt static libs. Mirrors ios/project.yml's
# OTHER_LDFLAGS/LIBRARY_SEARCH_PATHS approach -- core/ and
# third_party/android's dependencies are built once, separately (run
# third_party/android/build-deps.sh then build core/ for Android -- see
# DESIGN.md's Android vertical slice findings), and this module just links
# the resulting .a files rather than rebuilding TigerVNC's
# CMakeLists.txt a second time through Gradle (which can't share
# CMAKE_SOURCE_DIR-relative assumptions those files make -- see DESIGN.md).
cmake_minimum_required(VERSION 3.22.1)
project(vncjni CXX)

set(CMAKE_CXX_STANDARD 17)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(CORE_BUILD "${REPO_ROOT}/core/build/android")
set(THIRD_PARTY_SYSROOT "${REPO_ROOT}/third_party/android/sysroot")

if(NOT EXISTS "${CORE_BUILD}/libtigervnc_core.a")
  message(FATAL_ERROR
    "Missing ${CORE_BUILD}/libtigervnc_core.a -- build core/ for Android "
    "first (see DESIGN.md's Android vertical slice findings): "
    "third_party/android/build-deps.sh, then cmake -S core -B "
    "core/build/android -DCMAKE_TOOLCHAIN_FILE=third_party/android/"
    "android-arm64.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && cmake "
    "--build core/build/android")
endif()

add_library(vncjni SHARED VncBridge.cpp)

target_include_directories(vncjni PRIVATE "${REPO_ROOT}/core/include")

target_link_directories(vncjni PRIVATE
  "${CORE_BUILD}"
  "${CORE_BUILD}/common/rfb"
  "${CORE_BUILD}/common/rdr"
  "${CORE_BUILD}/common/network"
  "${CORE_BUILD}/common/core"
  "${THIRD_PARTY_SYSROOT}/lib"
)

find_library(LOG_LIB log)

target_link_libraries(vncjni PRIVATE
  tigervnc_core rfbclient rfb rdr network core
  gnutls hogweed nettle gmp tasn1 pixman-1 jpeg
  z ${LOG_LIB}
)
