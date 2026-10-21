# Features

`mobile` adds a native Android and iPad/iOS viewer to upstream `master`,
in `mobile/`. It uses the FLTK 1.3 desktop viewer from `master` and does
not include the audio extension (see `audio-extension` and `dev`).

## Branches

| Branch | Based on | Contains |
|---|---|---|
| `master` | upstream TigerVNC | Upstream code, FLTK 1.3 |
| `mobile` (this branch) | `master` | [Mobile viewer](#mobile-viewer-mobile) |

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
