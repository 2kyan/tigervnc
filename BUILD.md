# Building

The `dev` branch builds these from one tree, using the scripts in
`scripts/`:

- **Clients:** `vncviewer` (desktop, with the bundled FLTK 1.4), and the
  Android and iOS apps in `mobile/`.
- **Servers:** `x0vncserver`, `w0vncserver` and `Xvnc`.

All of them support the TigerVNC-native audio extension. See `AUDIO.md`
for how it works and how to set it up on the server, and `BUILDING.txt`
for upstream's generic build instructions.

| Target | Script | Tested |
|---|---|---|
| Linux build dependencies (apt) | `scripts/install-deps.sh` | no |
| `vncviewer` | `scripts/build-viewer.sh` | yes |
| `x0vncserver`, `w0vncserver`, `Xvnc` | `scripts/build-server.sh` | yes |
| Audio setup (sound server + null sink) | `scripts/setup-audio.sh` | no |
| Android app | `scripts/build-android.sh` | yes |
| iOS app | `scripts/build-ios.sh` | no, needs a Mac |

Run the scripts from anywhere; they find the tree from their own
location.

## Settings

The desktop scripts (`build-viewer.sh`, `build-server.sh`) share these
environment variables, defined in `scripts/common.sh`:

| Variable | Default | Meaning |
|---|---|---|
| `BUILD_TYPE` | `Release` | CMake build type (`Debug` turns warnings into errors) |
| `ENABLE_AUDIO` | `AUTO` | `AUTO`, `ON` or `OFF`; `OFF` drops libopus and PulseAudio |
| `JOBS` | number of CPUs | Parallel build jobs |
| `CMAKE_ARGS` | | Extra arguments for CMake |
| `BUILD_DIR` | `build-viewer` / `build-server` | Build directory |
| `BIN_DIR` | `bin` | Where finished binaries are copied |
| `INSTALL_BIN` | `1` | Set to `0` to skip copying to `BIN_DIR` |

## Linux: desktop viewer and servers

```sh
sudo scripts/install-deps.sh     # once; apt packages for everything below
scripts/build-viewer.sh          # vncviewer
scripts/build-server.sh          # x0vncserver, w0vncserver, Xvnc
```

`install-deps.sh` also runs `apt-get build-dep xserver-xorg-core` for
Xvnc. That needs `deb-src` entries enabled; if they aren't, it says so
and the rest of the packages are still installed.

Finished binaries are copied to `bin/` (ignored by git) under the names
TigerVNC's Debian packages use, so they can't be confused with a
distribution's `/usr/bin/x0vncserver` and such:

| Built file | Copied to `bin/` |
|---|---|
| `build-viewer/vncviewer/vncviewer` | `xtigervncviewer` |
| `build-server/unix/x0vncserver/x0vncserver` | `x0tigervncserver` |
| `build-server/unix/w0vncserver/w0vncserver` | `w0tigervncserver` |
| `build-server/unix/w0vncserver/w0vncserver-forget` | `w0tigervncserver-forget` |
| `build-server/unix/vncpasswd/vncpasswd` | `tigervncpasswd` |
| `build-server/unix/xserver/hw/vnc/Xvnc` | `Xtigervnc` |
| `build-server/unix/xserver/hw/vnc/.libs/libvnc.so` | `libvnc.so` |

`w0vncserver` is only built when the Wayland dependencies are found.

### Xvnc

`build-server.sh` builds Xvnc inside a patched copy of the X.Org server
source, after the CMake part:

| Variable | Default | Meaning |
|---|---|---|
| `XORG_SRC` | `/usr/src/xorg-server.tar.xz` | X.Org source, directory or tarball (from `xorg-server-source`) |
| `SKIP_XVNC` | | `1` builds only the CMake part |
| `CLEAN_XVNC` | | `1` starts over from fresh X.Org source |
| `XORG_CONFIGURE_ARGS` | | Extra arguments for the X.Org configure |

