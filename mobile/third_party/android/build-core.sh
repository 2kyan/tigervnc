#!/bin/bash
# Builds TigerVNC's common/{core,rdr,network,rfb} as static libs for
# Android (arm64-v8a), against the dependencies built by build-deps.sh.
# Run build-deps.sh first.
#
# Mirrors third_party/ios/build-core.sh -- see it for why this stages a
# minimal CMake source-dir shim (./coretree) instead of using
# TigerVNC's own top-level CMakeLists.txt.
#
# Usage: ./build-core.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The TigerVNC source tree this mobile/ directory lives in
TIGERVNC_DIR="$(cd "$ROOT_DIR/../../.." && pwd)"
SYSROOT="$ROOT_DIR/sysroot"

case "$(uname -s)" in
  Darwin) : "${ANDROID_NDK_HOME:=/opt/homebrew/share/android-ndk}" ;;
  Linux)  : "${ANDROID_NDK_HOME:=$(ls -d "${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"/ndk/*/ 2>/dev/null | sort -V | tail -1)}" ;;
esac
ANDROID_NDK_HOME="${ANDROID_NDK_HOME%/}"
export ANDROID_NDK_HOME
if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
  echo "ANDROID_NDK_HOME not found -- set it explicitly" >&2
  exit 1
fi

for lib in libpixman-1 libjpeg libgmp libtasn1 libnettle libhogweed libgnutls; do
  if [ ! -f "$SYSROOT/lib/$lib.a" ]; then
    echo "Missing $SYSROOT/lib/$lib.a -- run build-deps.sh first" >&2
    exit 1
  fi
done


mkdir -p "$ROOT_DIR/coretree"
cp "$ROOT_DIR/CoreSpikeCMakeLists.txt" "$ROOT_DIR/coretree/CMakeLists.txt"
ln -sfn "$TIGERVNC_DIR/common" "$ROOT_DIR/coretree/common"
ln -sfn "$TIGERVNC_DIR/cmake" "$ROOT_DIR/coretree/cmake"

export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR=""

cmake -S "$ROOT_DIR/coretree" -B "$ROOT_DIR/build/core-android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/android-arm64.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build/core-android"

echo "==> done. Static libs under $ROOT_DIR/build/core-android/common/**/*.a"
