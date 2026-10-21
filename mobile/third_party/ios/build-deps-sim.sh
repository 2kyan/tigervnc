#!/bin/bash
# Same as build-deps.sh, but for the iOS Simulator (arm64, Apple
# Silicon) instead of a physical device: -mios-simulator-version-min
# instead of -miphoneos-version-min, the iphonesimulator SDK, and
# separate src-sim/build-sim/sysroot-sim trees so it doesn't collide
# with the device build's object files. Needed to run and see the app
# in Simulator without device provisioning. See build-deps.sh for
# per-dependency rationale -- this mirrors it exactly otherwise.
#
# Usage: ./build-deps-sim.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/src-sim"
BUILD_DIR="$ROOT_DIR/build-sim"
SYSROOT="$ROOT_DIR/sysroot-sim"
mkdir -p "$SRC_DIR" "$BUILD_DIR" "$SYSROOT"

IOS_SDK_PATH="$(xcrun --sdk iphonesimulator --show-sdk-path)"
CC="$(xcrun --sdk iphonesimulator -f clang)"
CXX="$(xcrun --sdk iphonesimulator -f clang++)"
CPPFLAGS="-arch arm64 -isysroot $IOS_SDK_PATH -mios-simulator-version-min=13.0 -I$SYSROOT/include"
CFLAGS="-arch arm64 -isysroot $IOS_SDK_PATH -mios-simulator-version-min=13.0 -fPIC -I$SYSROOT/include"
CXXFLAGS="$CFLAGS"
LDFLAGS="-arch arm64 -isysroot $IOS_SDK_PATH -mios-simulator-version-min=13.0 -L$SYSROOT/lib"

PIXMAN_VER=0.43.4
JPEG_VER=3.0.4
GMP_VER=6.3.0
TASN1_VER=4.20.0
NETTLE_VER=3.10.1
GNUTLS_VER=3.8.9
OPUS_VER=1.5.2

# Reuse the device build's already-downloaded tarballs if present.
DEVICE_SRC="$ROOT_DIR/src"

fetch() {
  local name="$1" url="$2"
  [ -f "$SRC_DIR/$name" ] && return
  if [ -f "$DEVICE_SRC/$name" ]; then
    cp "$DEVICE_SRC/$name" "$SRC_DIR/$name"
    return
  fi
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
  sed "s#__SDK__#${IOS_SDK_PATH}#g" "$ROOT_DIR/meson-ios-arm64-sim.ini" > "$BUILD_DIR/meson-ios-arm64-sim.resolved.ini"
  meson setup "$BUILD_DIR/pixman" "$SRC_DIR/pixman-$PIXMAN_VER" \
    --cross-file "$BUILD_DIR/meson-ios-arm64-sim.resolved.ini" \
    --prefix "$SYSROOT" \
    -Dtests=disabled -Ddemos=disabled -Dlibpng=disabled -Dgtk=disabled \
    -Ddefault_library=static
  meson install -C "$BUILD_DIR/pixman"
fi

# --- libjpeg-turbo (cmake) ---------------------------------------------
if [ ! -f "$SYSROOT/lib/libjpeg.a" ]; then
  echo "==> building libjpeg-turbo"
  cmake -S "$SRC_DIR/libjpeg-turbo-$JPEG_VER" -B "$BUILD_DIR/libjpeg-turbo" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/ios-arm64-sim.toolchain.cmake" \
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
    ./configure --host=arm64-apple-darwin --prefix="$SYSROOT" \
      --disable-shared --enable-static --disable-assembly \
      CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -j"$(sysctl -n hw.ncpu)" && make install)
fi

# --- libtasn1 (autotools; gnutls's ASN.1 parser) ------------------------
if [ ! -f "$SYSROOT/lib/libtasn1.a" ]; then
  echo "==> building libtasn1"
  (cd "$SRC_DIR/libtasn1-$TASN1_VER" && \
    ./configure --host=arm64-apple-darwin --prefix="$SYSROOT" \
      --disable-shared --enable-static \
      CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -C lib -j"$(sysctl -n hw.ncpu)" && make -C lib install)
fi

# --- nettle (autotools) --------------------------------------------------
if [ ! -f "$SYSROOT/lib/libnettle.a" ]; then
  echo "==> building nettle"
  (cd "$SRC_DIR/nettle-$NETTLE_VER" && \
    ./configure --host=arm64-apple-darwin --prefix="$SYSROOT" \
      --disable-shared --enable-static --disable-documentation \
      --with-include-path="$SYSROOT/include" --with-lib-path="$SYSROOT/lib" \
      CC="$CC" CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" && \
    make -j"$(sysctl -n hw.ncpu)" && make install)
fi

# --- gnutls (autotools) ---------------------------------------------------
if [ ! -f "$SYSROOT/lib/libgnutls.a" ]; then
  echo "==> building gnutls (this one takes a while)"
  (cd "$SRC_DIR/gnutls-$GNUTLS_VER" && \
    ./configure --host=arm64-apple-darwin --prefix="$SYSROOT" \
      --disable-shared --enable-static \
      --disable-doc --disable-tools --disable-cxx --disable-tests \
      --disable-nls --disable-libdane --disable-gost \
      --disable-hardware-acceleration \
      --without-p11-kit --without-tpm --without-tpm2 --without-idn \
      --with-included-unistring --without-brotli --without-zstd \
      --with-zlib=yes \
      CC="$CC" CPPFLAGS="$CPPFLAGS" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" \
      NETTLE_CFLAGS="-I$SYSROOT/include" NETTLE_LIBS="-L$SYSROOT/lib -lnettle" \
      HOGWEED_CFLAGS="-I$SYSROOT/include" HOGWEED_LIBS="-L$SYSROOT/lib -lhogweed" \
      GMP_CFLAGS="-I$SYSROOT/include" GMP_LIBS="-L$SYSROOT/lib -lgmp" \
      LIBTASN1_CFLAGS="-I$SYSROOT/include" LIBTASN1_LIBS="-L$SYSROOT/lib -ltasn1" \
      ZLIB_CFLAGS="" ZLIB_LIBS="-lz" && \
    make -j"$(sysctl -n hw.ncpu)" && make install)
fi

# --- opus (cmake) -------------------------------------------------------
if [ ! -f "$SYSROOT/lib/libopus.a" ]; then
  echo "==> building opus"
  cmake -S "$SRC_DIR/opus-$OPUS_VER" -B "$BUILD_DIR/opus" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/ios-arm64-sim.toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="$SYSROOT" \
    -DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF \
    -DOPUS_BUILD_PROGRAMS=OFF \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR/opus"
  cmake --install "$BUILD_DIR/opus"
fi

echo "==> done. iOS Simulator (arm64) dependencies installed under $SYSROOT"
