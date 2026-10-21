#!/bin/bash
# Builds the Android app (mobile/android) end to end: cross-compiles
# mobile/third_party/android's dependencies (including libopus for the
# audio extension, see AUDIO.md), builds the mobile/core C ABI
# wrapper (tigervnc_core) for arm64-v8a against them, then assembles the
# APK via Gradle (which links the JNI module,
# mobile/android/app/src/main/cpp/CMakeLists.txt, against the prebuilt
# mobile/core/build/android output).
#
# Not the same as mobile/third_party/android/build-core.sh, which is the
# phase-1 spike that only checks TigerVNC's common/{core,rdr,network,rfb}
# compile standalone for Android.
#
# Requires: Android NDK + SDK (ANDROID_NDK_HOME auto-detected from
# ANDROID_SDK_ROOT/ANDROID_HOME if unset), and meson + ninja for
# build-deps.sh's pixman build. See mobile/DESIGN.md's Android vertical slice
# findings for background.
#
# Usage: scripts/build-android.sh [--release]
#   --release builds assembleRelease instead of assembleDebug.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOBILE_DIR="$ROOT_DIR/mobile"
ANDROID_DIR="$MOBILE_DIR/android"
THIRD_PARTY_DIR="$MOBILE_DIR/third_party/android"
SYSROOT="$THIRD_PARTY_DIR/sysroot"

GRADLE_TASK=assembleDebug
if [ "${1:-}" = "--release" ]; then
  GRADLE_TASK=assembleRelease
fi

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

echo "==> 1/3: cross-compiling mobile/third_party/android dependencies (incl. libopus)"
"$THIRD_PARTY_DIR/build-deps.sh"

echo "==> 2/3: building mobile/core (tigervnc_core) for Android arm64-v8a"
# Keep pkg-config inside the cross-compiled sysroot, so a host library
# with the same name can't be picked up by mistake.
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR=""
cmake -S "$MOBILE_DIR/core" -B "$MOBILE_DIR/core/build/android" \
  -DCMAKE_TOOLCHAIN_FILE="$THIRD_PARTY_DIR/android-arm64.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$MOBILE_DIR/core/build/android" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "==> 3/3: assembling the APK ($GRADLE_TASK)"
(cd "$ANDROID_DIR" && ./gradlew "$GRADLE_TASK")

echo "==> done. APK under mobile/android/app/build/outputs/apk/"
