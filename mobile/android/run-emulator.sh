#!/bin/bash
# Boots an AVD (reusing one that's already running, if there is one),
# then builds and installs + launches the debug APK on it.
#
# Requires core/ already built for Android arm64-v8a -- see
# deploy-device.sh's header comment for the same caveat.
#
# Usage: ./run-emulator.sh [avd-name] [--headless]
# With no avd-name, the first AVD from `emulator -list-avds` is used.
# --headless runs with no window (e.g. for CI); default opens a normal
# emulator window. Either way the emulator keeps running in the
# background after this script exits.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

APP_ID="com.tigervnc.mobile.viewer"
AVD=""
HEADLESS=0

for arg in "$@"; do
  case "$arg" in
    --headless) HEADLESS=1 ;;
    *) AVD="$arg" ;;
  esac
done

if [ -z "$AVD" ]; then
  AVD="$(emulator -list-avds | head -1)"
  if [ -z "$AVD" ]; then
    echo "No AVDs found. Create one via Android Studio's Device Manager first." >&2
    exit 1
  fi
fi

# Reuse an already-running instance of this AVD instead of starting a
# second one (the emulator refuses that anyway unless -read-only).
SERIAL=""
for s in $(adb devices | awk 'NR>1 && $2=="device" {print $1}'); do
  case "$s" in
    emulator-*)
      running_avd="$(adb -s "$s" emu avd name 2>/dev/null | head -1 | tr -d '\r')"
      if [ "$running_avd" = "$AVD" ]; then
        SERIAL="$s"
      fi
      ;;
  esac
done

if [ -n "$SERIAL" ]; then
  echo "==> Reusing already-running $AVD ($SERIAL)"
else
  # Scope every wait to this new instance's own serial -- other devices
  # (a physical device, or another emulator) may already be attached and
  # already booted, so an unscoped `adb shell`/`adb wait-for-device` can
  # silently answer for the wrong device instead of failing.
  before_serials="$(adb devices | awk 'NR>1 && $1 ~ /^emulator-/ {print $1}')"

  echo "==> Booting $AVD"
  args=(-avd "$AVD")
  [ "$HEADLESS" -eq 1 ] && args+=(-no-window -no-audio -no-boot-anim -gpu swiftshader_indirect)
  nohup emulator "${args[@]}" > "/tmp/${AVD}-emulator.log" 2>&1 &
  disown

  echo "==> Waiting for its adb serial to appear"
  SERIAL=""
  while [ -z "$SERIAL" ]; do
    for s in $(adb devices | awk 'NR>1 && $1 ~ /^emulator-/ {print $1}'); do
      if ! grep -qx "$s" <<< "$before_serials"; then
        SERIAL="$s"
        break
      fi
    done
    [ -z "$SERIAL" ] && sleep 1
  done

  echo "==> Waiting for $SERIAL to finish booting"
  adb -s "$SERIAL" wait-for-device
  until [ "$(adb -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; do
    sleep 2
  done
  echo "==> Booted as $SERIAL"
fi

echo "==> Building debug APK"
./gradlew assembleDebug

APK="$ROOT_DIR/app/build/outputs/apk/debug/app-debug.apk"
echo "==> Installing on $SERIAL"
adb -s "$SERIAL" install -r "$APK"

echo "==> Launching"
adb -s "$SERIAL" shell am start -n "$APP_ID/.MainActivity"
