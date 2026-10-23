#!/bin/bash
# Cross-builds the Windows vncviewer (64-bit) with MinGW-w64, inside a
# Fedora container: Fedora packages MinGW builds of every library the
# viewer needs (FLTK, GnuTLS, libjpeg-turbo, pixman, zlib, Opus), which
# Debian/Ubuntu don't.
#
# Usage: scripts/build-windows.sh
#
# Settings (environment), in addition to those in common.sh:
#   BUILD_DIR     build directory                 (default: build-win64)
#   CONTAINER     podman or docker   (default: podman if found, else docker)
#   IMAGE         name of the build image          (default: tigervnc-mingw64)
#   FEDORA        Fedora release the image is based on      (default: 44)
#
# The result is vncviewer.exe plus the MinGW DLLs it needs, copied to
# BIN_DIR/windows; copy that whole directory to the Windows machine.
#
# The image is built on first use and reused afterwards; remove it
# (podman rmi tigervnc-mingw64) to pick up newer packages.

set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-win64}"
IMAGE="${IMAGE:-tigervnc-mingw64}"
FEDORA="${FEDORA:-44}"

MINGW_PACKAGES="cmake make gettext mingw64-gcc-c++ mingw64-fltk
  mingw64-gnutls mingw64-libjpeg-turbo mingw64-pixman mingw64-zlib
  mingw64-gettext"
[ "$ENABLE_AUDIO" = "OFF" ] || MINGW_PACKAGES="$MINGW_PACKAGES mingw64-opus"

SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw

# copy_dlls <exe or dll> <dest dir>
#
# Copies the MinGW DLLs a binary imports, recursively. DLLs that aren't
# in the MinGW sysroot are Windows' own and are skipped.
copy_dlls()
{
  local dll
  for dll in $(x86_64-w64-mingw32-objdump -p "$1" |
               sed -n 's/^\s*DLL Name: //p'); do
    [ -e "$SYSROOT/bin/$dll" ] || continue
    [ -e "$2/$dll" ] && continue
    install -m 0755 "$SYSROOT/bin/$dll" "$2/$dll"
    copy_dlls "$SYSROOT/bin/$dll" "$2"
  done
}

# Runs inside the container, with the source tree mounted at the same
# path as on the host
build_in_container()
{
  local out="$BIN_DIR/windows"

  # The toolchain file leaves pkg-config alone, and the native one
  # wouldn't find Opus
  cmake_configure "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    -DPKG_CONFIG_EXECUTABLE=/usr/bin/x86_64-w64-mingw32-pkg-config \
    -DBUILD_VIEWER=ON
  cmake_build "$BUILD_DIR" vncviewer

  log "built $BUILD_DIR/vncviewer/vncviewer.exe"
  [ "$INSTALL_BIN" = "1" ] || return 0
  rm -rf "$out"
  mkdir -p "$out"
  install -m 0755 "$BUILD_DIR/vncviewer/vncviewer.exe" "$out/"
  copy_dlls "$out/vncviewer.exe" "$out"
  log "copied to $out:"
  ls "$out" | sed 's/^/    /'
}

if [ "${IN_CONTAINER:-0}" = "1" ]; then
  build_in_container
  exit 0
fi

if [ -z "${CONTAINER:-}" ]; then
  if command -v podman >/dev/null 2>&1; then
    CONTAINER=podman
  elif command -v docker >/dev/null 2>&1; then
    CONTAINER=docker
  else
    die "need podman or docker to cross-build for Windows"
  fi
fi
need_cmd "$CONTAINER" "$CONTAINER"

if ! "$CONTAINER" image inspect "$IMAGE" >/dev/null 2>&1; then
  log "building the $IMAGE image (Fedora $FEDORA and MinGW packages)"
  # shellcheck disable=SC2086
  echo "FROM registry.fedoraproject.org/fedora:$FEDORA
RUN dnf -y install $(echo $MINGW_PACKAGES) && dnf clean all" |
    "$CONTAINER" build -t "$IMAGE" -f - "$ROOT_DIR/scripts"
fi

# Run as the calling user so the build tree isn't owned by root
if [ "$CONTAINER" = "podman" ]; then
  USER_ARGS=(--userns=keep-id)
else
  USER_ARGS=(--user "$(id -u):$(id -g)")
fi

"$CONTAINER" run --rm "${USER_ARGS[@]}" \
  -v "$ROOT_DIR:$ROOT_DIR:z" -w "$ROOT_DIR" \
  -e IN_CONTAINER=1 -e BUILD_DIR="$BUILD_DIR" -e BIN_DIR="$BIN_DIR" \
  -e BUILD_TYPE="$BUILD_TYPE" -e ENABLE_AUDIO="$ENABLE_AUDIO" \
  -e JOBS="$JOBS" -e CMAKE_ARGS="$CMAKE_ARGS" -e INSTALL_BIN="$INSTALL_BIN" \
  "$IMAGE" "$ROOT_DIR/scripts/build-windows.sh"
