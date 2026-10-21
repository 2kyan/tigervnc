#!/bin/bash
# Builds the debug APK and installs + launches it on a connected,
# USB-debugging-enabled physical Android device.
#
# Requires core/ already built for Android arm64-v8a (see DESIGN.md's
# Android vertical slice findings, or third_party/android/build-deps.sh +
# a `cmake --build core/build/android` if core/ changed since the last
# build) -- ./gradlew's own CMake step fails with a clear message if it's
# missing or stale is not detected automatically, so rebuild it yourself
# after any core/ change.
#
# Usage: ./deploy-device.sh [device-serial]
# With no serial, the single attached device is used; if more than one
# is attached, pass one explicitly (see `adb devices` for serials).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

APP_ID="com.tigervnc.mobile.viewer"
SERIAL="${1:-}"

mapfile -t devices < <(adb devices | awk 'NR>1 && $2=="device" {print $1}')

if [ -z "$SERIAL" ]; then
  if [ "${#devices[@]}" -eq 0 ]; then
    echo "No authorized device attached. Connect via USB, enable Developer" >&2
    echo "options > USB debugging, and accept the RSA prompt on the device." >&2
    exit 1
  elif [ "${#devices[@]}" -gt 1 ]; then
    echo "Multiple devices attached -- pass one explicitly:" >&2
    printf '  %s\n' "${devices[@]}" >&2
    exit 1
  fi
  SERIAL="${devices[0]}"
fi

echo "==> Building debug APK"
./gradlew assembleDebug

APK="$ROOT_DIR/app/build/outputs/apk/debug/app-debug.apk"
echo "==> Installing on $SERIAL"
adb -s "$SERIAL" install -r "$APK"

echo "==> Launching"
adb -s "$SERIAL" shell am start -n "$APP_ID/.MainActivity"
