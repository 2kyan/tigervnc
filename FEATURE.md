# Features

`dev` combines the feature branches below on top of upstream `master`.
Each feature is developed on its own branch based on `master`, so it can
be merged or sent upstream independently. Work that needs more than one
feature, such as audio in the mobile apps, only exists on `dev`. See
`BUILD.md` for building everything.

## Branches

| Branch | Based on | Contains |
|---|---|---|
| `master` | upstream TigerVNC | Upstream code, FLTK 1.3 |
| `fltk-1.4` | `master` | [FLTK 1.4](#fltk-14-fltk-14) |
| `audio-extension` | `master` | [Audio extension](#audio-extension-audio-extension) (FLTK 1.3) |
| `mobile` | `master` | [Mobile viewer](#mobile-viewer-mobile) (FLTK 1.3, no audio) |
| `dev` | `fltk-1.4` | FLTK 1.4 + audio extension + mobile viewer with audio |

## FLTK 1.4 (`fltk-1.4`)

- [x] `USE_BUNDLED_FLTK` CMake option to build FLTK from the
      `3rdparty/fltk` submodule (release-1.4.5) instead of the system FLTK
      (`cmake/BundledFLTK.cmake`)
- [x] Accept both FLTK 1.3 and 1.4 in the version check; detect 1.4 with
      `FLTK_1_4` (`FL_API_VERSION >= 10400`)
- [x] Force FLTK's X11 backend over Wayland (`fl_disable_wayland`;
      vncviewer exports its symbols so FLTK can find it)
- [x] HiDPI: scale XRender drawing to physical pixels using FLTK's
      screen scale (`vncviewer/Surface_X11.cxx`)
- [x] HiDPI: convert window geometry to FLTK's scaled units
      (`vncviewer/x11.cxx`)
- [x] Windows build with FLTK 1.4 (cross-built, `scripts/build-windows.sh`)
- [ ] macOS build with FLTK 1.4

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
- [ ] Windows viewer with Opus (waveOut output): builds
      (`scripts/build-windows.sh`), not run on Windows yet
- [ ] macOS audio playback (not supported upstream either)

Build:
- [x] Opus and PulseAudio are optional; `ENABLE_AUDIO=OFF` removes both
      dependencies
- [x] `scripts/build-viewer.sh` and `scripts/build-server.sh`
      (`dev` uses the bundled FLTK 1.4, `audio-extension` the system FLTK 1.3)
- [x] `AUDIO.md`: server-side audio setup
- [x] `scripts/setup-audio.sh`: sound server and null sink for headless
      machines

Testing:
- [x] All targets build with audio on and off (Linux)
- [x] Xvnc starts and connects to the sound server
- [ ] End-to-end playback from Xvnc / x0vncserver to vncviewer
- [x] Mobile clients set `supportsNativeAudio` (`dev` only, see below)

## Mobile viewer (`mobile`)

Imported from the `mobile_tigervncviewer` repository (its `master`,
without audio). See `mobile/README.md` and `mobile/DESIGN.md`.

Integration:
- [x] Project lives in `mobile/` and builds against this tree's
      `common/` and `cmake/` instead of a `vendor/tigervnc` submodule
- [x] iOS/Android NLS fixes committed to `common/core/i18n.cxx`
      (previously applied as a patch at build time)
- [x] Design document (`mobile/DESIGN.md`, was `PLAN.md`)
- [x] One-shot build scripts: `scripts/build-android.sh`,
      `scripts/build-ios.sh`

Core (`mobile/core`):
- [x] `tigervnc_core` C ABI around `rfb::CConnection`: connect,
      credentials and certificate verification, decoded frames,
      pointer/key/clipboard input
- [x] Cross-builds for Android arm64-v8a and iOS arm64 (device and
      Simulator), with GnuTLS/Nettle
- [x] Host build with a smoke test against a libvncserver test server

Apps:
- [x] Android app (Kotlin, Jetpack Compose, JNI)
- [x] iOS/iPad app (SwiftUI, generated with XcodeGen)
- [x] Remote resize (both platforms)
- [x] Android physical/Bluetooth keyboard input
- [x] Remember last host/port; Enter submits the connect and
      credentials forms
- [ ] Android on-screen keyboard input
- [ ] Clipboard sync surfaced in the apps
- [ ] Remaining feature parity (see `mobile/DESIGN.md` phase 5)
- [ ] Performance (phase 6)
- [ ] Polish and release (phase 7)

Testing:
- [x] Core host build and smoke test pass in this tree
- [x] Android APK builds in this tree with `scripts/build-android.sh`
- [ ] iOS build in this tree (needs a Mac; `build-ios.sh` not yet run)
- [ ] Android on a physical device

## Only on `dev`

Mobile audio (needs both the audio extension and the mobile viewer):
- [x] `mobile/core`: Opus decode and audio callbacks in `tigervnc_core.h`;
      requests the native extension (`supportsNativeAudio`)
- [x] Android playback and audio toggle (`VncBridge`, `SessionScreen`)
- [x] iOS playback (`AudioPlayer.swift`) and audio toggle
- [x] libopus cross-compiled for Android and iOS (device and Simulator)
- [x] Smoke test Opus round trip; `audio_capture_manual_test` records a
      real server's audio to WAV
- [x] Audio design and findings merged into `mobile/DESIGN.md`
- [ ] iOS audio built and played on a device or Simulator (needs a Mac)
- [ ] Mobile playback against Xvnc / x0vncserver in this tree

Build tooling:
- [x] `BUILD.md`: building everything in this tree
- [x] `scripts/install-deps.sh`: apt packages for all Linux builds
- [x] `scripts/build-windows.sh`: Windows viewer cross-built with
      MinGW-w64 in a Fedora container, copied with its DLLs to
      `bin/windows/`
- [x] Build scripts copy binaries to `bin/` under TigerVNC's Debian
      package names (`xtigervncviewer`, `x0tigervncserver`,
      `Xtigervnc`, ...)
- [ ] `scripts/install-deps.sh` and `scripts/setup-audio.sh` run on a
      fresh machine

Testing on `dev`:
- [x] Mobile core host build; smoke test passes, including the Opus
      audio round trip
- [x] `scripts/build-android.sh` builds the APK with audio
- [x] `scripts/build-viewer.sh` and `scripts/build-server.sh` (with
      Xvnc) build and copy to `bin/`
