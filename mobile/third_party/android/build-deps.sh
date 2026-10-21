#!/bin/bash
# Cross-compiles pixman, libjpeg-turbo, gmp, libtasn1, nettle, gnutls, and
# libopus for Android (arm64-v8a, API 24+) from source, installing them
# into ./sysroot. These are the non-NDK-sysroot dependencies of
# TigerVNC's common/{core,rdr,network,rfb} (libopus specifically for
# core/src/VncClientImpl.cxx's Opus decode -- see rfb/audioTypes.h and
# DESIGN.md's "Opus audio compression" notes for why). zlib is not
# vendored -- the NDK sysroot ships it (as a shared system lib, libz.so).
#
# Mirrors third_party/ios/build-deps.sh -- see it for background on why
# each dependency is needed and why gnutls/nettle aren't optional.
#
# Requires meson+ninja on PATH, and an installed NDK. On macOS:
# `brew install --cask android-ndk` (sets up /opt/homebrew/share/android-ndk)
# and `brew install meson`. On Linux: install the NDK via Android Studio's
# SDK Manager / `sdkmanager --install "ndk;<version>"`, and meson via your
# package manager or `pip install meson` (ninja is commonly already
# available). Set ANDROID_NDK_HOME if the NDK lives somewhere else than
# this script's per-OS default.
#
# Usage: ./build-deps.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/src"
BUILD_DIR="$ROOT_DIR/build"
SYSROOT="$ROOT_DIR/sysroot"
mkdir -p "$SRC_DIR" "$BUILD_DIR" "$SYSROOT"

HOST_OS="$(uname -s)"
case "$HOST_OS" in
  Darwin)
    : "${ANDROID_NDK_HOME:=/opt/homebrew/share/android-ndk}"
    NDK_PREBUILT="darwin-x86_64"
    NPROC="$(sysctl -n hw.ncpu)"
    ;;
  Linux)
    : "${ANDROID_NDK_HOME:=$(ls -d "${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"/ndk/*/ 2>/dev/null | sort -V | tail -1)}"
    ANDROID_NDK_HOME="${ANDROID_NDK_HOME%/}"
    NDK_PREBUILT="linux-x86_64"
    NPROC="$(nproc)"
    ;;
  *)
    echo "Unsupported host OS: $HOST_OS" >&2
    exit 1
    ;;
esac
export ANDROID_NDK_HOME
if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
  echo "ANDROID_NDK_HOME not found (looked for an installed NDK under the Android SDK) -- set it explicitly" >&2
  exit 1
fi
NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_PREBUILT/bin"
API=24
CC="$NDK_BIN/aarch64-linux-android${API}-clang"
CXX="$NDK_BIN/aarch64-linux-android${API}-clang++"
AR="$NDK_BIN/llvm-ar"
RANLIB="$NDK_BIN/llvm-ranlib"
STRIP="$NDK_BIN/llvm-strip"
CPPFLAGS="-I$SYSROOT/include"
CFLAGS="-fPIC -I$SYSROOT/include"
CXXFLAGS="$CFLAGS"
LDFLAGS="-L$SYSROOT/lib"
HOST_TRIPLE=aarch64-linux-android

PIXMAN_VER=0.43.4
JPEG_VER=3.0.4
GMP_VER=6.3.0
TASN1_VER=4.20.0
NETTLE_VER=3.10.1
GNUTLS_VER=3.8.9
OPUS_VER=1.5.2

fetch() {
  local name="$1" url="$2"
  [ -f "$SRC_DIR/$name" ] && return
  echo "==> fetching $name"
  curl -sL --max-time 120 -o "$SRC_DIR/$name" "$url"
}

fetch "pixman-$PIXMAN_VER.tar.gz" "https://www.cairographics.org/releases/pixman-$PIXMAN_VER.tar.gz"
fetch "libjpeg-turbo-$JPEG_VER.tar.gz" "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$JPEG_VER/libjpeg-turbo-$JPEG_VER.tar.gz"
fetch "gmp-$GMP_VER.tar.xz" "https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VER.tar.xz"
fetch "libtasn1-$TASN1_VER.tar.gz" "https://ftp.gnu.org/gnu/libtasn1/libtasn1-$TASN1_VER.tar.gz"
fetch "nettle-$NETTLE_VER.tar.gz" "https://ftp.gnu.org/gnu/nettle/nettle-$NETTLE_VER.tar.gz"
fetch "gnutls-$GNUTLS_VER.tar.xz" "https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/gnutls-$GNUTLS_VER.tar.xz"
fetch "opus-$OPUS_VER.tar.gz" "https://github.com/xiph/opus/releases/download/v$OPUS_VER/opus-$OPUS_VER.tar.gz"

