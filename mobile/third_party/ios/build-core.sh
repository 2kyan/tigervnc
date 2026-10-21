#!/bin/bash
# Builds TigerVNC's common/{core,rdr,network,rfb} as static libs for
# iOS (arm64), against the dependencies built by build-deps.sh. Run
# build-deps.sh first.
#
# This does NOT use TigerVNC's own top-level CMakeLists.txt -- that
# file requires X11/FLTK and other desktop/server-only dependencies this
# spike doesn't need. Instead it stages a minimal source-dir shim
# (./coretree) with our own CMakeLists.txt plus symlinks to
# TigerVNC's common/ and cmake/ directories, since
# common/CMakeLists.txt insists on being reached via
# ${CMAKE_SOURCE_DIR}/common.
#
# Usage: ./build-core.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The TigerVNC source tree this mobile/ directory lives in
TIGERVNC_DIR="$(cd "$ROOT_DIR/../../.." && pwd)"
SYSROOT="$ROOT_DIR/sysroot"

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

cmake -S "$ROOT_DIR/coretree" -B "$ROOT_DIR/build/core-ios" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/ios-arm64.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build/core-ios"

echo "==> done. Static libs under $ROOT_DIR/build/core-ios/common/**/*.a"
