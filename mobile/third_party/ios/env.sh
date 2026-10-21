#!/bin/bash
# Common cross-compile environment for building iOS-arm64 (device) static
# libs consumed by core/. Source this before configuring/building any
# dependency: `source env.sh`.
set -e

export IOS_MIN_VERSION="13.0"
export IOS_ARCH="arm64"
export IOS_HOST_TRIPLE="arm64-apple-darwin"
export IOS_SDK_PATH="$(xcrun --sdk iphoneos --show-sdk-path)"

export ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SRC_DIR="$ROOT_DIR/src"
export BUILD_DIR="$ROOT_DIR/build"
export SYSROOT="$ROOT_DIR/sysroot"

mkdir -p "$SRC_DIR" "$BUILD_DIR" "$SYSROOT"

export CC="$(xcrun --sdk iphoneos -f clang)"
export CXX="$(xcrun --sdk iphoneos -f clang++)"
export AR="$(xcrun --sdk iphoneos -f ar)"
export RANLIB="$(xcrun --sdk iphoneos -f ranlib)"
export STRIP="$(xcrun --sdk iphoneos -f strip)"

export CFLAGS="-arch $IOS_ARCH -isysroot $IOS_SDK_PATH -miphoneos-version-min=$IOS_MIN_VERSION -fPIC -I$SYSROOT/include"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-arch $IOS_ARCH -isysroot $IOS_SDK_PATH -miphoneos-version-min=$IOS_MIN_VERSION -L$SYSROOT/lib"

# GNU autotools cross-compile settings (build=host machine, host=target)
export CONFIGURE_HOST="$IOS_HOST_TRIPLE"

echo "iOS cross env ready: SDK=$IOS_SDK_PATH ARCH=$IOS_ARCH MIN=$IOS_MIN_VERSION"
echo "Installing into SYSROOT=$SYSROOT"
