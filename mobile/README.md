# TigerVNC mobile viewer

A native VNC viewer for Android and iPad/iOS, built by reusing TigerVNC's
portable protocol core (`common/{core,network,rdr,rfb}` in this tree)
behind new, touch-first UIs, rather than porting TigerVNC's FLTK desktop
UI.

See [DESIGN.md](DESIGN.md) for architecture, phased roadmap, findings
and open decisions.

## Layout

| Path | Contents |
|---|---|
| `core/` | `tigervnc_core`, a C ABI wrapper around `rfb::CConnection` (`core/include/tigervnc_core.h`) |
| `android/` | Android app (Kotlin, Jetpack Compose, JNI bridge) |
| `ios/` | iOS/iPad app (SwiftUI); the Xcode project is generated from `project.yml` |
| `third_party/{android,ios}/` | Dependency cross-build scripts and toolchain files |

`core/common` and `core/cmake` are symlinks to the TigerVNC tree's
`common/` and `cmake/`.

## Building

The build scripts are in the repository root's `scripts/` folder; run
them from the root. Other paths below are relative to `mobile/`.

### Android

Needs the Android SDK and NDK (`ANDROID_NDK_HOME` is detected from
`ANDROID_SDK_ROOT`/`ANDROID_HOME` if unset), a JDK, and meson + ninja.

```sh
scripts/build-android.sh            # debug APK
scripts/build-android.sh --release  # release APK
```

This cross-compiles the dependencies (`third_party/android/build-deps.sh`),
builds `tigervnc_core` into `core/build/android`, and runs Gradle. The
APK ends up in `android/app/build/outputs/apk/`. `android/deploy-device.sh`
and `android/run-emulator.sh` install and run it.

### iOS (macOS only)

Needs Xcode with command line tools, `brew install xcodegen meson`.

```sh
scripts/build-ios.sh            # Simulator build
scripts/build-ios.sh --device   # device build (needs DEVELOPMENT_TEAM in project.yml)
```

### Host build and smoke test

`core/` also builds on the host, with a smoke test against a
libvncserver-based test server (needs `libvncserver-dev`, plus the
GnuTLS, Nettle, Pixman, libjpeg and zlib development packages):

```sh
cmake -S core -B core/build/host -DTIGERVNC_CORE_BUILD_TESTS=ON
cmake --build core/build/host
core/build/host/tests/smoke_test_server &
core/build/host/tests/smoke_test
```
