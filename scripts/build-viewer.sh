#!/bin/bash
# Builds vncviewer against the bundled FLTK 1.4 (3rdparty/fltk).
#
# Usage: scripts/build-viewer.sh
#
# Settings (environment), in addition to those in common.sh:
#   BUILD_DIR     build directory   (default: build-viewer)
#
# Example: ENABLE_AUDIO=OFF BUILD_TYPE=Debug scripts/build-viewer.sh

set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-viewer}"

check_common_deps

if [ ! -f "$ROOT_DIR/3rdparty/fltk/CMakeLists.txt" ]; then
  log "fetching the FLTK submodule"
  git -C "$ROOT_DIR" submodule update --init 3rdparty/fltk
fi

cmake_configure "$BUILD_DIR" -DBUILD_VIEWER=ON -DUSE_BUNDLED_FLTK=ON
cmake_build "$BUILD_DIR" vncviewer

log "built $BUILD_DIR/vncviewer/vncviewer"
