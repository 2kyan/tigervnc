#!/bin/bash
# Builds the TigerVNC servers: x0vncserver, w0vncserver (if Wayland
# support is found), the vncserver/vncsession wrappers and tools, and
# then Xvnc inside a patched copy of the X.Org server source.
#
# Usage: scripts/build-server.sh
#
# Settings (environment), in addition to those in common.sh:
#   BUILD_DIR            build directory                  (default: build-server)
#   XORG_SRC             X.Org server source, either a directory or a
#                        tarball   (default: /usr/src/xorg-server.tar.xz, from
#                        Debian/Ubuntu's xorg-server-source package)
#   SKIP_XVNC=1          only build the CMake part, not Xvnc
#   CLEAN_XVNC=1         start the Xvnc tree over from fresh X.Org source
#   XORG_CONFIGURE_ARGS  extra arguments for the X.Org configure
#
# Xvnc needs the X server's own build dependencies; on Debian/Ubuntu:
#   sudo apt install xorg-server-source
#   sudo apt-get build-dep xserver-xorg-core
#
# The X.Org tree is only extracted, patched and configured the first
# time; later runs rebuild incrementally. Use CLEAN_XVNC=1 after changing
# XORG_SRC or the unix/xserver*.patch files.

set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-server}"
XORG_SRC="${XORG_SRC:-/usr/src/xorg-server.tar.xz}"
SKIP_XVNC="${SKIP_XVNC:-0}"
CLEAN_XVNC="${CLEAN_XVNC:-0}"
XORG_CONFIGURE_ARGS="${XORG_CONFIGURE_ARGS:-}"

XSERVER_DIR="$BUILD_DIR/unix/xserver"

check_common_deps
if [ "$SKIP_XVNC" != "1" ]; then
  need_cmd autoreconf "autoconf automake libtool"
  need_cmd patch patch
  [ -e "$XORG_SRC" ] || die "X.Org source not found at $XORG_SRC; install xorg-server-source or set XORG_SRC"
fi

# Part 1: everything built by CMake. Xvnc links against the libraries
# and libtool files this produces, so it has to come first.
cmake_configure "$BUILD_DIR" -DBUILD_VIEWER=OFF
cmake_build "$BUILD_DIR"

if [ "$SKIP_XVNC" = "1" ]; then
  log "skipping Xvnc (SKIP_XVNC=1)"
  exit 0
fi

# Part 2: Xvnc, following the out-of-tree procedure in BUILDING.txt.

# Prints the matching unix/xserver*.patch for the X.Org tree in $1
xserver_patch()
{
  local version
  version="$(sed -n 's/^AC_INIT(\[xorg-server\], *\[\{0,1\}\([0-9.]*\).*/\1/p' "$1/configure.ac")"
  case "$version" in
    21.*)   echo "$ROOT_DIR/unix/xserver21.patch" ;;
    1.20.*) echo "$ROOT_DIR/unix/xserver120.patch" ;;
    *)      die "unsupported X.Org server version '$version' in $1" ;;
  esac
}

prepare_xserver()
{
  local src patchfile

  log "preparing X.Org tree in $XSERVER_DIR"
  rm -rf "$XSERVER_DIR"
  mkdir -p "$XSERVER_DIR"
  cp -R "$ROOT_DIR/unix/xserver/." "$XSERVER_DIR/"

  if [ -d "$XORG_SRC" ]; then
    src="$XORG_SRC"
  else
    XORG_TMP="$(mktemp -d)"
    trap 'rm -rf "$XORG_TMP"' EXIT
    log "extracting $XORG_SRC"
    tar -xf "$XORG_SRC" -C "$XORG_TMP"
    src="$(dirname "$(find "$XORG_TMP" -maxdepth 2 -name configure.ac | head -n 1)")"
    [ -f "$src/configure.ac" ] || die "no configure.ac found in $XORG_SRC"
  fi
  cp -R "$src/." "$XSERVER_DIR/"

  patchfile="$(xserver_patch "$XSERVER_DIR")"
  log "applying ${patchfile#"$ROOT_DIR"/}"
  patch -d "$XSERVER_DIR" -p1 < "$patchfile"

  log "running autoreconf (takes a while)"
  (cd "$XSERVER_DIR" && autoreconf -fi)

  configure_xserver
}

configure_xserver()
{
  local audio_arg=""

  case "$ENABLE_AUDIO" in
    ON)  audio_arg="--enable-vnc-audio" ;;
    OFF) audio_arg="--disable-vnc-audio" ;;
  esac

  # Paths match Debian/Ubuntu; adjust with XORG_CONFIGURE_ARGS elsewhere
  log "configuring Xvnc"
  # shellcheck disable=SC2086
  (cd "$XSERVER_DIR" && ./configure \
    --with-pic --without-dtrace --disable-static --disable-dri \
    --disable-xinerama --disable-xvfb --disable-xnest --disable-xorg \
    --disable-dmx --disable-xwin --disable-xephyr --disable-kdrive \
    --disable-config-hal --disable-config-udev --disable-dri2 --enable-glx \
    --with-default-font-path="catalogue:/etc/X11/fontpath.d,built-ins" \
    --with-xkb-path=/usr/share/X11/xkb \
    --with-xkb-output=/var/lib/xkb \
    --with-xkb-bin-directory=/usr/bin \
    --with-serverconfig-path=/usr/lib/xorg \
    $audio_arg $XORG_CONFIGURE_ARGS)
}

if [ "$CLEAN_XVNC" = "1" ] || [ ! -f "$XSERVER_DIR/Makefile" ]; then
  prepare_xserver
fi

log "building Xvnc"
make -C "$XSERVER_DIR" -j "$JOBS" TIGERVNC_SRCDIR="$ROOT_DIR" TIGERVNC_BUILDDIR="$BUILD_DIR"

log "built:"
for f in unix/x0vncserver/x0vncserver unix/w0vncserver/w0vncserver \
         unix/vncserver/vncserver unix/xserver/hw/vnc/Xvnc; do
  [ -e "$BUILD_DIR/$f" ] && echo "    $BUILD_DIR/$f"
done
echo "    try: $XSERVER_DIR/hw/vnc/Xvnc :99 -SecurityTypes None -localhost"
