# Server-side audio setup

TigerVNC servers (`x0vncserver`, `w0vncserver` and `Xvnc`) can send the
remote machine's audio to the viewer over the TigerVNC-native RFB audio
extension (see `common/rfb/audioTypes.h`). This document covers how to
build the servers with audio support and how to set up the machine so
there is audio to capture.

## How capture works

The servers record from PulseAudio's **monitor source of the default
sink**, i.e. whatever the machine would play through its speakers (not a
microphone). The implementation lives in
`unix/x0vncserver/AudioCapturePulse.cxx` (shared by `w0vncserver`) and
`unix/xserver/hw/vnc/AudioCapturePulse.cxx` (Xvnc).

1. The server connects to the user's PulseAudio server (PipeWire's
   `pipewire-pulse` works too), asks for the default sink, and opens a
   record stream on `<default sink>.monitor` as S16LE, stereo, 48 kHz.
   PulseAudio resamples if the sink runs at a different rate.
2. Samples are collected into 20 ms frames (960 samples) and encoded
   with Opus at 64 kbit/s.
3. The monitor source produces samples even when nothing is playing, so
   the server only starts a stream once a frame rises above a small
   amplitude threshold, and ends it after about one second of silence.
4. Encoded frames are handed from PulseAudio's thread to the server's
   main thread and sent to every client that requested audio.
5. If no sound server is reachable, the server keeps running video-only
   and retries every 5 seconds. The same happens when the connection is
   lost later, e.g. when PipeWire stops and starts again: capture resumes
   within 5 seconds of the sound server coming back, on whatever the
   default sink is by then. A client re-enabling audio also triggers an
   immediate retry (at most once per second).

Audio only reaches viewers that support the extension: a `vncviewer`
built with Opus, or the mobile clients.

## Building with audio support

Install the development packages (Debian/Ubuntu):

```sh
sudo apt install libpulse-dev libopus-dev
```

### x0vncserver and w0vncserver (CMake)

Audio capture is built when both PulseAudio and Opus are found and audio
is not disabled:

| `ENABLE_AUDIO` | Behaviour |
|---|---|
| `AUTO` (default) | Built if libpulse and libopus are found, otherwise skipped with a warning |
| `ON` | Required; configuration fails if either library is missing |
| `OFF` | Not built; no dependency on libpulse or libopus |

`HAVE_AUDIO_CAPTURE` in the generated `config.h` tells you whether it
was enabled. You can confirm with:

```sh
ldd unix/x0vncserver/x0vncserver | grep -E 'pulse|opus'
```

### Xvnc (autotools)

Xvnc is built as part of the patched xorg-server tree, so `ENABLE_AUDIO`
does not apply to it. Use the configure option instead:

| Option | Behaviour |
|---|---|
| *(none)* / `--enable-vnc-audio=auto` | Built if libpulse and opus are found |
| `--enable-vnc-audio` | Required; configure fails if either is missing |
| `--disable-vnc-audio` | Not built |

## Setting up the machine

The server only needs a default sink to exist. It does not matter
whether real hardware is attached to it.

### Desktop with real sound hardware

`x0vncserver` and `w0vncserver` run inside an existing desktop session
and normally see that session's sound server already. Nothing more is
needed: whatever plays on the default output device is captured.

### Headless machine, VM, or Xvnc session

Create a virtual "null sink" and make it the default. Run these as the
same (normal) user that runs the VNC server, **not** as root, because
PulseAudio is a per-user daemon:

```sh
# Keep the user's services (and PulseAudio) alive after SSH logout;
# see "Without lingering" below
sudo loginctl enable-linger "$USER"

# Install and start PulseAudio if no sound server is running yet
sudo apt install pulseaudio pulseaudio-utils
pactl info >/dev/null 2>&1 || pulseaudio --start

# Create a virtual sink and make it the default
pactl load-module module-null-sink sink_name=virtual_speaker \
      sink_properties=device.description=Virtual_Speaker
pactl set-default-sink virtual_speaker
```

On a system that uses PipeWire, skip installing `pulseaudio` and make
sure PipeWire is running; the `pactl` commands are the same. PipeWire
can be installed but not running, for example when it was installed
after the user's systemd instance started. Start it with:

```sh
systemctl --user start pipewire.socket pipewire-pulse.socket \
  pipewire wireplumber pipewire-pulse
```

`scripts/setup-audio.sh` does all of the above.

