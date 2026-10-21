#!/bin/bash
# Builds the iOS app (mobile/ios) end to end: cross-compiles
# mobile/third_party/ios's dependencies for both the Simulator and a
# physical device (including libopus for the audio extension, see
# AUDIO.md), builds the mobile/core C ABI wrapper (tigervnc_core)
# against each, generates the Xcode project from mobile/ios/project.yml
# (XcodeGen), and builds the app.
#
# Not the same as mobile/third_party/ios/build-core.sh, which is the phase-1
# spike that only checks TigerVNC's common/{core,rdr,network,rfb}
# compile standalone for iOS.
#
# macOS only: needs Xcode + command line tools, XcodeGen
# (`brew install xcodegen`), and meson + ninja (`brew install meson`)
# for build-deps.sh's pixman build.
#
# Defaults to a Simulator build, which needs no code signing. Pass
# --device to build for a physical device instead (needs
# DEVELOPMENT_TEAM configured in mobile/ios/project.yml).
#
# Not yet run on a Mac; verify before relying on it.
#
# Usage: scripts/build-ios.sh [--device]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOBILE_DIR="$ROOT_DIR/mobile"
IOS_DIR="$MOBILE_DIR/ios"
THIRD_PARTY_DIR="$MOBILE_DIR/third_party/ios"

TARGET_SDK=iphonesimulator
if [ "${1:-}" = "--device" ]; then
  TARGET_SDK=iphoneos
fi

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "xcodegen not found -- install with: brew install xcodegen" >&2
  exit 1
fi

echo "==> 1/4: cross-compiling mobile/third_party/ios device dependencies (incl. libopus)"
"$THIRD_PARTY_DIR/build-deps.sh"

echo "==> 2/4: cross-compiling mobile/third_party/ios Simulator dependencies (incl. libopus)"
"$THIRD_PARTY_DIR/build-deps-sim.sh"

echo "==> 3/4: building mobile/core (tigervnc_core) for device and Simulator"
# Keep pkg-config inside each cross-compiled sysroot, so Homebrew
# libraries can't be picked up by mistake.
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR=""
PKG_CONFIG_LIBDIR="$THIRD_PARTY_DIR/sysroot/lib/pkgconfig" \
  cmake -S "$MOBILE_DIR/core" -B "$MOBILE_DIR/core/build/ios" \
    -DCMAKE_TOOLCHAIN_FILE="$THIRD_PARTY_DIR/ios-arm64.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$MOBILE_DIR/core/build/ios" -j"$(sysctl -n hw.ncpu)"
PKG_CONFIG_LIBDIR="$THIRD_PARTY_DIR/sysroot-sim/lib/pkgconfig" \
  cmake -S "$MOBILE_DIR/core" -B "$MOBILE_DIR/core/build/ios-sim" \
    -DCMAKE_TOOLCHAIN_FILE="$THIRD_PARTY_DIR/ios-arm64-sim.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$MOBILE_DIR/core/build/ios-sim" -j"$(sysctl -n hw.ncpu)"

echo "==> 4/4: generating the Xcode project and building for $TARGET_SDK"
(cd "$IOS_DIR" && xcodegen generate)
xcodebuild -project "$IOS_DIR/TigerVNCViewer.xcodeproj" \
  -scheme TigerVNCViewer -configuration Debug -sdk "$TARGET_SDK" build

echo "==> done."
