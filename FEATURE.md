# Features

`audio-extension` adds the TigerVNC-native audio extension to upstream
`master`, using the FLTK 1.3 viewer from `master`. The same feature
combined with FLTK 1.4 is on `dev`.

## Branches

| Branch | Based on | Contains |
|---|---|---|
| `master` | upstream TigerVNC | Upstream code, FLTK 1.3 |
| `audio-extension` (this branch) | `master` | [Audio extension](#audio-extension-audio-extension) |

## Audio extension (`audio-extension`)

TigerVNC-native RFB audio extension using Opus. See `AUDIO.md`.

Protocol (`common/rfb`):
- [x] `pseudoEncodingTigerAudio` and message types (`audioTypes.h`)
- [x] Client and server negotiation; prefers the native extension over
      QEMU's when both are offered
- [x] `supportsNativeAudio` so clients without Opus fall back to QEMU's
      raw PCM audio

Servers:
- [x] PulseAudio monitor-source capture with Opus encoding
      (`AudioCapturePulse`)
- [x] Only sends while audio is playing (silence detection)
- [x] Retries every 5s when no sound server is reachable, and when a
      client re-enables audio
- [x] `x0vncserver`
- [x] `w0vncserver`
- [x] Xvnc (`--enable-vnc-audio` configure option in the xserver patches)
- [ ] Only advertise the extension when capture is actually built in

Viewer:
- [x] Opus decoding in `vncviewer` (Linux, PulseAudio output)
- [ ] Windows viewer with Opus (waveOut output)
- [ ] macOS audio playback (not supported upstream either)

Build:
- [x] Opus and PulseAudio are optional; `ENABLE_AUDIO=OFF` removes both
      dependencies
- [x] `scripts/build-viewer.sh` (system FLTK 1.3) and
      `scripts/build-server.sh`
- [x] `AUDIO.md`: server-side audio setup
- [x] `scripts/setup-audio.sh`: sound server and virtual sink setup

Testing:
- [x] All targets build with audio on and off (Linux)
- [x] Xvnc starts and connects to the sound server
- [ ] End-to-end playback from Xvnc / x0vncserver to vncviewer
- [ ] Mobile clients (`mobile_tigervncviewer`) set `supportsNativeAudio`
