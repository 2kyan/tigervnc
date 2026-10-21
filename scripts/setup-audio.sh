#!/bin/bash
# Sets up a PulseAudio-compatible sound server with a default sink, so
# the servers' audio capture (AudioCapturePulse, see AUDIO.md) has
# something to record from. Needed on VMs and headless machines without
# sound hardware; a software-only "null sink" is enough, since the
# servers only read the sink's monitor source.
#
# Run as your normal user, not root: PulseAudio is a per-user daemon,
# and the VNC server must run as the same user. The steps that need
# privilege (enabling lingering, installing packages) use sudo.
#
# On systems that already run PipeWire's pipewire-pulse (or PulseAudio),
# this only adds the null sink and makes it the default.
#
# Lingering keeps the user's services, and with them the sound server,
# alive after the last login session (e.g. SSH) ends.
#
# Start order doesn't matter: the VNC servers retry connecting to the
# sound server every 5 seconds.
#
# Safe to re-run. The sink lasts until the sound server restarts; see
# AUDIO.md for making it permanent.
#
# Usage: scripts/setup-audio.sh
set -euo pipefail

SINK_NAME="${SINK_NAME:-virtual_speaker}"

if [ "$(id -u)" -eq 0 ]; then
  echo "Run this as your normal user, not root/sudo; it uses sudo itself" >&2
  echo "where needed." >&2
  exit 1
fi

if command -v loginctl >/dev/null 2>&1; then
  echo "==> enabling lingering for $USER (keeps the sound server alive after logout)"
  sudo loginctl enable-linger "$USER"
fi

if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  echo "!! XDG_RUNTIME_DIR is not set in this shell, so the sound server may" >&2
  echo "!! not be reachable from here. This usually means the login didn't" >&2
  echo "!! get a proper systemd session (check: systemctl --user status)." >&2
fi

if ! command -v pactl >/dev/null 2>&1; then
  echo "==> installing pactl"
  sudo apt-get update
  sudo apt-get install -y pulseaudio-utils
fi

if ! pactl info >/dev/null 2>&1; then
  # Nothing answers: no PipeWire/PulseAudio running for this user
  if ! command -v pulseaudio >/dev/null 2>&1; then
    echo "==> installing pulseaudio"
    sudo apt-get update
    sudo apt-get install -y pulseaudio
  fi
  echo "==> starting pulseaudio"
  pulseaudio --start
fi

if ! pactl list sinks short 2>/dev/null | grep -q "$SINK_NAME"; then
  echo "==> creating virtual sink '$SINK_NAME' (no audio hardware needed)"
  pactl load-module module-null-sink sink_name="$SINK_NAME" \
    sink_properties=device.description="Virtual_Speaker"
fi
pactl set-default-sink "$SINK_NAME"

echo "==> sinks:"
pactl list sinks short

echo "==> done. Applications in the VNC session should now play to '$SINK_NAME'."