The X.Org tree is extracted, patched (`unix/xserver21.patch` or
`xserver120.patch`, picked from the source's version) and configured
only on the first run; later runs rebuild incrementally. The configure
paths match Debian/Ubuntu.

### Running what you built

```sh
bin/Xtigervnc :99 -SecurityTypes None -localhost -rfbport 5900
bin/x0tigervncserver -display :0 -rfbport 5900 -SecurityTypes None
bin/xtigervncviewer 127.0.0.1::5900
```

`w0tigervncserver` needs a Wayland session; see the "w0vncserver audio
capture" section of `mobile/DESIGN.md` for its two backends and for
testing in a nested `sway`.

### Audio on a VM or headless server

The servers capture from the default sink's monitor source, so they need
a sound server with a sink. On a machine without sound hardware:

```sh
scripts/setup-audio.sh           # as your normal user, not sudo
```

This enables lingering (so the sound server survives SSH logout),
starts PulseAudio if no PulseAudio-compatible server is running, and
creates a `virtual_speaker` null sink as the default. With PipeWire
already running, it only adds the sink. Start order doesn't matter: the
servers retry every 5 seconds. See `AUDIO.md` for details and
troubleshooting.

## Android

```sh
scripts/build-android.sh             # debug APK
scripts/build-android.sh --release   # release APK
```

Cross-compiles `mobile/third_party/android`'s dependencies (including
libopus) for arm64-v8a, builds `mobile/core` against them, then runs
Gradle. Needs the Android SDK and NDK (`ANDROID_NDK_HOME` is detected
from `ANDROID_SDK_ROOT`/`ANDROID_HOME` if unset), a JDK, and meson +
ninja.

Output: `mobile/android/app/build/outputs/apk/{debug,release}/`. Install
with `adb install -r <apk>`, or use `mobile/android/deploy-device.sh` /
`run-emulator.sh`.

## iOS

Not yet run on a Mac; treat it as a first draft.

```sh
scripts/build-ios.sh             # Simulator build, no signing needed
scripts/build-ios.sh --device    # device build (needs DEVELOPMENT_TEAM in mobile/ios/project.yml)
```

Needs Xcode with command line tools and `brew install xcodegen meson`.
Cross-compiles `mobile/third_party/ios`'s dependencies (including
libopus) for both device and Simulator, builds `mobile/core` against
each, generates the Xcode project with `xcodegen` and runs `xcodebuild`.

## Mobile core on the host

`mobile/core` also builds on Linux, with a smoke test against a
libvncserver test server that includes an Opus audio round trip:

```sh
cmake -S mobile/core -B mobile/core/build/host -DTIGERVNC_CORE_BUILD_TESTS=ON
cmake --build mobile/core/build/host
mobile/core/build/host/tests/smoke_test_server &
mobile/core/build/host/tests/smoke_test
```

`audio_capture_manual_test` (same directory) connects to a real server
and records the audio it receives to a WAV file, for checking real
audio end to end.

## Common problems

- **Wrong libopus when cross-compiling.** `mobile/core` finds Opus with
  pkg-config. If the host also has `libopus-dev` installed, a manual
  cross-compile can pick up the host's `opus.pc`. `build-android.sh` and
  `build-ios.sh` set `PKG_CONFIG_LIBDIR`, `PKG_CONFIG_PATH` and
  `PKG_CONFIG_SYSROOT_DIR` to prevent this; set them yourself if you run
  `cmake` on `mobile/core` by hand.
- **No audio from a server.** See the troubleshooting table in
  `AUDIO.md`.

## Further reading

- `AUDIO.md`: server-side audio setup.
- `FEATURE.md`: what each branch contains.
- `mobile/README.md` and `mobile/DESIGN.md`: the mobile apps, their
  design and the findings behind each piece (search for headings such as
  "Opus audio compression" or "Android Opus cross-compile").
