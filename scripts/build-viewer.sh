#!/bin/bash
# Builds vncviewer against the system FLTK 1.3.
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
need_cmd fltk-config libfltk1.3-dev

cmake_configure "$BUILD_DIR" -DBUILD_VIEWER=ON
cmake_build "$BUILD_DIR" vncviewer

log "built $BUILD_DIR/vncviewer/vncviewer"