The same setup also works on a machine with real sound hardware, if you
want audio to play only in the VNC viewer and not on the machine's own
speakers. Once the virtual sink is the default, move applications that
are already playing with `pactl move-sink-input <id> virtual_speaker`.
A running VNC server notices the new default sink and switches to it on
its own.

The null sink does not survive a restart of the sound server. To make it
permanent with PipeWire, create
`~/.config/pipewire/pipewire.conf.d/tigervnc-virtual_speaker.conf`
(WirePlumber remembers the default sink by itself):

```
context.objects = [
  { factory = adapter
    args = {
      factory.name     = support.null-audio-sink
      node.name        = "virtual_speaker"
      node.description = "Virtual_Speaker"
      media.class      = Audio/Sink
      audio.position   = [ FL FR ]
    }
  }
]
```

With PulseAudio, add these lines to `~/.config/pulse/default.pa`:

```
.include /etc/pulse/default.pa
load-module module-null-sink sink_name=virtual_speaker sink_properties=device.description=Virtual_Speaker
set-default-sink virtual_speaker
```

Startup order does not matter: if the VNC server starts before the sound
server, it picks it up on its next retry (within 5 seconds).

### Without lingering

PipeWire and PulseAudio run as the user's systemd services, and without
lingering systemd stops those when the user's last login session ends.
A VNC viewer connection is not a login session. So if you start the VNC
server with `vncserver` from an SSH login and then log out, the VNC
desktop keeps running but has no sound until you log in again. The VNC
server then reconnects by itself within 5 seconds; nothing needs
restarting.

To have sound without any other login, either enable lingering, or run
the VNC server as a systemd service, which opens its own login session
and so keeps the user's services running for as long as the VNC session
does:

```sh
# Map display :1 to your user, then start it as a service
echo ":1=$USER" | sudo tee -a /etc/tigervnc/vncserver.users
sudo systemctl enable --now tigervncserver@:1
```

(Stop a `vncserver`-started session on that display first, with
`vncserver -kill :1`.)

### Switching the output sink

The VNC server captures whichever sink is the default. On a machine with
real sound hardware, choose where audio plays by changing the default
sink: the virtual sink plays only in the VNC viewer, and a hardware sink
plays on the machine's own speakers as well as in the viewer.

```sh
# List sinks and show the current default
pactl list short sinks
pactl get-default-sink

# Play only in the VNC viewer
pactl set-default-sink virtual_speaker

# Play on the hardware too (use a name from "pactl list short sinks")
pactl set-default-sink alsa_output.<device>.analog-stereo
```

Applications that are already playing may stay on the old sink. Move
them too:

```sh
pactl list short sink-inputs | cut -f1 | xargs -r -I{} pactl move-sink-input {} virtual_speaker
```

The same can be done from a desktop's sound settings (in MATE: the
panel's volume icon, *Sound Preferences*, *Output* tab) or with
`pavucontrol`, whose *Playback* tab also moves single applications.

On PipeWire, WirePlumber remembers the chosen default across restarts
and reboots, as long as that sink still exists. A running VNC server
switches to the new default sink's monitor on its own and logs
`Default sink changed, now capturing from <sink>.monitor`.

## Verifying

1. Check the server log. While it cannot connect, it logs:

   ```
   No sound server found yet -- will retry every 5000ms
   ```

2. Check that the server is recording from the monitor source:

   ```sh
   pactl list source-outputs short
   ```

   There should be a stream from the TigerVNC application on
   `virtual_speaker.monitor` (or your real sink's `.monitor`).

3. Connect with an audio-capable viewer, then play something inside the
   session:

   ```sh
   paplay /usr/share/sounds/alsa/Front_Center.wav
   ```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Log keeps saying "No sound server found" | The server runs as a different user, or without the session environment (`XDG_RUNTIME_DIR` unset, e.g. started from cron or `sudo`). Run it as the user that owns the sound server, inside a proper login session. |
| "Could not determine default sink" | No sink exists. Create the null sink as shown above. |
| Connected but silent | Applications play to a sink other than the default. Check with `pactl list sink-inputs short` and move them with `pactl move-sink-input <id> virtual_speaker`. |
| Audio stops after SSH logout | Lingering is not enabled, so the sound server stopped with the last login session. Sound returns within 5 seconds of the next login; see "Without lingering" for how to avoid the gap. |
| No audio with an older viewer | The viewer does not support the TigerVNC-native extension (built without Opus, or a stock TigerVNC viewer). |
