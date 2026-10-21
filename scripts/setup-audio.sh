#!/bin/bash
# Sets up a PulseAudio-compatible sound server with a default sink, so
# the servers' audio capture (AudioCapturePulse, see AUDIO.md) has
# something to record from. Needed on VMs and headless machines without
# sound hardware; a software-only "null sink" is enough, since the
# servers only read the sink's monitor source.
#
# Run as your normal user, not root: PulseAudio is a per-user daemon,
# and the VNC server must run as the same user. The steps that need
# privilege (installing packages, enabling lingering) use sudo.
#
# On systems that already run PipeWire's pipewire-pulse (or PulseAudio),
# this only adds the null sink and makes it the default. If PipeWire is
# installed but not running, its user services are started; PulseAudio
# is only installed when there is no PipeWire.
#
# Lingering is left alone unless LINGER=1. It keeps the user's services,
# and with them the sound server, alive after the last login session
# (e.g. SSH) ends. Without it, sound pauses while nobody is logged in
# and the VNC servers reconnect once the sound server is back; running
# the VNC server as the tigervncserver@ service avoids the pause too
# (see AUDIO.md, "Without lingering").
#
# Start order doesn't matter: the VNC servers retry connecting to the
# sound server every 5 seconds.
#
# The virtual sink is always made the default, even when there is real
# sound hardware, so audio plays only to VNC clients and not on this
# machine's speakers. Running VNC servers switch to it by themselves.
#
# Safe to re-run. On PipeWire the sink is permanent; with PulseAudio it
# lasts until the sound server restarts, see AUDIO.md for making it
# permanent.
#
# Usage: scripts/setup-audio.sh
#        LINGER=1 scripts/setup-audio.sh
set -euo pipefail

SINK_NAME="${SINK_NAME:-virtual_speaker}"
LINGER="${LINGER:-0}"

if [ "$(id -u)" -eq 0 ]; then
  echo "Run this as your normal user, not root/sudo; it uses sudo itself" >&2
  echo "where needed." >&2
  exit 1
fi

if [ "$LINGER" = "1" ] && command -v loginctl >/dev/null 2>&1; then
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

if ! pactl info >/dev/null 2>&1 && command -v pipewire >/dev/null 2>&1; then
  # PipeWire is installed but not running, e.g. because it was installed
  # after the user's systemd instance started. WirePlumber is needed too,
  # or PipeWire has no sinks. Never fall back to installing PulseAudio
  # here: it would conflict with pipewire-pulse.
  echo "==> starting pipewire, wireplumber and pipewire-pulse"
  systemctl --user start pipewire.socket pipewire-pulse.socket \
    pipewire wireplumber pipewire-pulse ||
    { echo "!! could not start PipeWire's user services" >&2; exit 1; }
  for _ in $(seq 10); do
    pactl info >/dev/null 2>&1 && break
    sleep 1
  done
  if ! pactl info >/dev/null 2>&1; then
    echo "!! PipeWire is installed but pactl cannot reach it. Is" >&2
    echo "!! pipewire-pulse installed and running?" >&2
    echo "!! (check: systemctl --user status pipewire-pulse)" >&2
    exit 1
  fi
fi

if ! pactl info >/dev/null 2>&1; then
  # Nothing answers and there is no PipeWire: use PulseAudio
  if ! command -v pulseaudio >/dev/null 2>&1; then
    echo "==> installing pulseaudio"
    sudo apt-get update
    sudo apt-get install -y pulseaudio
  fi
  echo "==> starting pulseaudio"
  pulseaudio --start
fi

if pactl info 2>/dev/null | grep -q "on PipeWire"; then
  # Have PipeWire create the sink itself every time it starts.
  # WirePlumber remembers the default sink set below, so together they
  # survive restarts and reboots.
  conf="$HOME/.config/pipewire/pipewire.conf.d/tigervnc-$SINK_NAME.conf"
  if [ ! -e "$conf" ]; then
    echo "==> writing $conf (recreates '$SINK_NAME' when PipeWire starts)"
    mkdir -p "$(dirname "$conf")"
    cat > "$conf" <<EOF
# Virtual sink for TigerVNC audio capture; see scripts/setup-audio.sh
context.objects = [
  { factory = adapter
    args = {
      factory.name     = support.null-audio-sink
      node.name        = "$SINK_NAME"
      node.description = "Virtual_Speaker"
      media.class      = Audio/Sink
      audio.position   = [ FL FR ]
    }
  }
]
EOF
  fi
fi

if ! pactl list sinks short 2>/dev/null | grep -q "$SINK_NAME"; then
  # Also needed with the PipeWire config above, which only takes effect
  # the next time PipeWire starts
  echo "==> creating virtual sink '$SINK_NAME' (no audio hardware needed)"
  pactl load-module module-null-sink sink_name="$SINK_NAME" \
    sink_properties=device.description="Virtual_Speaker"
fi
pactl set-default-sink "$SINK_NAME"

# Applications that are already playing stay on their old sink (and so
# on real speakers, if any) unless moved
pactl list short sink-inputs | cut -f1 | while read -r id; do
  pactl move-sink-input "$id" "$SINK_NAME" || true
done

echo "==> sinks:"
pactl list sinks short

echo "==> done. Applications in the VNC session should now play to '$SINK_NAME'."