for f in "$SRC_DIR"/*.tar.gz "$SRC_DIR"/*.tar.xz; do
  d="${f%.tar.*}"
  [ -d "$d" ] || tar xf "$f" -C "$SRC_DIR"
done

# --- pixman (meson) ---------------------------------------------------
if [ ! -f "$SYSROOT/lib/libpixman-1.a" ]; then
  echo "==> building pixman"
  sed "s#__NDK_BIN__#${NDK_BIN}#g" "$ROOT_DIR/meson-android-arm64.ini" > "$BUILD_DIR/meson-android-arm64.resolved.ini"
  meson setup "$BUILD_DIR/pixman" "$SRC_DIR/pixman-$PIXMAN_VER" \
    --cross-file "$BUILD_DIR/meson-android-arm64.resolved.ini" \
    --prefix "$SYSROOT" \
    -Dtests=disabled -Ddemos=disabled -Dlibpng=disabled -Dgtk=disabled \
    -Ddefault_library=static
  meson install -C "$BUILD_DIR/pixman"
fi

# --- libjpeg-turbo (cmake) ---------------------------------------------
if [ ! -f "$SYSROOT/lib/libjpeg.a" ]; then
  echo "==> building libjpeg-turbo"
  cmake -S "$SRC_DIR/libjpeg-turbo-$JPEG_VER" -B "$BUILD_DIR/libjpeg-turbo" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/android-arm64.toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_JAVA=OFF \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR/libjpeg-turbo"
  cmake --install "$BUILD_DIR/libjpeg-turbo"
fi

# --- gmp (autotools; needed by nettle's hogweed for RSA) ---------------
if [ ! -f "$SYSROOT/lib/libgmp.a" ]; then
  echo "==> building gmp"
  (cd "$SRC_DIR/gmp-$GMP_VER" && \
    ./configure --host="$HOST_TRIPLE" --prefix="$SYSROOT" \
      --disable-shared --enable-static --disable-assembly \
      CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
      CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -j"$NPROC" && make install)
fi

# --- libtasn1 (autotools; gnutls's ASN.1 parser) ------------------------
if [ ! -f "$SYSROOT/lib/libtasn1.a" ]; then
  echo "==> building libtasn1"
  (cd "$SRC_DIR/libtasn1-$TASN1_VER" && \
    ./configure --host="$HOST_TRIPLE" --prefix="$SYSROOT" \
      --disable-shared --enable-static \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
      CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -C lib -j"$NPROC" && make -C lib install)
fi

# --- nettle (autotools) --------------------------------------------------
if [ ! -f "$SYSROOT/lib/libnettle.a" ]; then
  echo "==> building nettle"
  (cd "$SRC_DIR/nettle-$NETTLE_VER" && \
    ./configure --host="$HOST_TRIPLE" --prefix="$SYSROOT" \
      --disable-shared --enable-static --disable-documentation \
      --with-include-path="$SYSROOT/include" --with-lib-path="$SYSROOT/lib" \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
      CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -j"$NPROC" && make install)
fi

# --- gnutls (autotools) ---------------------------------------------------
# Same minimal feature set as the iOS build -- see build-deps.sh there for
# why each --without-* flag is set.
if [ ! -f "$SYSROOT/lib/libgnutls.a" ]; then
  echo "==> building gnutls (this one takes a while)"
  (cd "$SRC_DIR/gnutls-$GNUTLS_VER" && \
    ./configure --host="$HOST_TRIPLE" --prefix="$SYSROOT" \
      --disable-shared --enable-static \
      --disable-doc --disable-tools --disable-cxx --disable-tests \
      --disable-nls --disable-libdane --disable-gost \
      --disable-hardware-acceleration \
      --without-p11-kit --without-tpm --without-tpm2 --without-idn \
      --with-included-unistring --without-brotli --without-zstd \
      --with-zlib=yes \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
      CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" \
      NETTLE_CFLAGS="-I$SYSROOT/include" NETTLE_LIBS="-L$SYSROOT/lib -lnettle" \
      HOGWEED_CFLAGS="-I$SYSROOT/include" HOGWEED_LIBS="-L$SYSROOT/lib -lhogweed" \
      GMP_CFLAGS="-I$SYSROOT/include" GMP_LIBS="-L$SYSROOT/lib -lgmp" \
      LIBTASN1_CFLAGS="-I$SYSROOT/include" LIBTASN1_LIBS="-L$SYSROOT/lib -ltasn1" \
      ZLIB_CFLAGS="" ZLIB_LIBS="-lz" && \
    make -j"$NPROC" && make install)
fi

# --- opus (cmake) -------------------------------------------------------
if [ ! -f "$SYSROOT/lib/libopus.a" ]; then
  echo "==> building opus"
  cmake -S "$SRC_DIR/opus-$OPUS_VER" -B "$BUILD_DIR/opus" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/android-arm64.toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
    -DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF \
    -DOPUS_BUILD_PROGRAMS=OFF \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR/opus"
  cmake --install "$BUILD_DIR/opus"
fi

echo "==> done. Android arm64-v8a dependencies installed under $SYSROOT"
