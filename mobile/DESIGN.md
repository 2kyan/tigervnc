# Design: TigerVNC on Android & iPad

> **Integrated into the TigerVNC tree.** This project was developed as a
> separate repository (`mobile_tigervncviewer`) that pulled TigerVNC in
> as a `vendor/tigervnc` submodule. It now lives in `mobile/` inside the
> TigerVNC source tree (`mobile` branch) and builds against the
> surrounding tree directly:
>
> - `vendor/tigervnc/` in the history below now means the TigerVNC tree
>   itself (`..` from this directory).
> - `patches/vendor-tigervnc/0001-ios-nls-fixes.patch` is committed
>   directly to `common/core/i18n.cxx`; the build scripts no longer
>   apply it.
> - The audio extension patches (`patches/vendor-tigervnc/0002`-`0006`)
>   are ordinary commits in this tree (`audio-extension` and `dev`
>   branches); setup is documented in `AUDIO.md` at the root.
> - The original repository's `server/` CMake project and its
>   `build-linux.sh`, `build-xvnc.sh`, `install-deps.sh` and
>   `setup-audio.sh` are replaced by `scripts/build-server.sh`,
>   `scripts/build-viewer.sh`, `scripts/install-deps.sh` and
>   `scripts/setup-audio.sh`; `BUILD.md` lives at the root.
> - This file was `PLAN.md` in the original repository.

## Goal

A native, touch-first VNC client for Android and iPad, reusing TigerVNC's
protocol core instead of rewriting RFB/encoding/crypto from scratch.

**Platform priority: iPad first.** This is a personal need (not just a
"someday" target), and this machine has the Mac/Xcode access iOS work
requires -- so the iOS/iPad vertical slice (phase 4 below) comes before
the Android one once phase 2's core wrapper is done, even though it's
listed second in the phased roadmap for historical reasons. Android
remains a real goal, just not the immediate one.

## Progress

Status is updated here as each roadmap step below is completed — check this
before assuming what's done.

- [x] Repo scaffolding: `PLAN.md`, `README.md`, `.gitignore`, `android/`
      `ios/` `core/` `docs/` placeholders, `vendor/tigervnc` submodule
      committed. (2026-09-09)
- [x] 1. Spike: core builds standalone (Android NDK) (2026-09-10) --
      same result as the iOS spike: `common/{core,rdr,network,rfb}`
      (incl. `rfbclient`/`rfbserver`) compile and link as arm64-v8a
      Android static libs, real GnuTLS/Nettle crypto. See **Android spike
      findings** below.
- [x] 1. Spike: core builds standalone (iOS toolchain) (2026-09-10) --
      `common/{core,rdr,network,rfb}` (incl. `rfbclient`/`rfbserver`)
      compile and link as arm64 iOS static libs, with real GnuTLS/Nettle
      TLS+AES crypto (no stubbing). See **iOS spike findings** below.
- [x] 2. Core wrapper API (C ABI) (2026-09-11) -- `core/` builds
      `tigervnc_core`, a C ABI (`core/include/tigervnc_core.h`) wrapping
      `rfb::CConnection`: connect/disconnect, credential and
      certificate/host-key verification callbacks, decoded-frame
      delivery, pointer/key/clipboard sending. Verified end-to-end
      against a real RFB server, and cross-builds for both iOS and
      Android using phase 1's toolchains/sysroots. See **Core wrapper
      (phase 2) findings** below.
- [x] 3. Android vertical slice (2026-09-19, on this Linux machine per
      PLAN.md's own "doable on Linux" note) -- (a) ported
      `third_party/android/build-deps.sh`/`build-core.sh` off their
      macOS-only assumptions and cross-compiled `tigervnc_core` for
      Android arm64-v8a on Linux; (b) `android/` is a real Gradle project
      (Kotlin + Jetpack Compose), JNI bridge (`app/src/main/cpp/
      VncBridge.cpp`) linking `tigervnc_core`; (c) connect screen, frame
      rendering to a `Canvas`, touch → pointer mapping, credential/cert
      verification prompts, mirroring the iOS vertical slice's scope; (d)
      `./gradlew assembleDebug` succeeds, `libvncjni.so` links clean, and
      -- installed on an API 36 x86_64 emulator (arm64 translation) --
      driven via `adb`/`uiautomator` through a real end-to-end session
      against a password-protected `core/tests/test_server.c`: connected,
      hit the credentials prompt, authenticated, rendered a live decoded
      frame (screenshotted for visual confirmation), and disconnected
      cleanly with no crash. See **Android vertical slice (phase 3)
      findings** below. Not yet tried on a physical device.
- [x] 4. iOS/iPad vertical slice (2026-09-11) -- `ios/` is a real SwiftUI
      app (generated via xcodegen from `ios/project.yml`) binding
      `tigervnc_core`: connect screen, live frame rendering, touch →
      pointer, on-screen keyboard → key events, credential/cert
      verification prompts. Built, installed, and run in the iOS
      Simulator against a real RFB server -- connected, rendered a live
      decoded frame on screen, screenshotted for visual confirmation. Not
      yet tried on a physical device (needs code-signing setup) or an
      iPad-specific pass (pointer/trackpad, Split View). See **iOS
      vertical slice (phase 4) findings** below.
- [ ] 5. Feature parity pass -- started early, out of roadmap order:
  - [x] Remote resize (server desktop size follows client window/view
        size), both iOS (2026-09-22) and Android (2026-09-26). See
        **Remote resize findings** below.
  - [x] Android physical/Bluetooth keyboard input (2026-09-26) --
        Android had no keyboard input at all until now (iOS already had
        on-screen keyboard input from phase 4). See **Android keyboard
        input findings** below. Android on-screen/soft keyboard input
        (iOS parity) still not done.
  - [x] Remember last-connected host/port (2026-09-26), both platforms --
        `VncBridge.connect()` now persists host/port
        (`SharedPreferences`/`UserDefaults`, same mechanism as
        `useReadableResize`), and the connect form pre-fills from it
        instead of always starting blank/5900. Verified on Android:
        connected to a distinctive `192.168.55.77:6001`, force-stopped
        and relaunched the app, confirmed the form showed those exact
        values again (and confirmed the raw `SharedPreferences` XML
        directly). iOS mirrors the same logic (`ConnectView` now has a
        manual `init()` seeding `@State` from `bridge.lastHost`/
        `lastPort`) but isn't yet run on the Mac.
  - [x] Enter/Return submits the connect form and credentials dialog
        (2026-09-26), both platforms -- a user asked specifically for
        this after the physical-keyboard fix above, since without it
        Enter did nothing in these two local-UI forms (as opposed to the
        session view, where Enter is forwarded to the *remote* desktop).
        Android: `KeyboardOptions(imeAction = ...)` +
        `KeyboardActions` on each `OutlinedTextField` (`Next` from
        Host/Username moves focus via `LocalFocusManager`, `Done` from
        Port/Password calls the same connect/submit lambda the button
        uses). iOS: `@FocusState` + `.submitLabel`/`.onSubmit` on the
        `Form`'s two fields; the credentials `.alert`'s fields don't
        support `@FocusState` binding the way `Form` fields do, so only
        the password field's `.onSubmit` is wired (username Return relies
        on the system's own default alert-field-advance behavior).
        Verified on Android via `adb shell input keyevent KEYCODE_ENTER`
        (same physical-keyboard code path as the keyboard-input fix):
        confirmed end-to-end against a real server twice -- Enter in the
        Port field connected, and Enter in the Password field
        authenticated -- both reaching `SessionScreen`, not just a UI
        state check. iOS unverified (needs the Mac).
  - [x] Remote audio (2026-09-27) -- new TigerVNC-native RFB audio
        extension, since the servers this app targets are standard
        desktop VNC servers (Xvnc/TigerVNC server/x11vnc), not QEMU --
        the only audio support vendored upstream is QEMU's own VNC
        audio extension, irrelevant to non-QEMU servers. Client side
        implemented and verified end-to-end on Linux (protocol +
        Android); iOS is written but unverified (needs the Mac).
        Server side (protocol + a real PulseAudio capture backend) done
        for *both* real TigerVNC server variants -- `x0vncserver` and
        the real, patched `Xvnc` X display server (a much bigger build:
        the actual X.Org `xserver` source, not just CMake) -- each
        verified against a real X11/PipeWire desktop with genuine
        captured audio (a real test tone), not just a mock. See
        **Remote audio extension findings**, **Remote audio extension:
        server-side + real PulseAudio capture**, and **Remote audio
        extension: Xvnc support** below.
  - [x] Opus-compress the audio wire format (2026-09-29) -- the initial
        implementation above sent raw S16LE PCM (~1.5Mbps), too high for
        a mobile client on a real network; switched to Opus at ~64kbps
        (~24x reduction). Host/Linux side (protocol constants, both
        `AudioCapturePulse` encoder copies, `VncClientImpl`'s decoder,
        both automated and real-audio-hardware verification) is
        complete. Android is now also cross-compiled and verified end-
        to-end on a real emulator (see the Android sub-item below); iOS
        cross-compiling `libopus` is still deferred -- a fresh
        `core/build/ios` configure will fail until that follow-up lands
        (the existing prebuilt iOS lib is unaffected). See **Opus audio
        compression** below.
    - [x] Android: cross-compile `libopus` + verify (2026-09-29) --
          `third_party/android/build-deps.sh` now also builds `libopus`
          (CMake, same pattern as `libjpeg-turbo`) into
          `third_party/android/sysroot`; `core/build/android` reconfigured
          and rebuilt against it (confirmed via `CMakeCache.txt` that
          `pkg_check_modules(OPUS)` resolved the cross-compiled
          `sysroot/lib/libopus.a`, not this host's own `libopus-dev`);
          `android/app/src/main/cpp/CMakeLists.txt` links `opus` into
          `libvncjni.so`. Verified for real on a running Android
          emulator: installed the rebuilt debug APK, connected to a
          real `x0vncserver` (real captured, Opus-encoded system
          audio), confirmed via `logcat` that the app's own `AudioTrack`
          is created exactly when audio starts (`createTrack_l`) and
          cleanly stops when it ends (buffer-underrun removal, not a
          crash), and confirmed the mute/unmute icon actually
          suppresses and resumes playback (`AudioTrack.
          restartIfDisabled` on unmute). No app-side crash on either
          end. This is the first genuinely complete client+server
          audio round trip on a real mobile client in this project.
  - [x] w0vncserver audio capture (2026-09-29, fully verified 2026-10-03)
        -- extended the same audio extension to `w0vncserver`
        (TigerVNC's Wayland server), the third and last real server
        variant this repo builds. Initially build-verified only, since
        this machine's GNOME portal declines the `RemoteDesktop`
        session (`Session creation inhibited` -- a local Mutter policy,
        root-caused to `org.gnome.desktop.remote-desktop.{rdp,vnc}
        enable` both being off system-wide). Closed by testing the
        *other* backend instead: stood up a nested `sway` compositor
        (no portal consent needed at all -- `WaylandDesktop` talks
        directly to wlr protocols) and confirmed a full real audio
        round trip through it -- clean disconnection, real video
        captured, RMS 8436 against a theoretical max of ~8485 for the
        test tone. See **w0vncserver audio capture** below for both the
        initial pass and the 2026-10-03 follow-up.
  - [x] vncviewer audio decode (2026-10-03) -- added Opus decode to
        vendor/tigervnc's own real, full-featured FLTK desktop client
        (`vncviewer`), rather than building a separate minimal Linux
        client from scratch: it already negotiated and received this
        extension's audio with zero plumbing changes needed (shares the
        same `handleAudioBegin/End/Data` callbacks QEMU's audio
        extension already used, already wired to vendor's own
        `AudioOutputPulse`); the only gap was Opus decode, which QEMU's
        raw-PCM audio never needed. Stood up `vncviewer`'s build in this
        repo for the first time (`server/CMakeLists.txt`, needed one new
        package: `libfltk1.3-dev`). Verified for real: connected a
        running `vncviewer` to a real `x0vncserver`, played a genuine
        tone, and confirmed via `wpctl status` both `AudioCapturePulse`
        (server, capturing) and `AudioOutputPulse` (vncviewer, playing)
        were simultaneously active -- real, live, audible playback, not
        just a clean build. See **vncviewer audio decode** below.
  - [x] Build scripts for every platform (2026-10-04) -- one-shot
        scripts to actually build everything this plan covers, instead
        of the ad-hoc hand-run commands used throughout development:
        `server/install-deps.sh` + `server/build-linux.sh` (Linux
        x0vncserver/w0vncserver/vncviewer), `android/build-android.sh`
        (full real pipeline: deps incl. libopus, core/, Gradle), and
        `ios/build-ios.sh` (same shape for iOS, both device+Simulator).
        Closed iOS's own libopus cross-compile gap as part of this
        (third_party/ios/build-deps{,-sim}.sh + project.yml) -- it had
        never been done, unlike Android's. Linux and Android scripts
        were actually run end-to-end and work; the iOS script is
        written to the same proven pattern but unverified (no Mac in
        this environment -- same standing gap as the rest of iOS). Also
        fixed a real gap found when asked "does build-xvnc.sh do the
        same patch-apply this needs too?": it didn't -- it copies
        `unix/xserver/hw/vnc` straight from the `vendor/tigervnc`
        submodule, so without the vendor patches already applied first
        it would have silently built an audio-less `Xvnc` instead of
        failing. Added the same defensive patch-apply loop there too,
        verified by actually running it (confirmed the new step
        correctly detects "already applied" and the rest of the build
        proceeds exactly as before). Also added `BUILD.md`, linked from
        `README.md`, with build instructions for every platform.
        **Follow-up (2026-10-04)**: user caught that `install-deps.sh`
        never covered `Xvnc`'s own, much bigger dependency chain
        (`xorg-server-source`/`xserver-xorg-dev` + `apt-get build-dep
        xserver-xorg-core`) -- only `x0vncserver`/`w0vncserver`/
        `vncviewer`'s. Added it, with the `build-dep` step's `deb-src`
        requirement handled as a non-fatal warning (doesn't abort
        installing the other packages if it can't run). This specific
        addition is less thoroughly verified than the rest of the
        scripts -- the dependency was already satisfied on the
        development machine before the script existed, so pushed
        without running it from a genuinely clean state; deferred
        real-world verification to the user's other machines.
        **Follow-up (2026-10-06)**: user deployed `Xvnc` on a fresh VM
        and hit "no sound devices" -- root cause was a VM with no real
        sound hardware, so `AudioCapturePulse` had no default sink to
        capture from (true for all three servers equally, since they
        share identical capture code). Walked through the fix (a
        PulseAudio-compatible daemon + a software "null sink" is
        sufficient, no real hardware needed) and the SSH-specific gotcha
        (`systemd-logind` kills a user's PulseAudio when their last SSH
        session ends unless `loginctl enable-linger` is set; also
        confirmed `AudioCapturePulse` only attempts the PulseAudio
        connection once, at server startup, with no retry, so ordering
        and restarts matter). Turned that into `server/setup-audio.sh`
        and a new "Audio setup" section in `BUILD.md`, rather than
        leaving it as chat-only guidance. **Unverified**: this
        development machine uses PipeWire's own native tools, not
        `pactl`/`pulseaudio-utils`, so the script has never actually
        been run end-to-end -- written against standard `pactl` usage,
        to be confirmed on a real VM. See **Build scripts** below.
  - [x] AudioCapturePulse automatic + manual reconnect (2026-10-06) --
        follow-up to `setup-audio.sh` above: user asked how to recover
        if PulseAudio gets (re)started *after* the VNC server already
        did, since `AudioCapturePulse` only ever tried once, at
        startup, with no retry. Added two things: a periodic background
        retry (every 5000ms while unavailable, via a new
        `core::Timer` on `AudioCapturePulse` itself) and a manual
        "retry now" trigger wired to the existing client mute/unmute
        control message (Android/iOS's mute button; `vncviewer`
        deliberately excluded, per explicit scope decision), via a new
        `VNCServerST::setAudioRetryCallback()`/`requestAudioRetry()`
        channel -- the first path in this extension that goes from the
        shared `rfb::` server classes back out to the platform-specific
        capture backend, not just the other way around. Also added a
        success log line, a real pre-existing gap (the original code
        never logged anything on a successful connect, only failures).
        Verified both paths for real using an isolated fake
        `XDG_RUNTIME_DIR` (no real desktop audio disrupted); found and
        ruled out a self-inflicted stdio-buffering false alarm along
        the way, not a real bug. See **AudioCapturePulse reconnect**
        below.
  - [x] Synced with the user's `../tigervnc` fork (branch
        `audio-extension-fltk-1.4`, commit `b0ccb88a`) (2026-10-20) --
        audio is now an optional build feature there (`HAVE_OPUS`/
        `HAVE_AUDIO_CAPTURE`, Xvnc `--enable-vnc-audio`), plus a new
        `CConnection::supportsNativeAudio` so an Opus-less viewer stops
        advertising the native extension. Ported into patches 0002-0006,
        with two fixes on top: `VncClientImpl` now sets
        `supportsNativeAudio` (mobile audio would otherwise have gone
        silently dead), and Xvnc gates on its own `VNC_AUDIO_CAPTURE`
        macro. `patches/xorg-server-source/` was retired. Host servers,
        `vncviewer`, core smoke test (with audio) and Xvnc were all
        rebuilt and verified. Then bumped the `vendor/tigervnc` pin
        `886800c7` -> `30df0c7f` (the fork's `master`, also upstream
        `master`) so patches and fork share one base; host servers,
        vncviewer, core smoke test, Android core + APK and Xvnc rebuilt
        on it. iOS not rebuilt (needs a Mac). See **Fork sync** below.
- [ ] 6. Performance
- [ ] 7. Polish/ship

## Resuming on another machine

1. Clone the TigerVNC fork and check out the `mobile` branch; the
   mobile project is in `mobile/`:
   ```
   git clone -b mobile git@github.com:2kyan/tigervnc.git
   ```
2. Read **Progress** below for the last completed step, then **Phased
   roadmap** for what that step means concretely. As of this writing,
   phases 1, 2, 3, and 4 are all done (see their findings sections below)
   -- `core/` has a real, working C ABI wrapper (`tigervnc_core`), and
   both `ios/` (SwiftUI) and `android/` (Kotlin/Compose) are real apps
   that have each been run and verified end-to-end against a real RFB
   server (Simulator for iOS, an emulator for Android).
   `ios/TigerVNCViewer.xcodeproj` is gitignored (generated from
   `ios/project.yml` -- run `xcodegen generate` in `ios/` after
   cloning/pulling, needs `brew install xcodegen`). `android/` needs its
   native side prebuilt before `./gradlew` can build the app -- see step 3
   below and **Android vertical slice (phase 3) findings**.
3. Nothing else external to restore — no secrets, env vars, or generated
   config are part of this project yet. Toolchain install (Android
   Studio/SDK/NDK, or Xcode + command line tools + `xcodegen`) is the
   only local setup needed; see **Environment / toolchain requirements**
   below for what's required per platform. To actually run the iOS app
   in Simulator (not just build it), the dependencies need a Simulator
   build too, not just device -- run `third_party/ios/build-deps-sim.sh`
   then rebuild `core/` with `ios-arm64-sim.toolchain.cmake` (see **iOS
   vertical slice (phase 4) findings** below) before opening the Xcode
   project. For Android: `third_party/android/build-deps.sh`, then
   `cmake -S core -B core/build/android -DCMAKE_TOOLCHAIN_FILE=
   third_party/android/android-arm64.toolchain.cmake
   -DCMAKE_BUILD_TYPE=Release && cmake --build core/build/android`, then
   `cd android && ./gradlew assembleDebug` (see **Android vertical slice
   (phase 3) findings** below for the NDK-version-pinning gotcha).
4. Keep updating **Progress** as steps complete, and commit/push DESIGN.md
   changes along with the code so this stays the single source of truth
   across machines.

## iOS spike findings (2026-09-10)

Built on macOS + Xcode 26.6. Full working recipe is in `third_party/ios/`:
`build-deps.sh` then `build-core.sh` reproduce everything below from
scratch (downloads sources, cross-compiles, builds core). See **Android
spike findings** below for the NDK equivalent, done right after this on
the same machine -- same outcome, same dependency chain, and the *source*
patch below turned out to apply to both.

- **Dependency chain, all cross-compiled from source for arm64 iOS
  device (13.0+), no Homebrew/macOS libs involved:** pixman (meson),
  libjpeg-turbo (cmake), gmp, libtasn1, nettle+hogweed, gnutls (all
  autotools). zlib is not vendored -- the iOS SDK ships it. Installed
  into `third_party/ios/sysroot/`.
- **Crypto decision resolved:** cross-compiled real gnutls+nettle rather
  than substituting CommonCrypto, per earlier discussion. Confirms the
  "Crypto library on mobile" open decision below -- upstream's TLS/AES
  code (`rdr/TLSSocket`, `rdr/AESInStream`/`AESOutStream`) hard-requires
  gnutls/nettle headers unconditionally (not gated behind the CMake
  `GNUTLS_FOUND`/`NETTLE_FOUND` flags the way higher-level files like
  `CSecurityTLS` are), so this was never optional.
- **One real upstream source patch needed** (not just build config):
  `common/core/i18n.cxx` unconditionally pulls in `<Carbon/Carbon.h>`
  under `#ifdef __APPLE__`, which also matches iOS (Carbon is macOS-only).
  Fixing that also exposed a latent, non-iOS-specific upstream bug in the
  same file's NLS-disabled fallback path (dgettext/etc. macros stripped
  and never restored) that just never triggers on desktop Linux, where
  gettext is always found. Patch kept at
  `patches/vendor-tigervnc/0001-ios-nls-fixes.patch` (see its README) and
  applied automatically by `build-core.sh` since we can't commit into the
  submodule directly. Worth upstreaming independent of this project.
- **`common/core/CMakeLists.txt`'s own logic doesn't need patching** --
  its `elseif(APPLE) target_link_libraries(core "-framework Carbon")`
  branch is dead weight for iOS (no such framework there) but harmless
  since our iOS build uses a hand-written CMakeLists (see below), not
  tigervnc's own. If phase 2's `core/` wrapper ever reuses tigervnc's
  top-level CMakeLists.txt directly, that branch will need an
  `AND NOT IOS`-style guard too.
- **Did not go through tigervnc's own top-level `CMakeLists.txt`** -- it
  hard-requires X11 (fine, skipped on `APPLE`) but also FLTK and other
  desktop/server-only deps we don't want. Instead
  `third_party/ios/build-core.sh` stages a minimal shim source directory
  (`coretree/`, gitignored) containing just our own `CMakeLists.txt` (in
  `third_party/ios/CoreSpikeCMakeLists.txt`) plus symlinks to
  `vendor/tigervnc/common` and `cmake`, because `common/CMakeLists.txt`
  insists on being reached via `${CMAKE_SOURCE_DIR}/common`. This shim
  will likely become (or feed into) phase 2's real `core/` CMake setup.
- **No other hidden desktop deps found** in core/rdr/network/rfb --
  checked for Cocoa/CoreGraphics/X11/Xlib/windows.h/winsock; everything
  else was already correctly platform-gated upstream.

## Android spike findings (2026-09-10)

Built on the same macOS machine as the iOS spike, using NDK r30 (`brew
install --cask android-ndk`) -- no separate Linux machine needed for this
after all, though the plan's original assumption (Android is Linux-doable
too) still holds. Full recipe in `third_party/android/`: `build-deps.sh`
then `build-core.sh`, structured identically to the iOS scripts. Same
outcome: `common/{core,rdr,network,rfb}` (incl. `rfbclient`/`rfbserver`)
build and link as arm64-v8a (API 24+) static libs, real GnuTLS/Nettle
crypto, no stubbing.

- **Same dependency chain as iOS**, cross-compiled from source for
  arm64-v8a instead: pixman (meson), libjpeg-turbo (cmake), gmp,
  libtasn1, nettle+hogweed, gnutls (autotools). zlib is not vendored --
  the NDK sysroot ships it (`libz.so`, a shared system lib rather than
  iOS's static `libz.tbd`, but same idea). Installed into
  `third_party/android/sysroot/`. The NDK's per-API-level versioned clang
  wrappers (e.g. `aarch64-linux-android24-clang`) already embed the
  target triple and sysroot, so no manual `--sysroot`/`-arch` flags were
  needed the way iOS's `xcrun`-provided raw clang needed them -- much
  less flag plumbing than the iOS build-deps.sh.
- **Same source patch applies unmodified for the Carbon/NLS-macro fixes**
  (`patches/vendor-tigervnc/0001-ios-nls-fixes.patch`, items 1-2) --
  confirms these were genuinely platform-agnostic bugs, not iOS quirks.
  **Found one more of the same shape**: `getlocaledir()`'s final `#else`
  branch returns `CMAKE_INSTALL_FULL_LOCALEDIR`, a macro only defined by
  tigervnc's own top-level CMakeLists.txt (via GNUInstallDirs) -- Android
  hits this as a hard compile error the same way iOS would have if its
  own `#elif` branch didn't already intercept it first. Fixed with an
  `#elif defined(__ANDROID__)` branch returning `nullptr`, same reasoning
  as iOS/tvOS. Patch file updated in place (still named
  `0001-ios-nls-fixes.patch` despite now covering both platforms -- see
  its own note in `patches/vendor-tigervnc/README.md`).
- **No hidden desktop deps found beyond the shared i18n.cxx issue** --
  same Cocoa/CoreGraphics/X11/Xlib/windows.h/winsock sweep as iOS, and
  `common/core/CMakeLists.txt`'s `elseif(APPLE)` Carbon-framework link is
  irrelevant here (not Apple), not just harmless.
- **Toolchain choice:** `ANDROID_STL=c++_static` for now (no shared
  runtime `.so` to bundle at this stage) and `ANDROID_PLATFORM=android-24`
  (a conservative, broadly-compatible minSdk baseline, picked the same
  way iOS's `13.0` deployment target was) -- both revisitable in phase 2/3
  once real app packaging constraints are known.
- **Did not go through tigervnc's own top-level `CMakeLists.txt`**, same
  as iOS and for the same reason (X11/FLTK/server deps); `build-core.sh`
  stages the same kind of minimal shim source directory
  (`coretree/`, gitignored) as the iOS build.

## Core wrapper (phase 2) findings (2026-09-11)

`core/` is now a real (non-throwaway) top-level CMake project -- see its
own top comment for the build instructions per platform. `common` and
`cmake` inside it are symlinks to the submodule's directories of the
same name, same reasoning as the phase-1 spikes' shim trees (tracked in
git this time, since this one's permanent). Builds and links for host
(macOS, via Homebrew pixman/jpeg-turbo/gnutls/nettle), iOS, and Android
(both via the phase-1 `third_party/{ios,android}` toolchains/sysroots --
run each platform's `build-deps.sh` first).

- **Threading model:** one background thread per `VncClient`, doing the
  blocking connect, the RFB handshake, and a `select()` loop over the
  socket fd plus a self-pipe used to wake it for outgoing events or
  disconnection. Every `vnc_client_*` call is safe from any thread and
  never blocks on network I/O. All callbacks fire on that background
  thread; the embedder (JNI/Swift layer, in later phases) is responsible
  for hopping to its own UI thread.
- **Verified against a real RFB server, not just compiled.** Added
  `core/tests/` (opt-in via `-DTIGERVNC_CORE_BUILD_TESTS=ON`, needs
  `brew install libvncserver`): a minimal RFB test server plus a smoke
  test exercising connect/disconnect, frame delivery and pixel format
  conversion, pointer/key/clipboard sending, a refused-connection error
  path, and a deliberate connect-then-immediately-disconnect race. This
  caught two real bugs before they could surface later as a hard-to-repro
  mobile crash or hang:
  1. **Wake-pipe creation race.** The self-pipe used to interrupt
     `select()` was originally created inside the worker thread itself.
     `disconnect()` called right after `connect()` (a completely normal
     caller pattern -- e.g. a screen closing before the connection
     finished) could run before the worker thread had created it,
     silently dropping the wake byte and hanging forever on `destroy()`.
     Fixed by creating the pipe in the constructor instead, synchronously
     before any thread can exist.
  2. **Framebuffer swapped out from under an in-flight decode.**
     `CConnection::setExtendedDesktopSize()` unconditionally calls
     `resizeFramebuffer()` -- including once during the very first
     update, even when the size didn't actually change. The original
     `resizeFramebuffer()` override replaced the pixel buffer object
     outright (`new` + `setFramebuffer()`); since that happens mid-update,
     it looked plausible this could orphan a buffer pointer a rect decode
     already held (it turned out `dataRect()` re-reads `framebuffer`
     fresh each call, so this exact path was safe, but the pattern is
     fragile and not guaranteed to stay that way). Fixed properly by
     resizing the existing `ManagedPixelBuffer` in place
     (`setPF()`/`setSize()`), matching how upstream's own pixel buffer
     classes are clearly meant to be used.
  A third apparent bug -- decoded pixels reading back as black/zero --
  turned out to be two test-harness mistakes, not a wrapper bug: sampling
  a pixel still mid-incremental-repaint (normal VNC behavior; fixed by
  checking the frame after an explicit `vnc_client_request_refresh()`)
  and sampling `(0,0)` specifically, which is permanently covered by the
  server's own baked-in cursor since the wrapper sets
  `supportsLocalCursor = false` (client-side cursor rendering isn't
  implemented yet -- see Open decisions).
- **Fixed pixel format for `frame_update`:** 32bpp RGBA8888 byte order
  (R,G,B,unused), regardless of the server's own native format --
  `CConnection`/`DecodeManager` do the conversion. Chosen for being a
  common, GPU-upload-friendly default; revisit if either platform's
  vertical slice wants zero-copy into its native bitmap format instead
  (see the existing "Rendering approach" open decision).
- **Scope deliberately deferred past phase 2** (not part of the C ABI
  yet): cursor rendering (`setCursor`/`setCursorPos` are recorded on
  `ServerParams` but not surfaced -- server bakes its own cursor in for
  now), audio, LED state, and the `SecurityClient::secTypes` global
  parameter (security type list) is left at its upstream default rather
  than exposed per-connection. All reasonable phase 5 (feature parity)
  candidates, not phase 2 blockers.

## iOS vertical slice (phase 4) findings (2026-09-11)

`ios/` is a real, non-throwaway Xcode project. `ios/project.yml` is the
tracked source of truth; `ios/TigerVNCViewer.xcodeproj` is generated from
it via `xcodegen generate` (`brew install xcodegen`) and gitignored, same
reasoning as any other generated-from-source-of-truth artifact in this
repo -- regenerate after cloning/pulling, before opening in Xcode.

- **Needed a second dependency build: iOS Simulator (arm64), not just
  device.** Phase 1/2 only ever cross-compiled for the `iphoneos` SDK
  (physical device). Running and actually *seeing* the app without
  dealing with code-signing/provisioning meant also cross-compiling
  pixman/jpeg/gmp/libtasn1/nettle/gnutls for the `iphonesimulator` SDK --
  same arm64 CPU architecture as device, but a genuinely different
  platform (confirmed via `otool -l` showing `LC_BUILD_VERSION platform
  7` = `PLATFORM_IOSSIMULATOR`), so device and simulator builds can't be
  `lipo`'d together the way multi-CPU-arch slices can. Added
  `third_party/ios/build-deps-sim.sh` (`-mios-simulator-version-min`
  instead of `-miphoneos-version-min`, separate `src-sim/`/`build-sim/`/
  `sysroot-sim/` trees, reuses the device build's already-downloaded
  tarballs) and `ios-arm64-sim.toolchain.cmake`, then built `core/` again
  against it (`core/build/ios-sim/`). The Xcode project's
  `LIBRARY_SEARCH_PATHS` is SDK-conditional
  (`[sdk=iphoneos*]`/`[sdk=iphonesimulator*]`) to point at whichever tree
  matches what's being built.
- **App structure:** SwiftUI, `ios/App/`. `VncBridge.swift` wraps
  `tigervnc_core`'s C callbacks (context = `Unmanaged<VncBridge>`,
  trampolined via non-capturing `@convention(c)`-eligible closures) as
  `@Published` state on an `ObservableObject`, converting each
  `frame_update` into a `CGImage` (`CGDataProvider` over a copy of the
  buffer -- the C callback's pointer is only valid for the call's
  duration). `ConnectView` is the host/port form plus credential and
  cert/host-key-verification alerts (SwiftUI's `alert(...) { }` with
  `TextField`/`SecureField` actions, iOS 15+). `SessionView` renders the
  frame aspect-fit in a `GeometryReader`, maps `DragGesture` locations
  through the same aspect-fit rect back to framebuffer pixel coordinates
  for `vnc_client_send_pointer_event`, and captures keyboard input via a
  custom always-empty `UITextField` subclass (`KeyboardCaptureField.swift`
  -- rejects every edit in its delegate and overrides `deleteBackward()`
  with `hasText` forced `true` so the backspace key still fires even
  though there's never any text to visibly delete) rather than diffing a
  bound `TextField` string, which is fragile once the field needs
  resetting after every character.
- **Deployment target is iOS 17, not 13** (the iOS *toolchain's* minimum,
  used for `core/`'s C++ cross-compile flags) -- SwiftUI's
  `NavigationStack`, the two-parameter `onChange(of:initial:)`, and
  `.persistentSystemOverlays` are all iOS 16/17+. No reason not to target
  current SwiftUI idiom for a brand-new app with no legacy users; the
  static C libraries linked in have a lower minimum than the app anyway,
  which is always fine (only the reverse -- a library with a *higher*
  minimum than its app -- would be a problem).
- **Verified by actually running it**, not just building: booted an iPad
  Pro (M5) Simulator (`xcrun simctl boot`; the runtime itself needed
  `xcodebuild -downloadPlatform iOS`, an ~8.5GB one-time download, since
  no Simulator runtime was preinstalled), installed and launched the
  built `.app`, and connected it to `core/tests/test_server.c` (the same
  throwaway RFB server from phase 2) running on the host Mac -- the iOS
  Simulator shares the host's network stack, so `127.0.0.1` from inside
  the Simulator reaches the Mac directly. Confirmed via `simctl io
  screenshot` that a live decoded frame renders on screen correctly.
  Added a `#if DEBUG`-gated auto-connect hook in `ConnectView`
  (`VNC_TEST_HOST`/`VNC_TEST_PORT` environment variables, read via
  `ProcessInfo`) since driving SwiftUI text fields and buttons from the
  command line has no simctl equivalent to real UI automation; env vars
  reach a `simctl launch`ed process only when exported as
  `SIMCTL_CHILD_<NAME>` on the *host* shell running `simctl`, not as
  plain trailing arguments to the launch command (those become `argv`
  instead).
- **Not yet done:** physical device testing (needs the user's own Apple
  ID/team in Xcode for code signing -- `CODE_SIGN_STYLE: Automatic` is
  set but no `DEVELOPMENT_TEAM`).

### iPad-specific polish pass (2026-09-11, same day)

Addressed the two iPad-specific items the phase 4 roadmap entry called
out up front (pointer/trackpad, Split View/Stage Manager-safe layout):

- **Pointer/trackpad support.** Replaced the SwiftUI `Image` +
  `DragGesture` combo with `RemoteScreenView.swift`, one
  `UIViewRepresentable`-wrapped `UIView` (`RemoteScreenUIView`) that owns
  the whole pointer surface, because hover tracking and button-masked
  clicks aren't expressible through SwiftUI's gesture system, and a
  UIKit gesture overlay layered on top of a *separate* SwiftUI touch
  target risks the two fighting over hit-testing:
  - Finger touch and a trackpad's primary click both arrive as ordinary
    `UITouch`es (iPadOS's own doing, not something we opted into), so
    `touchesBegan/Moved/Ended` already covers both -- no special-casing
    needed for "trackpad primary click" specifically.
  - Trackpad/mouse **hover** (pointer movement with no button held) is
    `UIHoverGestureRecognizer`, sending `buttonMask=0` moves. Guarded
    with a `primaryDown` flag so a click-drag in progress (already
    covered correctly by `touchesMoved`) doesn't get its held-button
    state stomped by a stray hover event mid-click.
  - Trackpad/mouse **secondary (right) click** is a `UITapGestureRecognizer`
    with `buttonMaskRequired = .secondary`. That property alone isn't
    enough, though -- it only filters *indirect* pointer input; a
    same-named caveat in Apple's docs notes it has no effect on finger
    touches, so without also setting `allowedTouchTypes = [.indirectPointer]`,
    every plain one-finger tap would misfire as a right-click too.
  - `UIPointerInteraction` gives the hovering system pointer a small
    dot shape instead of the default arrow -- it's standing in for the
    *remote* cursor's position (there's no local button/link for the
    system's usual lift/highlight hover effect to make sense over), so
    a distinct minimal shape reads better than the default arrow.
  - **Verified:** the refactor builds and still renders/passes the same
    primary-touch-driven session correctly in Simulator (re-ran the
    phase 4 screenshot check). The hover and secondary-click paths
    themselves are unverified beyond compiling against the real SDK
    signatures and sharing the already-proven coordinate-mapping code --
    exercising them for real needs either a physical trackpad or the
    Simulator's indirect-pointer input mode, neither of which is
    scriptable from here (no `simctl` equivalent for synthesizing hover
    or a secondary click). Worth a manual pass with a trackpad-equipped
    Mac before considering this fully done.
- **Split View/Stage Manager-safe layout.** Removed the `GeometryReader`
  from `SessionView`; `RemoteScreenUIView` now reads its own live
  `bounds` at touch/hover time for the aspect-fit math, and
  `layer.contentsGravity = .resizeAspect` lets Core Animation handle the
  visual scaling. Both are correct for *any* size the view is ever
  given, including a live resize mid-session (dragging a Split View
  divider), which is more robust than baking a captured size into gesture
  closures the way the `GeometryReader`-based version did (that also
  happened to be correct today, since `GeometryReader` re-invokes on
  size changes too, but was closer to accidentally-correct than
  structurally so). No fixed frame/width/height constants exist anywhere
  in the view hierarchy, and `UIRequiresFullScreen` is not set (which
  would have blocked Split View entirely). Not verified by actually
  triggering Split View/Stage Manager in Simulator, though -- doing that
  needs real drag-from-Dock interaction, which isn't scriptable here
  either; this is a structural argument for correctness, not an
  observed one.

### Bug found via real usage: premature VNC_STATE_CONNECTED (2026-09-11)

The user tried connecting to a real password-protected server on their
LAN and got an immediate, unconditional "Authentication cancelled" --
never even saw the credentials prompt. Reproduced locally with a
password-protected `libvncserver` test instance (`rfbCheckPasswordByList`)
rather than guessing: added file-based tracing (Simulator's `simctl
launch --console`/`--stdout`/`--stderr` did not actually capture the
app's `print()` output despite being the documented mechanism -- ended
up having the app append trace lines straight to a `/tmp` file instead,
which *does* work since Simulator shares the host filesystem).

Root cause: `VncClientImpl::runThread()` called
`reportState(VNC_STATE_CONNECTED, nullptr)` immediately after
`CConnection::initialiseProtocol()` -- but per `CConnection.cxx`,
`initialiseProtocol()` only sets `state_ = RFBSTATE_PROTOCOL_VERSION`
and does nothing else. The entire real handshake (version exchange,
security type selection, password auth, `ServerInit`) happens later,
driven by subsequent `processMsg()` calls in the `select()` loop just
below that call. So the UI was told "connected" -- and SwiftUI
immediately presented `SessionView` via its `fullScreenCover(isPresented:
onChange(of: bridge.state))` -- before authentication had even started.
By the time `getUserPasswd()` actually ran and fired
`credentials_needed`, the credentials alert (attached to the
now-covered `ConnectView`) had nowhere sensible to surface, and the
connection eventually tore down as cancelled.

Fixed by moving the `VNC_STATE_CONNECTED` report into
`VncClientImpl::initDone()`, which `CConnection` only calls from
`serverInit()` after the state machine actually reaches
`RFBSTATE_NORMAL` -- i.e. once auth has genuinely succeeded. This also
matches what `tigervnc_core.h` already documented `VNC_STATE_CONNECTED`
as meaning ("session established, frames will follow"); the
implementation just didn't live up to its own contract yet.

Verified via the fixed flow end-to-end against the password-protected
test server: extended the existing debug-only auto-connect hook with an
optional `VNC_TEST_PASSWORD` env var that auto-answers the credentials
prompt when set, so the whole connect → credentials-needed →
provide-credentials → authenticated → frames-rendering path could be
exercised and confirmed via screenshot without needing real screen taps
(same `simctl` limitation noted in the polish-pass section above).

## Android vertical slice (phase 3) findings (2026-09-19)

Built entirely on this Linux machine (Ubuntu 26.04), the first time this
project's Android path has been exercised end-to-end on Linux rather than
the macOS machine phase 1's Android spike used -- confirms PLAN.md's own
"doable on Linux" environment note.

- **Host tooling gaps vs. the macOS-authored scripts, all fixable without
  root:** `meson` (needed for pixman) wasn't installed and apt requires
  sudo (no passwordless sudo on this machine) -- worked around with a
  throwaway Python venv (`python3 -m venv` + a manually fetched
  `get-pip.py`, since this distro's `python3-venv` doesn't include
  `ensurepip`) and `pip install meson` into it, no system changes needed.
  `gradle` was present but ancient (4.4.1, predates modern AGP) -- used it
  once, only to run `gradle wrapper --gradle-version 8.9` and generate a
  proper wrapper, then never touched the system gradle again.
- **`third_party/android/build-deps.sh`/`build-core.sh` were macOS-only**
  (hardcoded `darwin-x86_64` NDK prebuilt dir, `sysctl -n hw.ncpu`,
  Homebrew's NDK path as the `ANDROID_NDK_HOME` default). Made both
  host-OS-aware (`uname -s` branch for Darwin vs. Linux) rather than
  Linux-only forks, so they still work unchanged on the original macOS
  machine. Same dependency chain and same one upstream patch
  (`patches/vendor-tigervnc/0001-ios-nls-fixes.patch`) as the original
  Android spike -- nothing new needed there. The `vendor/tigervnc`
  submodule itself also wasn't checked out yet on this machine (`git
  submodule status` showed a `-` prefix) -- `git submodule update --init
  --recursive` first, per **Resuming on another machine** above.
- **`core/` builds for Android exactly as documented**: `cmake -S core -B
  core/build/android -DCMAKE_TOOLCHAIN_FILE=third_party/android/
  android-arm64.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && cmake
  --build core/build/android` -- no changes needed to `core/CMakeLists.txt`
  itself, confirming phase 2's cross-platform design held up.
- **JNI bridge (`android/app/src/main/cpp/VncBridge.cpp`) mirrors
  `ios/App/VncBridge.swift`'s role**: trampolines tigervnc_core's C
  callbacks (which fire on its own background thread, per
  `tigervnc_core.h`'s documented threading model) into the host language.
  Since a JNI callback can arrive on a thread the JVM has never seen, each
  one wraps a `ScopedEnv` helper that calls `AttachCurrentThread` /
  `DetachCurrentThread` per-call rather than leaking a permanent
  attachment (the worker thread isn't JNI-created, so nothing else would
  ever detach it). `frame_update`'s packed RGBA8888 rows (padded to
  `stride`, not necessarily `width*4`) are converted to Android's
  0xAARRGGBB "color int" format and repacked tightly in native code before
  crossing into a `jintArray` -- naively reinterpreting the raw bytes as
  `int[]` would put the unused/alpha byte in the wrong place and could
  render a fully transparent bitmap.
- **App links `tigervnc_core` as prebuilt static libs, not via
  `add_subdirectory`**, mirroring `ios/project.yml`'s
  `LIBRARY_SEARCH_PATHS`/`OTHER_LDFLAGS` approach exactly (same library
  list and order: `tigervnc_core rfbclient rfb rdr network core gnutls
  hogweed nettle gmp tasn1 pixman-1 jpeg z`). This isn't just style
  parity with iOS -- `vendor/tigervnc/common/CMakeLists.txt` itself
  resolves paths via `CMAKE_SOURCE_DIR` (the whole build's top directory),
  not `CMAKE_CURRENT_SOURCE_DIR`, so `add_subdirectory(core)` from
  Gradle's own CMake invocation would silently break those path
  assumptions; building `core/` as its own top-level CMake project first
  and linking the resulting `.a` files avoids that entirely.
- **AGP auto-downloads its own default NDK version if `android.ndkVersion`
  isn't pinned** (silently pulled NDK 27.0.12077973 here, separate from
  the NDK 29.0.13846066 used to prebuild `core/`/`third_party/android`'s
  deps) -- pinned `ndkVersion` in `app/build.gradle.kts` to match, to
  avoid linking JNI glue compiled by one NDK against static libs built by
  another. Also cost one debugging round-trip on an off-by-one `../../..`
  path-traversal typo in `app/src/main/cpp/CMakeLists.txt` (five `..` to
  reach the repo root from `android/app/src/main/cpp`, not six).
- **Host Linux build of `core/tests` needed one more fix of the same
  shape as the Android/iOS `i18n.cxx` gaps**: `getlocaledir()`'s generic
  `#else` branch (the one Android/iOS's own `#elif` branches bypass)
  unconditionally returns `CMAKE_INSTALL_FULL_LOCALEDIR`, a macro only
  ever defined by tigervnc's own top-level `CMakeLists.txt` (via
  GNUInstallDirs) -- never hit before because prior host builds of
  `core/tests` were done on macOS, whose own `__APPLE__ && TARGET_OS_OSX`
  branch intercepts first. This is a `core/CMakeLists.txt` build-config
  fix, not another vendor source patch: `add_compile_definitions
  (CMAKE_INSTALL_FULL_LOCALEDIR="")` for host Linux, mirroring the
  existing `Intl_INCLUDE_DIR`/`Intl_LIBRARIES` "needed even with NLS off"
  precedent right above it in the same file.
- **`libvncserver-dev` wasn't installed** (needed for `core/tests`' fake
  RFB server, `TIGERVNC_CORE_BUILD_TESTS=ON`) and, unlike the meson gap,
  isn't pip-installable -- the user ran `sudo apt install -y
  libvncserver-dev` (this session has no passwordless sudo).
- **Verified by actually running it, end-to-end, not just installed**:
  installed the debug APK on a local `google_apis` x86_64 API 36 emulator
  (AVD name `Android_API_36`, headless via `emulator -no-window`) -- the
  emulator's arm64-v8a binary translation (`ro.product.cpu.abilist` lists
  both `x86_64` and `arm64-v8a`) ran the arm64-only build without needing
  a second x86_64 cross-compile of the whole dependency chain. Then, with
  no `simctl`-equivalent scripted-UI limitation on Android (unlike both
  iOS verification passes above), drove the real UI via `adb shell input`
  taps/text plus `uiautomator dump` to read back rendered text/bounds:
  filled in the connect form with `10.0.2.2:15900` (the emulator's
  host-loopback alias) pointing at a password-protected
  `core/tests/test_server.c` instance running on this host, hit the
  `credentials_needed` prompt, entered the password, and reached
  `SessionScreen` with a live decoded frame rendered (`screencap`
  screenshot confirms the aspect-fit brown frame matching the server's
  known test pattern) -- the exact connect → credentials-needed →
  authenticated → frames-rendering path the iOS
  premature-`VNC_STATE_CONNECTED` bug above once broke, and it worked
  correctly here on the first try since that fix lives in shared
  `core/` code. Tapped disconnect: returned cleanly to `ConnectScreen`,
  no crash, confirmed via `adb logcat`.

## Remote resize findings (2026-09-22)

TigerVNC's own desktop viewer calls this "remote resize": the client
asks the server to change its virtual desktop's pixel dimensions (RFB
`SetDesktopSize`), typically to match the client window, rather than the
client scaling/letterboxing a fixed-size remote desktop into whatever
window size it has. Modeled directly on
`vncviewer/DesktopWindow.cxx`'s `remoteResize()` -- specifically its
single-screen ("windowed mode") branch; this app has no
fullscreen/multi-monitor concept to worry about, so the multi-screen
branch there doesn't apply.

- **`core/` additions:** `vnc_client_request_resize(client, width,
  height)` in `tigervnc_core.h`, plus a `resize_result` callback
  (`VncResizeResult`: OK/PROHIBITED/NO_RESOURCES/INVALID/UNSUPPORTED).
  Success doesn't strictly need the callback -- a resize just shows up
  as an ordinary `frame_update` with different dimensions -- but
  surfacing an explicit failure (e.g. server refuses because a policy
  disallows it) is more useful than silently doing nothing. Implemented
  as `VncClientImpl::sendResizeRequest()` (builds a single-screen
  `rfb::ScreenSet` covering the new size and calls
  `writer()->writeSetDesktopSize()`) plus a new
  `setExtendedDesktopSize()` override that calls the `CConnection` base
  implementation first (so our own framebuffer/state updates before we
  report anything), then checks `reason == rfb::reasonClient` to
  distinguish "this is the server's answer to our own request" from a
  server- or other-client-driven resize. Requests coalesce like pointer
  moves (latest size wins if called again before the worker thread gets
  to an earlier one) rather than queuing.
- **Bug found and fixed while touching this code, unrelated to resize
  itself:** the worker thread's clipboard-request handling
  (`if (clipboardNow) requestClipboard();`) resolved, via ordinary C++
  name hiding, to `VncClientImpl`'s own same-named public queuing
  wrapper instead of `CConnection::requestClipboard()` (the real
  protocol call) -- so it would have re-queued itself forever instead of
  ever actually sending a clipboard request, the moment anything called
  `vnc_client_request_clipboard()`. Confirmed the mechanism with an
  isolated minimal repro of the name-hiding rule before touching
  anything, then fixed by qualifying the call
  (`CConnection::requestClipboard()`). Never caught before now because
  the smoke test didn't exercise clipboard at all; it does now.
- **iOS side:** `RemoteScreenUIView` (the same UIKit view from the iPad
  polish pass) overrides `layoutSubviews()`, converts its own `bounds`
  to pixels via `traitCollection.displayScale`, and calls a new
  `onResize` closure -- debounced 0.35s so a live Split View/Stage
  Manager drag (many rapid layout passes) sends one request once the
  size settles, not one per intermediate frame. Since `layoutSubviews()`
  also fires on the view's first layout, this doubles as "match the
  window size at connect time" with no special-casing needed -- the
  first `SetDesktopSize` goes out automatically the moment the session
  view appears.
- **Android side (2026-09-26), added after the iOS one and after pulling
  in an intervening upstream `core/` change:** mirrors the iOS
  implementation as closely as the platforms allow. `VncBridge.cpp`
  gained `nativeRequestResize()` and wired the new `resize_result`
  callback through to a Kotlin `onResizeResult()`; `VncBridge.kt` gained
  `requestResize()` and a `resizeUnsupported` flag (same shape as iOS's,
  reset on each new connection). `SessionScreen.kt` attaches
  `Modifier.onSizeChanged` to the session view and debounces it 350ms
  (matching iOS's 0.35s) before calling `requestResize` -- Compose's
  `onSizeChanged` already reports raw pixels, so unlike iOS's
  `displayScale` multiply, no points-to-pixels conversion is needed. The
  350ms/0.35s debounce interval is currently a duplicated magic number
  (Kotlin int vs. Swift `TimeInterval`), not a shared constant -- worth
  hoisting into `tigervnc_core.h` if it ever needs to change, since both
  platforms already include that header.
- **Verified two ways on Android, mirroring the iOS verification:**
  rebuilt `core/` for Android first (this session's pull had touched it
  since the last Android build) and reran the host `smoke_test`, which
  now exercises the same resize assertion as iOS's verification did.
  Then ran the real app on an API 36 emulator against
  `core/tests/test_server.c`: the server's log confirmed `Client
  requested resolution change to (1080x2400)` / `resize successful`
  immediately on connect (matching the emulator's screen exactly), and
  forcing a rotation to landscape (`adb shell settings put system
  user_rotation 1`) produced a second request for the swapped
  `(2400x1080)` -- confirmed both by the log and by a screenshot showing
  the frame filling the rotated screen edge-to-edge with no
  letterboxing.
- **Verified two ways:** `core/tests/smoke_test.cxx` now requests a
  resize to a specific size (400x300) against `core/tests/test_server.c`
  (which gained a real `setDesktopSizeHook` -- reallocates and repaints
  its framebuffer at the new size, not a stub) and asserts the resulting
  `frame_update` actually has those dimensions -- passes. Separately, ran
  the real iOS app in Simulator against that same resize-capable server:
  the server's own log confirms `Client requested resolution change to
  (1668x2370)` / `resize successful` -- the iPad Pro 11" (M5) simulator's
  real pixel dimensions (1668pt × 2x scale, minus the status bar/toolbar
  area outside the remote-screen view) -- immediately on connect, and
  the screenshot shows the frame filling the screen edge-to-edge instead
  of the small letterboxed 320x240 image from before. Exactly one
  `SetDesktopSize` was sent for the whole session (per the server's own
  stats log), confirming the debounce/coalescing didn't flood it.
- **Not done (as of 2026-09-22):** no user-facing control over this yet
  (always-on, automatic -- there's no way to turn it off if a user wants
  the server's own native resolution with letterboxing/scrolling
  instead). `resizeUnsupported` is plumbed through to `VncBridge` but
  nothing in the UI reads it yet. Both reasonable follow-ups for a later
  feature parity pass, not blockers for this one. (The first of these
  two gaps -- no user control -- is addressed by the Readable/Sharp
  toggle below, added 2026-09-26. `resizeUnsupported` is still unread by
  either UI.)
- **DPI/readability bug found and fixed (2026-09-26):** both platforms
  were requesting `SetDesktopSize` in raw physical pixels (Android:
  Compose `onSizeChanged`'s native unit; iOS: `bounds` explicitly
  multiplied by `traitCollection.displayScale`). Since RFB's
  `SetDesktopSize` carries no DPI field, servers rendered their desktop
  UI at their own default DPI assumption but at that full pixel count --
  e.g. a 1080x2400/density-420 (2.625x) device requesting exactly that --
  making remote text/icons physically tiny even though the frame itself
  filled the screen sharply. Checked TigerVNC's own upstream desktop
  client (`vendor/tigervnc/vncviewer/DesktopWindow.cxx`'s
  `remoteResize()`) for a precedent first -- it has zero DPI awareness
  either (raw FLTK window pixels), and the vendored tree doesn't even
  have TigerVNC's old desktop "Scaling" option anymore, so there was no
  existing convention to mirror; this needed its own design.
- **Fix + new Readable/Sharp toggle (2026-09-26):** requesting the
  platform's density-independent unit (dp on Android, points on iOS)
  instead of raw pixels keeps remote UI legible -- the same trick a
  Retina Mac's scaled "looks like" display modes use. Made this a
  user-facing, persisted choice rather than a silent default change per
  discussion: `VncBridge` on both platforms now owns a persisted
  `useReadableResize` boolean (`SharedPreferences` on Android --
  `VncBridge`'s constructor now takes a `Context`, always
  `applicationContext` to avoid leaks; `UserDefaults` on iOS, via a
  `didSet` on the `@Published` property), defaulting `true`. A small
  toggle button in each session view's top bar (Android: next to the
  disconnect "×", showing "Aa"/"1:1"; iOS: in the existing overlay
  `HStack`, `textformat.size`/`viewfinder` SF Symbols) flips it
  immediately -- both platforms key their resize-triggering effect on
  the flag too (Android's `LaunchedEffect(viewSize, useReadableResize)`;
  iOS's `RemoteScreenUIView.useReadableResize`'s `didSet` forces
  `scheduleResizeIfNeeded()` by clearing `lastScheduledSize` first), so
  toggling re-requests immediately even if the view's own size hasn't
  changed. No `core/` change needed anywhere -- `vnc_client_request_
  resize()` was always unit-agnostic, correctly, since RFB itself has no
  DPI concept at all.
- **Verified for real on Android** (iOS deferred to the Mac per this
  file's own toolchain notes -- code written, not yet run) against
  `core/tests/test_server.c` on an API 36 emulator (1080x2400 physical,
  density 420 = 2.625x): default Readable mode requested
  `(411x914)` -- exactly 1080/2.625 and 2400/2.625 rounded -- confirmed
  via the server's `Client requested resolution change to (...)` log
  line (`test_server.c` warns "New width (411) is not a multiple of 4"
  but still reports `resize successful`; harmless, no server-side
  rejection). Tapping the toggle immediately produced a fresh request
  for `(1080x2400)` (Sharp/raw pixels), then force-stopping and
  relaunching the app and reconnecting showed the *first* connect-time
  request already used `(1080x2400)` -- confirming the `SharedPreferences`
  persistence survives a real process restart, not just an in-memory
  toggle. Toggling back to Readable immediately produced `(411x914)`
  again. A screenshot after each toggle confirms the aspect-fit `Canvas`
  draw needed no changes -- the frame fills the screen edge-to-edge in
  both modes, just upscaled from a smaller framebuffer in Readable mode.

## Android keyboard input findings (2026-09-26)

Found via a user report: connecting from a tablet with an attached
physical keyboard, typing did nothing. Turned out Android had **no
keyboard input implemented at all** -- `VncBridge.kt` always exposed
`sendKeyEvent()` (a thin wrapper over `vnc_client_send_key_event()`), but
nothing in `SessionScreen.kt` ever called it. iOS has had on-screen
keyboard input since phase 4 (`KeyboardCaptureField.swift`'s hidden
always-empty `UITextField` trick); Android never got any keyboard path,
physical or on-screen.

- **Scope: physical/Bluetooth keyboards only, not the on-screen
  keyboard.** A hardware keyboard reliably generates a discrete
  `android.view.KeyEvent` per keystroke, which Compose's
  `Modifier.onKeyEvent` can capture directly. A software IME generally
  does *not* -- composing/predictive text often bypasses `KeyEvent`
  dispatch entirely, which is why iOS's own on-screen-keyboard solution
  needed the hidden-`UITextField` trick instead of raw key capture.
  Android on-screen-keyboard input (an iOS-equivalent solution) is a
  separate, still-open follow-up, not attempted here.
- **Focus:** `SessionScreen`'s root `Box` is made focusable
  (`.focusRequester(focusRequester).focusable()`) and requests focus once
  on first composition; the pointer-input handler also re-requests focus
  on every tap, in case clicking the resize-mode/disconnect buttons stole
  it (Material3's clickable `Surface` is focusable too).
- **Android `KeyEvent` -> X11 keysym mapping**
  (`androidKeyEventToKeysym()` in `SessionScreen.kt`), since
  `vnc_client_send_key_event()` expects an X11 keysym per
  `tigervnc_core.h`'s documented contract: non-printable/control keys
  (arrows, Tab, Return, Escape, Backspace/Delete, Home/End, Page Up/Down,
  Insert, Caps Lock, Shift/Control/Alt/Meta, F1-F12) map to fixed
  keysyms from X11's `keysymdef.h` (stable, part of the core X11
  protocol spec -- not guessed). Printable characters use
  `event.unicodeChar` directly: X11 keysyms for Latin-1 (0x20-0xFF) equal
  the Unicode code point exactly, and beyond that X11's "Unicode keysym"
  convention is `0x01000000 + code point`. Dead/accent keys (Android
  flags these via `KeyCharacterMap.COMBINING_ACCENT`) are skipped rather
  than forwarded wrong -- proper multi-keystroke accent composition isn't
  implemented.
- **Test infrastructure:** `core/tests/test_server.c` gained a
  `kbdAddEvent` hook (`onKbdAddEvent`, logs every keysym/down-state it
  receives) -- it had none before, so there was no way to observe
  received key events short of the framebuffer visibly reacting to them.
  Mirrors how `setDesktopSizeHook` was added for the remote-resize
  feature's own verification.
- **Verified for real**, not just compiled: connected the real app to
  `test_server.c` on an API 36 emulator, then simulated physical-keyboard
  input via `adb shell input keyevent`/`input text` (these route through
  the same `android.view.KeyEvent` dispatch a real Bluetooth keyboard
  uses) and confirmed via the server's new log line: `a` ->
  `keysym=0x0061`, Enter -> `0xff0d`, Left arrow -> `0xff51`, Tab ->
  `0xff09` (and Tab reached our handler rather than being swallowed by
  Compose's own focus-traversal system). Typing `"Hi!"` confirmed
  Shift is forwarded correctly around shifted characters too: `Shift_L
  down (0xffe1) -> H (0x0048) -> Shift_L up`, `i (0x0069)` unshifted,
  `Shift_L down -> ! (0x0021) -> Shift_L up` -- matching standard X11/VNC
  modifier-key handling (hold modifier, send the already-shifted keysym
  for the letter, release modifier).

## Environment / toolchain requirements

- **Android:** doable on Linux (Android SDK/NDK both run there; build,
  emulator, and USB device testing all work) or on macOS (confirmed
  2026-09-10: `brew install --cask android-ndk` was all phase 1's Android
  half needed, no Android Studio/SDK required for a CMake-only build like
  this). No macOS-specific requirement exists for phases 1-3.
- **iOS/iPad:** requires macOS + Xcode at the point of compiling against
  the iOS SDK, running on simulator/device, code-signing, or
  TestFlight/App Store submission — this is an Apple-enforced restriction,
  not a portability gap we can code around. Writing Swift/C++ source can
  happen anywhere, but phase 1 (iOS half) and phase 4 onward need actual
  Mac access: a spare Mac, or cloud Mac CI (GitHub Actions `macos-latest`,
  Codemagic, MacStadium). Not needed until we reach those phases.

## Architecture

- **Core (reused, portable C++, no UI dependency):** `common/{core,network,rdr,rfb}` of the TigerVNC tree
  - RFB protocol state machine, encodings (Tight/ZRLE/Raw/etc.), pixel
    formats, TLS/crypto (via nettle), zlib framing.
  - Consumed via CMake as a static lib on both platforms.
- **Discarded (not reused):** `vncviewer/**` — FLTK widgets
  and platform glue (`*X11*`, `*Win32*`, `cocoa.mm`). Desktop
  mouse/keyboard interaction model doesn't fit touch anyway.
- **New per-platform layers:**
  - `android/`: Kotlin app, Jetpack Compose UI, JNI bridge (`core-jni/`)
    wrapping the C++ core, built via NDK/CMake.
  - `ios/`: Swift app, SwiftUI UI, thin Obj-C++/Swift bridging layer
    wrapping the core as a static lib, built via Xcode + CMake.
  - Each platform owns: connection UI, framebuffer rendering (surface/view
    receiving decoded RGB frames from the core), touch→input translation
    (tap = click, drag = pointer move, pinch = zoom, long-press = right
    click, on-screen keyboard handling), credential/settings storage.

## Repo layout

```
common/                     TigerVNC protocol core (shared with the desktop viewer)
scripts/                    build scripts: desktop viewer/servers, Android, iOS
mobile/
  DESIGN.md                 this document
  core/                     thin wrapper API around common/rfb (C ABI for JNI/Swift interop)
  android/                  Android app
  ios/                      iOS/iPad app
  third_party/{ios,android} dependency and core cross-build scripts, toolchains
```

## Phased roadmap

1. **Spike: build the core standalone.** Get `common/{core,network,rdr,rfb}`
   compiling as a static lib with the Android NDK toolchain and separately
   with Xcode's iOS toolchain, no UI. Prove out crypto/TLS lib availability
   on both (nettle or a substitute) and confirm no hidden FLTK/X11/Win32
   deps leak into these dirs.
2. **Core wrapper API.** Design a small C ABI (`core/`) around `CConnection`
   equivalent: connect(host, port, credentials), receive decoded frame
   buffer callbacks, send pointer/key events. This is the only surface
   both platforms bind against.
3. **Android vertical slice.** JNI bridge + Compose UI: connect dialog,
   single-session view rendering frames to a `SurfaceView`/`Canvas`,
   basic touch → pointer mapping. Get one real VNC session working
   end-to-end.
4. **iOS/iPad vertical slice (priority target -- see Goal).** Same
   milestone as phase 3, targeting iPad first: Swift bridge, SwiftUI
   connect screen, `CAMetalLayer` or `UIView` frame rendering, touch →
   pointer mapping. iPad-specific from the start rather than deferred to
   phase 5: layout that tolerates Split View/Slide Over/Stage Manager
   resizing (no fixed-size assumptions), and pointer/trackpad support
   (iPadOS's native `UIPointerInteraction`) since that's a first-class
   iPad input method, not just a "feature parity" nice-to-have.
5. **Feature parity pass.** Clipboard sync, on-screen keyboard + external
   keyboard/trackpad support (iPad Magic Keyboard, Android
   Bluetooth/USB keyboards), multi-touch gestures (pinch zoom/pan,
   two-finger scroll, right-click via long-press or two-finger tap),
   saved server profiles, TLS/VeNCrypt auth flows.
6. **Performance.** Frame decode off main thread, texture upload path,
   adaptive quality on slow links.
7. **Polish/ship.** App icons, settings screens, error handling/reconnect,
   platform store listings.

## Open decisions

- **Licensing.** TigerVNC is GPLv2. Historically GPL clients have run into
  App Store friction (e.g. VLC's past removal). Need to confirm current
  Apple App Store GPLv2 stance before iOS distribution — may affect
  whether iOS ships via App Store, TestFlight-only, or sideloading, or
  whether a relicensed/reimplemented core is needed for that path.
  Android/Play Store has no such restriction.
- **Crypto library on mobile.** Resolved for both platforms (2026-09-10):
  cross-compile real gnutls+nettle from source rather than substituting a
  platform-native TLS/crypto lib -- see **iOS spike findings** and
  **Android spike findings** above for the dependency chain (identical on
  both) and why it wasn't optional. OpenSSL (available via NDK prebuilts)
  was a live alternative for Android specifically, but wasn't needed --
  the from-source cross-compile turned out to be no harder there than on
  iOS.
- **Rendering approach.** Software blit to a view vs. GPU-backed
  (Metal/Vulkan) surface — start with the simple path, revisit if
  performance requires it.
- **Submodule vs. vendored copy of TigerVNC.** Submodule tracks upstream
  fixes but couples us to their tree layout; a vendored copy is more
  flexible if we need to fork `common/` fixes. Resolved: the mobile
  project now lives inside the TigerVNC tree itself (`mobile/`), so
  `common/` fixes are ordinary commits.

## Remote audio extension findings (2026-09-27)

Full design on file at
`~/.claude/plans/for-the-current-project-rustling-church.md`.
Implements a new, TigerVNC-native RFB audio extension (NOT QEMU's --
that one's already vendored but only works against QEMU's own VM-console
VNC server, irrelevant to the standard desktop VNC servers this app
targets), negotiated the same spec-compliant way every optional RFB
feature is: an unrecognized pseudo-encoding is required to be silently
ignored, so an unmodified server keeps working exactly as before (no
audio, no breakage).

- **Wire protocol**: `pseudoEncodingTigerAudio = -262`
  (`vendor/tigervnc/common/rfb/encodings.h`, first clean slot after
  `pseudoEncodingLEDState = -261`); `msgTypeAudioStream = 153`
  (server→client begin/data/end) and `msgTypeAudioControl = 154`
  (client→server enable/disable), own namespace rather than QEMU's
  shared envelope message `255`; fixed v1 format S16LE / stereo /
  48000 Hz (no negotiation message). Tracked as
  `patches/vendor-tigervnc/0002-tigervnc-native-audio-extension.patch`
  (same mechanism as `0001-ios-nls-fixes.patch`); both
  `third_party/{ios,android}/build-core.sh` now apply every patch in
  that directory, not just one hardcoded file.
- **`core/` C ABI**: `audio_begin/audio_end/audio_data` callbacks and
  `vnc_client_set_audio_enabled()`, following the exact same
  "valid only for this call" and coalesced-command patterns
  `frame_update`/`requestResize` already establish. `VncClientImpl` now
  sets `supportsAudio = true` (was hardcoded `false`).
- **Verified end-to-end on Linux, host-side, no audio hardware needed**:
  extended `core/tests/test_server.c` (libvncserver-based mock) to
  hand-emit raw bytes for the new encoding/messages -- libvncserver has
  no native concept of them -- via a `newClientHook`-captured
  `rfbClientPtr` and `rfbWriteExact`. Discovered and fixed a real design
  gap in the process: the real client automatically replies to the
  capability announcement with `writeAudioControl(true)`, and
  libvncserver's own dispatch closes the connection on any message type
  it doesn't recognise (confirmed empirically: `rfbProcessClientNormalMessage:
  unknown message type 154` immediately followed by connection teardown).
  Fixed by registering a minimal `rfbProtocolExtension` (libvncserver's
  supported mechanism for exactly this) so the mock tolerates and logs
  the control byte instead of dying -- necessary for the *mock* only;
  a real unmodified server never sends the capability rect in the first
  place, so this failure mode can never occur against one. Extended
  `core/tests/smoke_test.cxx` with a byte-exact assertion (a deterministic
  square-wave test tone, `core/tests/audio_test_tone.h`, shared between
  mock and test so both sides generate identical bytes): `audio_begin`
  fires once, all 57600 accumulated `audio_data` bytes `memcmp`-match
  exactly, `audio_end` fires once, and the mute path
  (`vnc_client_set_audio_enabled`) is confirmed reaching the wire
  (`op=0`/`op=1` observed in the mock's log). All pre-existing smoke
  tests still pass unchanged -- regression evidence that advertising an
  encoding the mock doesn't understand is safely ignored.
- **Android**: `VncBridge.cpp` gets 3 new JNI trampolines (`onAudioData`
  copies into a `NewByteArray`, satisfying the "valid only for this
  call" contract) and `nativeSetAudioEnabled`. `VncBridge.kt` gets a
  genuinely separate path from the existing `mutableStateOf`/
  `mainHandler.post` last-value-wins pattern: a bounded
  `LinkedBlockingQueue` + dedicated thread feeding an `AudioTrack`
  (`MODE_STREAM`), since audio needs real ordered queueing, unlike video
  frames. `SessionScreen.kt` gets a mute button ("Au"/"Mu", matching the
  existing plain-glyph "Aa"/"1:1"/"×" style -- no emoji).
  **Verified by actually running it** on an API 36 emulator against the
  extended mock server (`10.0.2.2:15900`): connected, the negotiated
  `audioControl: op=1 (enable)` reply arrived at the mock automatically,
  tapping the mute button flipped the UI to "Mu" and the mock observed
  `op=0 (disable)` on the wire. **Found and fixed a real, pre-existing
  bug in the process** (not audio-specific): `MainActivity`'s
  `TigerVncApp()` keeps one `VncBridge` for the entire activity
  lifetime (`remember`), but `vnc_client_connect()` is documented as
  valid only once per `VncClient` instance -- reconnecting after a
  disconnect reused the same instance and reassigned a live
  `std::thread`, crashing with `std::terminate()` (confirmed via a
  5-cycle reconnect stress pass, the exact check this plan called for
  given the new audio thread lifecycle). Fixed in `VncBridge.kt`'s
  `connect()`: destroy and recreate the native handle on every connect
  call, not just once per `VncBridge` instance. Re-ran the same 5-cycle
  stress pass clean afterward, audio renegotiating correctly each time.
- **iOS**: `VncBridge.swift` gets the same 3 callbacks (deliberately
  *not* hopping to `DispatchQueue.main`, unlike every other callback in
  that file -- audio is a real ordered stream, not last-value-wins) plus
  `setAudioMuted()`. New `ios/App/AudioPlayer.swift`: `AVAudioEngine` +
  `AVAudioPlayerNode.scheduleBuffer` (which already queues/orders
  buffers internally -- no manual ring buffer needed, unlike Android's
  raw `AudioTrack.write()` loop), fed from its own serial
  `DispatchQueue`. `AVAudioSession` category `.playback` set on
  connect. `SessionView.swift` gets a `speaker.wave.2`/`speaker.slash`
  button. `ios/project.yml` links `AVFoundation`. **Written but not yet
  built or run -- needs the Mac** (same status as other recent iOS-side
  changes in this file); no reason to expect it not to work given how
  closely it mirrors the already-established callback/queue patterns,
  but unverified is unverified.
- **Scope boundary, superseded by the server-side work below**: this
  bullet originally said no production server implementation existed or
  was planned in this repo. That's no longer true -- see **Remote audio
  extension: server-side + real PulseAudio capture** below. **Still not
  done**: the one-time manual check that a real, *unmodified*
  `tigervncserver`/Xvnc build (not this repo's own new `x0vncserver`
  build) still behaves identically with this patched client -- still
  needed before considering the client-side extension itself shippable
  against arbitrary third-party servers.

## Remote audio extension: server-side + real PulseAudio capture (2026-09-27)

Followed up the client-only work above with a genuine, runnable server:
the protocol's server side (`common/rfb`) plus a real audio-capture
backend for `unix/x0vncserver` (TigerVNC's standalone userspace X11
server -- shares an existing desktop, no patched X server needed).
Tracked as a new patch, `patches/vendor-tigervnc/
0003-tigervnc-audio-server-and-capture.patch` (patch 0002 was also
regenerated in place to absorb 3 format-constant lines that belonged in
its own `audioTypes.h` addition but were only added afterward -- no
semantic change, confirmed via a full reverse/reapply round-trip before
replacing the committed file). Full per-class breakdown is in the
patch README's own `0003` section -- summary here:

- **New build**: `server/CMakeLists.txt` (repo root, new directory) --
  a minimal wrapper mirroring `core/CMakeLists.txt`'s symlink trick
  (`common`/`cmake`/`unix` symlinked from `vendor/tigervnc`) but for
  `unix/{x0vncserver,tx,common}`, generating a real `config.h` from
  the vendored `config.h.in` directly (`core/CMakeLists.txt` doesn't
  need this since it never got this deep into optional-feature
  `HAVE_*` macros). Configures and builds a real `x0vncserver` binary
  cleanly on this Linux machine (all needed X11 dev packages were
  already installed; `libpulse-dev` was not and the user installed it
  live mid-session). `unix/x0vncserver` turned out **not** separable
  the simple way `core/`'s client libs are -- it needs the full X11 +
  `HAVE_XDAMAGE`/`HAVE_XFIXES`/`HAVE_XRANDR`/`HAVE_XTEST` chain, unlike
  `common/{core,rdr,network,rfb}` which upstream already keeps
  desktop/server-dependency-free.
- **Server protocol** (`common/rfb`): mirrors an existing capability's
  pattern per class rather than inventing new shapes --
  `ClientParams::supportsTigerAudio()`, `SMsgHandler::audioControl()`,
  `SMsgReader::readAudioControl()`, `SMsgWriter::writeAudioBegin/Data/
  End()` + `writeTigerAudioSupport()`, `SConnection::setEncodings()`'s
  first-time-capability gate, `VNCSConnectionST::handleAudioControl()`
  + `writeAudio*OrClose()`, `VNCServerST::sendAudioBegin/Data/End()` +
  `isAudioStreaming()`.
- **Capture**: new `unix/x0vncserver/AudioCapturePulse.h/.cxx`, async
  PulseAudio recording from the default sink's *monitor* (system audio,
  not microphone), mirroring `vncviewer/AudioOutputPulse.cxx`'s
  `pa_threaded_mainloop` pattern in reverse. A new `AudioBridge` helper
  in `x0vncserver.cxx` hands captured data from PulseAudio's own thread
  to the server's single main thread via a mutex-guarded queue drained
  by a `core::Timer` (not a bare per-loop-iteration check -- `select()`
  can block indefinitely when idle with no client/timer activity, so
  only a registered timer guarantees the drain runs often enough).
- **Two real bugs found and fixed via actually running it against a
  real X11 desktop + PulseAudio/PipeWire sound server** (not just
  compiling): (1) the server never actually sent the capability
  announcement rect telling the client "I support this extension" --
  `writer()->writeTigerAudioSupport()` was missing from
  `SConnection::setEncodings()`'s first-time gate, so the client's
  already-correct auto-request logic from patch 0002 never fired
  (confirmed empirically: 0 bytes captured before the fix, real audio
  after). (2) A client that enabled audio *after* a stream was already
  flowing received `audio_data` with no matching `audio_begin`, since
  the begin/end bookkeeping was global to the capture backend, not
  per-connection -- fixed with `VNCServerST::isAudioStreaming()` +
  `VNCSConnectionST::handleAudioControl()` synthesizing an immediate
  begin for mid-stream joiners.
- **Verified end-to-end for real**, not just against a mock: ran the
  new `x0vncserver` bound to `-localhost` against this machine's actual
  live X11/PipeWire desktop session (`DISPLAY=:0`), connected a new
  manual verification client (`core/tests/audio_capture_manual_test.cxx`
  -- writes received audio to a `.wav` file, distinct from the
  automated byte-exact `smoke_test`), played a real audible test tone
  via `speaker-test`, and confirmed the captured `.wav` (768000 bytes,
  correct `audio_begin`×1/`audio_end`×1 bracketing) contained genuine
  signal -- amplitude range -26226..26225, 75% of samples above the
  silence threshold, not silence or garbage. Re-ran the existing
  automated `smoke_test` suite afterward (against the mock, unrelated
  to this server) to confirm no regression: still passes, byte-exact.
- **Scope, honestly stated**: `x0vncserver` is upstream TigerVNC's own
  standalone server, real and complete once built, but only this repo's
  fork of it (via patch 0003) understands the new extension --
  connecting to someone else's unpatched `x0vncserver`/`tigervncserver`
  still just works with no audio, per 0002's whole design point.
  PulseAudio-only (Linux); no Windows/macOS capture backend.

## Remote audio extension: Xvnc support (2026-09-27, same day)

User asked specifically about `Xvnc` (TigerVNC's own patched X *display*
server -- what `vncserver`/the packaged "TigerVNC server" actually
launches), a genuinely different, real X server binary from
`x0vncserver` (a standalone userspace program sharing an existing X11
session). Extended the server-side audio work to cover it too, tracked
as `patches/vendor-tigervnc/0004-xvnc-audio-capture.patch` plus a new
`patches/xorg-server-source/` directory (patches the real upstream X.Org
`xserver` source, a genuinely different upstream project from
`vendor/tigervnc`).

- **Much bigger build than `x0vncserver`**: Xvnc isn't CMake-based at
  all -- it's a full X.Org `xserver` autotools build with TigerVNC's VNC
  driver (`unix/xserver/hw/vnc`) patched in via upstream's own
  `unix/xserver21.patch`. The real X.Org source isn't vendored anywhere
  in this repo (per `vendor/tigervnc/BUILDING.txt`'s own documented
  recipe) -- had to fetch it separately. Got lucky: this machine's
  installed X server is exactly version 1.21.1.22, an exact match for
  Ubuntu's `xorg-server-source` apt package, so no cross-version
  guessing was needed. `sudo apt-get build-dep xserver-xorg-core` pulled
  the correct, curated full dependency chain (xorg-macros, autoconf/
  automake/libtool, every X11 protocol/library dev header) in one shot.
- **New `server/build-xvnc.sh`**: automates the whole recipe (extract →
  merge TigerVNC's `hw/vnc` with the real X.Org source → apply
  `xserver21.patch` then the new PulseAudio-detection patch → autoreconf
  → configure with Debian/Ubuntu-appropriate font/XKB paths → make).
  Verified by actually running it from a clean state, not just trusting
  the manual steps that preceded it.
- **Found a real cross-build-system gap along the way**: Xvnc's
  autotools build expects `TIGERVNC_BUILDDIR` (passed on the `make`
  command line) to be a directory containing both TigerVNC's own
  `config.h` *and* `.la` libtool-control-file shims for the
  CMake-built `common/{core,rdr,network,rfb}` static libraries --
  upstream's top-level `CMakeLists.txt` generates both via its own
  `libtool_generate_control_files()` call, but this repo's
  `server/CMakeLists.txt` (built for `x0vncserver`, not originally
  written with Xvnc in mind) never called it, so the `.la` files
  silently never existed. First build attempt failed with `config.h: No
  such file or directory`; tracing that back led to this. Fixed by
  adding the same `libtool_generate_control_files()` call to
  `server/CMakeLists.txt` -- it now does double duty, building
  `x0vncserver` directly *and* supplying what Xvnc's separate build
  needs (`server/build` is what `TIGERVNC_BUILDDIR` should point at).
- **`XserverDesktop.cc` integration**: reused the exact same
  `core::Timer`-based drain pattern as `x0vncserver.cxx`'s `AudioBridge`
  (mutex-guarded queue, drained on a timer, not a bare per-iteration
  check) -- confirmed this transplants correctly by finding
  `blockHandler()` (called by the X server's own event loop right
  before it blocks) already calls `core::Timer::checkTimeouts()`, same
  integration point. `AudioCapturePulse.{h,cxx}` is an intentional
  duplicate of the `x0vncserver` copy (see patch 0004's own note on why:
  autotools cross-directory source references are fragile without
  `subdir-objects`).
- **Verified end-to-end for real, twice**: once against the manually-built
  Xvnc, once against a from-scratch run of `server/build-xvnc.sh` (to
  make sure the script actually reproduces a working build, not just
  document what I did by hand). Both: real `Xvnc :99 -localhost
  -rfbport ...` instance, `core/tests/audio_capture_manual_test`
  connected, `speaker-test` playing an audible tone, captured `.wav`
  confirmed as genuine signal (identical amplitude stats to the
  `x0vncserver` test: -26226..26225 range, 75% non-silent), clean
  `audio_begin`×1/`audio_end`×1 bracketing. Re-ran the automated
  `smoke_test` suite afterward -- no regression.
- **Scope, same honesty as 0003**: only this repo's patched `Xvnc` (built
  via `server/build-xvnc.sh`) understands the extension; someone else's
  stock `Xvnc`/`tigervnc-standalone-server` package still just works
  with no audio. `server/build-xvnc/` is gitignored (regenerated from
  scratch each run, never tracked -- it's a large, X.Org-version-specific
  merged source tree, not something to commit).

## Opus audio compression (2026-09-29)

Raw S16LE stereo 48kHz PCM (the wire format from the audio work above)
is ~1.5Mbps -- too high for a mobile client on a real network. Decided
to switch the `msgTypeAudioStream` `data` payload to Opus, a codec
designed exactly for this (low-latency, low-bitrate, perceptually
tuned): chose a fixed ~64kbps target, ~24x smaller than raw PCM.

Scope for this pass (explicit, user-approved): **host/Linux only, but
verified fully** -- protocol constants, both server-side encoders, and
the shared client-side decoder. Android/iOS cross-compiling `libopus`
is a separate, deferred follow-up (not started).

- **`common/rfb/audioTypes.h`**: added `audioOpusFrameSamples = 960`
  (20ms @ 48000 Hz -- Opus requires a fixed frame size, and 20ms matches
  the existing PulseAudio capture chunk size already in use) and
  `audioOpusBitrate = 64000`. Updated the doc comment to state the wire
  payload is Opus-encoded, not raw PCM, with the bandwidth rationale.
  No FEC -- RFB already rides on reliable, ordered TCP, so packet loss
  isn't the concern it would be over UDP/RTP.
- **Encode side, both `AudioCapturePulse.{h,cxx}` copies** (identical
  logic in `unix/x0vncserver` and `unix/xserver/hw/vnc`, same
  can't-share-across-build-systems reason as before): each now owns an
  `OpusEncoder`, accumulates captured PCM into a `std::vector` buffer,
  and `encodeAvailableFrames()` drains it in fixed 960-sample frames --
  encoding (and forwarding to `onData()`) only while `streaming` is
  true, using the existing amplitude-threshold silence state machine
  unchanged in spirit (just now driven per-Opus-frame instead of
  per-PulseAudio-fragment). First draft had redundant/dead conditional
  branches; simplified once it was clear `streaming` alone was already
  the correct encode-and-forward gate.
- **Decode side, `core/src/VncClientImpl.{h,cxx}`** (shared by every
  platform, not per-platform code): added a lazily-created
  `OpusDecoder*`, and `handleAudioData()` now decodes each incoming
  packet back to PCM before calling `callbacks_.audio_data()`. This is
  the key design choice: Opus is purely a wire-format detail confined
  to the shared C++ core -- the C ABI's `audio_data` callback still
  delivers plain PCM exactly as before, so **Android, iOS, and every
  existing test tool needed zero code changes** for this pass.
- **Build systems**: `core/CMakeLists.txt`, `server/CMakeLists.txt`, and
  `unix/xserver/hw/vnc/Makefile.am` (via
  `patches/xorg-server-source/0002-opus-detection.patch`, mirroring
  0001's PulseAudio-detection patch) all gained an Opus dependency via
  `pkg_check_modules` (no CMake find-module for Opus, matching how Pulse
  was already handled). `core/CMakeLists.txt` makes Opus a hard
  `REQUIRED` dependency for *every* platform target (host/Android/iOS),
  which is a deliberate, flagged tradeoff: a fresh `core/build/android`
  or `core/build/ios` configure will now fail to configure until
  Android/iOS cross-compile `libopus` (separate follow-up, not started
  this pass) -- existing prebuilt static libs for those platforms,
  built before this change, are unaffected and still work.
- **Test harness updated to match** (`core/tests/`): `audio_test_tone.h`
  switched from a square wave to a 440Hz sine wave -- a square wave's
  sharp edges are an adversarial worst-case signal for a lossy
  perceptual codec, not representative of what's actually being tested,
  and the old integer-arithmetic trick for cross-language bit-exact
  reproduction was only needed for the byte-exact comparison this pass
  removes anyway. `test_server.c`'s mock server now Opus-encodes the
  tone frame-by-frame (15×960-sample frames) exactly like the real
  encoder, instead of sending one raw PCM blob. `smoke_test.cxx`'s
  audio assertion switched from an exact `memcmp()` to a single-bin
  Goertzel DFT magnitude comparison at 440Hz -- found empirically that
  a plain sample-domain diff is *not* usable post-Opus: Opus's encoder
  algorithmic look-ahead delay shifts the decoded signal by a few ms,
  which for a ~109-sample-period tone is enough phase misalignment to
  register as ~86% "error" even on a totally correct round trip. A
  Goertzel magnitude is phase-blind by construction, so it actually
  measures what matters (did the right frequency content survive) --
  passes with a 440Hz magnitude ratio of ~0.97 against the reference.
- **Real audible verification, not just the mock**: rebuilt `Xvnc`
  from scratch (`server/build-xvnc.sh`, picking up
  `0002-opus-detection.patch`) and `x0vncserver`
  (`server/CMakeLists.txt`), both clean. Played a genuine 440Hz tone
  through this machine's real PipeWire/PulseAudio-compatible sound
  server, captured it via `core/tests/audio_capture_manual_test`
  against both a running `x0vncserver` and a running `Xvnc :99`, and
  confirmed the decoded `.wav` in each case carries strong 440Hz energy
  (RMS ~6400-6550, dominant Goertzel bin at 440Hz) -- the full real
  chain (PulseAudio capture -> Opus encode -> RFB wire -> Opus decode)
  verified end-to-end on both server variants, not just protocol
  plumbing.
- **Patches regenerated**: `patches/vendor-tigervnc/0002-*.patch`
  (audioTypes.h constants), `0003-*.patch` (x0vncserver encoder), and
  `0004-*.patch` (Xvnc encoder) all re-diffed against the current
  working tree to capture these changes, then round-trip-verified by
  applying all four in order to a pristine `vendor/tigervnc` checkout
  (via a scratch `git worktree`) and confirming the result byte-for-byte
  matches the live submodule -- same verification discipline as every
  earlier patch in this series. `patches/xorg-server-source/README.md`
  and `patches/vendor-tigervnc/README.md` updated with the Opus
  additions to each existing section.

## w0vncserver audio capture (2026-09-29)

Extended the TigerVNC-native audio extension to the third and last real
server variant this repo builds: `w0vncserver`, TigerVNC's Wayland/
`xdg-desktop-portal` server -- a genuinely different desktop backend
from `x0vncserver` (direct X11) and `Xvnc` (its own patched X display
server): it captures screen content via PipeWire, negotiated through
the portal's `RemoteDesktop`/`ScreenCast` D-Bus interfaces (or directly
via wlroots protocols on a compositor that supports them).
`w0vncserver` wasn't built by this repo at all before this -- unlike
adding audio to an already-built server, this required standing up its
build for the first time too.

- **New host dependency**: `libpipewire-0.3-dev` was missing (everything
  else -- GLib/GIO/GObject/wayland-client/xkbcommon/uuid -- was already
  installed). Needed `sudo apt-get install libpipewire-0.3-dev`, done
  interactively by the user since this sandbox's `sudo` requires a
  password I can't supply. Confirmed via `pkg-config --exists
  libpipewire-0.3` before proceeding. No `libportal-dev` needed despite
  `PortalDesktop`'s name -- it talks to `xdg-desktop-portal` over plain
  GDBus (`gio/gio.h`), not `libportal`.
- **Build wiring, much simpler than Xvnc's (0004)**: `w0vncserver`
  already builds via CMake (same build system as `x0vncserver`), so
  unlike Xvnc's separate autotools/libtool integration, this only
  needed `find_package()` calls for GLib/Gio/Gobject/PipeWire/Uuid/
  WaylandClient/Xkbcommon in `server/CMakeLists.txt` (using the
  vendored `cmake/Modules/Find*.cmake` modules, the same ones
  upstream's own top-level `CMakeLists.txt` uses when `ENABLE_WAYLAND`
  is on) plus `add_subdirectory(unix/w0vncserver)`.
- **`AudioCapturePulse.{h,cxx}` reused directly, not duplicated a third
  time**: unlike Xvnc's copy (a real duplication, needed because that's
  a separate autotools build where cross-directory source references
  are fragile without `subdir-objects`), `w0vncserver` and
  `x0vncserver` are both plain CMake targets, where a relative-path
  source (`../x0vncserver/AudioCapturePulse.cxx`) works cleanly -- each
  target gets its own object file, no collision. One fewer copy to keep
  in sync going forward.
- **`AudioBridge` class, functionally identical to x0vncserver's own
  copy** (a `core::Timer::Callback` draining a mutex-guarded queue onto
  `VNCServerST::sendAudioBegin/Data/End()`), duplicated in
  `w0vncserver.cxx` rather than shared -- consistent with it already
  being a small, per-file glue class in both other integrations, not
  shared infrastructure. The one real difference -- this file's main
  loop is GLib-driven, not a bare `select()` loop -- needed **zero
  extra plumbing**: `RFBTimerSource` (already constructed in `main()`
  for unrelated reasons) already bridges `core::Timer::checkTimeouts()`
  into every GLib main-loop iteration for *every* registered
  `core::Timer`, so constructing an `AudioBridge` (which starts its own
  20ms timer) was enough for it to start draining automatically, no
  w0vncserver-specific integration code needed.
- **Verified partially, and documented exactly why**: rebuilt
  `server/build` clean, confirmed `x0vncserver` still builds unaffected
  (regression check), started `w0vncserver`, and confirmed
  `AudioCapturePulse::isAvailable()` succeeds against this machine's
  real PulseAudio-compatible sound server -- the same capture backend
  already proven end-to-end on `x0vncserver`/`Xvnc`. Could not complete
  a full session-level real-audio round trip like those two got,
  though: `w0vncserver`'s `PortalDesktop` backend requires an
  interactive `org.freedesktop.portal.RemoteDesktop` consent before
  `SConnection` ever reaches `RFBSTATE_NORMAL` (gating *all* protocol
  traffic, not just audio -- confirmed via `SConnection::processMsg`'s
  `RFBSTATE_CLIENT_READY` state), and in this environment GNOME's
  portal implementation declines the session outright
  (`journalctl --user -u xdg-desktop-portal-gnome`: `Failed to create
  remote desktop session: ... Session creation inhibited`). That's a
  machine-local Mutter policy, not a bug in this change, and not
  something to flip unilaterally -- it governs this real machine's
  actual screen-recording/remote-control permissions. Same category of
  gap as this project's existing "iOS unverified, needs the Mac" notes:
  real end-to-end verification needs a Wayland session that will
  actually grant the portal request (or a wlroots compositor, for the
  `WaylandDesktop` fallback path instead), deferred until one's
  available.
- **Patch**: `patches/vendor-tigervnc/0005-w0vncserver-audio-capture.patch`
  (new), round-trip-verified by applying all five vendor patches in
  order to a pristine `vendor/tigervnc` checkout and confirming the
  result matches the live submodule exactly, same discipline as every
  earlier patch in this series. `patches/vendor-tigervnc/README.md`
  updated with a new section.

### Follow-up: full real-audio verification (2026-10-03)

Closed the one gap left open above -- root-caused *why* the portal
declined the session, then verified the actual audio code via the
other, portal-free backend instead of fighting the portal further.

- **Root cause found**: `gsettings get org.gnome.desktop.remote-desktop.rdp
  enable` and the `.vnc` equivalent were both `false` on this machine --
  GNOME's own remote-desktop/screen-sharing master switch is off
  system-wide, which is almost certainly what gates Mutter's
  `RemoteDesktop` portal implementation (it backs both GNOME's own
  built-in sharing *and* third-party portal requests like
  `w0vncserver`'s). Flipping that is a real, security-relevant change
  to this machine's actual remote-access posture, so it wasn't done
  without asking -- offered the user a choice between flipping it
  (temporarily) and testing the *other* code path instead.
- **User chose the other path**: install `sway` (a wlroots compositor)
  and exercise `WaylandDesktop`, which needs no portal consent at all --
  it talks directly to wlr-screencopy/wlr-virtual-pointer/virtual-
  keyboard protocols, privileges any client connecting to that
  compositor's own socket already has. This also closes a second,
  previously-untouched gap: `WaylandDesktop` had never been exercised
  by any test, portal or not.
- **Getting a clean nested compositor took two tries**: the first
  `sway` launch picked up the user's real (leftover) `~/.config/i3/config`
  and started spawning real session apps (tray icons, nm-applet, etc.)
  nested inside itself -- not wanted for a clean test, and a reminder
  that `sway`/`i3` share a config-search-path convention. Fixed with an
  explicit minimal `-c` config (`bar { swaybar_command true }` to fully
  suppress the default bar/tray, not just hide it).
- **First connection attempt still silently used the portal anyway**,
  not `WaylandDesktop`: `PortalDesktop::available()` only checks
  whether `org.freedesktop.portal.{RemoteDesktop,ScreenCast}` exist on
  D-Bus -- a global check, completely independent of which
  `WAYLAND_DISPLAY` the process is pointed at. Tried isolating it via
  `dbus-run-session` (a private, empty D-Bus session bus) next, but
  that backfired differently: D-Bus service-activation spun up a
  *fresh*, parallel `xdg-desktop-portal`/`xdg-desktop-portal-gnome`
  instance on the private bus (visible in its logs: "Failed to open
  service channel Wayland connection... org.gnome.Mutter.ServiceChannel
  without an owner") -- a disconnected parallel instance that can't
  reach the real Mutter compositor's remote-desktop service at all, not
  a clean "no portal" test. (One real risk from this detour: a
  broad-pattern `pkill -f xdg-desktop-portal-gnome` could have killed
  the user's *real*, systemd-managed portal instance, not just the
  private-bus one, since both have identical command lines. Checked
  `systemctl --user status` immediately after -- the real services'
  uptimes were untouched, confirming only the private-bus instances
  died. Worth remembering: prefer PID-scoped kills over broad `pkill -f`
  patterns when two processes can share a command line.)
- **Settled on a one-line, reversible, uncommitted source change**
  instead: gated the `PortalDesktop::available()` branch in
  `w0vncserver.cxx`'s `main()` behind
  `getenv("TIGERVNC_FORCE_WAYLAND_DESKTOP") == nullptr`, for this one
  local test build only. Rebuilt, ran `WAYLAND_DISPLAY=<sway's socket>
  TIGERVNC_FORCE_WAYLAND_DESKTOP=1 w0vncserver`, confirmed it actually
  took the `WaylandDesktop` branch this time.
- **Full real round trip confirmed**: played a 440Hz tone through
  PipeWire, captured via `core/tests/audio_capture_manual_test` against
  the sway-backed `w0vncserver` -- real video was captured too (server
  log: 17 rects, Tight-encoded, genuine screen content from the nested
  session, "Clean disconnection", no errors), and the decoded audio
  measured RMS 8436 against a theoretical max of ~8485 for this tone --
  excellent fidelity, on par with or better than the earlier
  x0vncserver/Xvnc passes.
- **Reverted the test-only change** immediately after: removed the
  `getenv(...)` gate, confirmed via `git apply --check --reverse` against
  `patches/vendor-tigervnc/0005-w0vncserver-audio-capture.patch` that
  the file exactly matches the already-committed patch state again (exit
  0), and rebuilt clean. Nothing from this follow-up needed a new commit
  -- it only *exercised* already-committed code via an already-existing,
  portal-independent fallback path; `PortalDesktop` itself remains
  unverified at the session level (that gap is specifically about
  Mutter's portal declining consent, not about this project's own code).

## Android Opus cross-compile + real device verification (2026-09-29)

Closed the gap the Opus pass above deliberately deferred: Android's
`core/build/android` couldn't configure at all once `core/CMakeLists.txt`
started hard-requiring Opus, since `third_party/android/sysroot` never
had it cross-compiled. This is the first pass where an actual mobile
client (not a host-side test tool) plays real remote audio.

- **`third_party/android/build-deps.sh`**: added an `opus` build step,
  same CMake-via-the-shared-toolchain-file pattern already used for
  `libjpeg-turbo` (simpler than the autotools dependencies -- libopus
  ships its own `CMakeLists.txt` since ~1.3, and its defaults are
  already what's wanted: static lib, no shared lib, no test/program
  targets, pkg-config module installed). Installs `libopus.a` +
  `opus.pc` into the existing `sysroot`.
- **Verified the pkg-config cross-resolution risk directly**, since
  `core/CMakeLists.txt` finds Opus via `pkg_check_modules`, not a CMake
  find-module, and this host also has its own `libopus-dev` installed
  (from the server-side Opus work) -- a real risk of silently resolving
  the wrong (host, x86_64) `opus.pc` instead of the cross one during
  Android configure. Reconfigured `core/build/android` with
  `PKG_CONFIG_LIBDIR` pointed only at
  `third_party/android/sysroot/lib/pkgconfig` and `PKG_CONFIG_PATH`/
  `PKG_CONFIG_SYSROOT_DIR` cleared (the same safeguard
  `third_party/android/build-core.sh`'s older phase-1-spike script
  already used, just not yet applied to `core/CMakeLists.txt`'s own
  direct-configure path documented in `android/app/src/main/cpp/
  CMakeLists.txt`'s error message) -- confirmed via `CMakeCache.txt`
  that `OPUS_INCLUDE_DIRS`/`pkgcfg_lib_OPUS_opus` both resolved under
  `third_party/android/sysroot`, not `/usr`.
- **`core/build/android` rebuilt clean** -- `libtigervnc_core.a` links
  the cross-compiled `libopus.a` (a STATIC library, so this alone
  wouldn't have caught a wrong include/lib path -- `ar` doesn't need
  real symbol resolution the way a final link does; the real proof
  came from the JNI module linking successfully next).
- **`android/app/src/main/cpp/CMakeLists.txt`**: added `opus` to
  `target_link_libraries(vncjni ...)`, alongside the other prebuilt
  static deps (gnutls/hogweed/nettle/gmp/tasn1/pixman-1/jpeg) --
  `libvncjni.so` links clean, confirming the whole cross-compiled Opus
  chain is actually correct end to end, not just "configure succeeded."
- **Verified for real on a running Android emulator**, not just a
  clean build: installed the rebuilt debug APK, connected to a real,
  unmodified `x0vncserver` on this host (real captured system audio,
  Opus-encoded -- same binary already proven working in the earlier
  Opus pass), and confirmed via `adb logcat`:
  - `AudioTrack: createTrack_l` fires exactly when a real tone starts
    playing on the host (i.e. `audio_begin` -> JNI `OnAudioBegin` ->
    `VncBridge.kt`'s playback thread lazily creating the track --
    matches the design in the original client-side audio plan).
  - The track cleanly stops (`AudioFlinger: ... BUFFER TIMEOUT: remove
    track ... due to underrun`, not a crash) once the tone -- and the
    server's `audio_end` -- stops, matching the tone's actual duration.
  - Tapping the mute icon suppresses all `AudioTrack`/`AudioFlinger`
    activity entirely while a tone plays (confirming
    `vnc_client_set_audio_enabled(0)` actually reaches the server and
    stops the stream, not just a local no-op); tapping it again resumes
    playback on the *same* track (`AudioTrack: restartIfDisabled(...)
    ... restarting`), confirming the mute/unmute round trip works both
    directions.
  - No crash, on either the app or `x0vncserver`'s side, throughout.
  - Hit one unrelated pre-existing issue along the way: the app's
    connect flow triggers an `RRSetScreenSize` X request that crashed
    `x0vncserver` against this sandbox's virtual X display (`BadMatch`)
    on the first attempt -- nothing to do with audio (same crash
    would happen with audio-free builds too); worked around for this
    test with `-AcceptSetDesktopSize=0`, not something this pass fixed.
- This is the first pass in the project where a real mobile client
  (not `core/tests`' host-side C++ tools) plays real remote audio
  end-to-end against a real, unmodified TigerVNC-native-audio-patched
  server.
- iOS is still unverified/undone for Opus -- same `libopus`
  cross-compile gap, deferred (needs a Mac to build/verify, consistent
  with every other iOS gap already tracked in this file).

## vncviewer audio decode (2026-10-03)

User asked for a Linux desktop client with audio support, and clarified
when asked about scope: not a new client -- vendor/tigervnc already
ships one (`vncviewer`, the real FLTK desktop client), the question was
whether to add audio support *there*. That reframing was the right
call: this repo never otherwise maintains its own separate client
implementation (it patches TigerVNC's own server binaries instead of
writing new ones), so patching `vncviewer` is consistent with the whole
project's approach, not an exception to it.

- **Most of the plumbing already existed, for free**: `vncviewer`'s
  `CConn` class already had `handleAudioBegin/End/Data()` overrides
  wired to vendor's own `AudioOutputPulse` (real-time PulseAudio
  playback, originally built for the pre-existing QEMU audio
  extension). Since the TigerVNC-native extension shares those exact
  same `CMsgHandler` callbacks by design (see 0002's own note), and
  `supportsAudio`/negotiation already live in the shared
  `CConnection` base class, `vncviewer` was already requesting and
  receiving this extension's audio with zero code changes needed --
  confirmed in its own log: `CConnection: Requesting audio (format 3, 2
  channels, 48000 Hz)`, on the very first connection attempt, before
  any patch was even applied for this pass.
- **The actual gap: Opus**. `AudioOutputPulse::play()` expects raw PCM;
  the TigerVNC-native extension's payload has been Opus-encoded since
  the earlier "Opus audio compression" pass. Added an `OpusDecoder` to
  `CConn` (same lazy-creation lifecycle as `core/src/VncClientImpl`'s
  identical field) and decode in `handleAudioData()` before calling
  `play()`.
- **Found and fixed a real correctness bug before it shipped, not
  after**: my first draft decoded unconditionally, which would have
  silently broken `vncviewer`'s *existing*, working QEMU audio support
  -- QEMU's extension is still fully live in this same codebase
  (unrelated to this project) and sends raw PCM through this identical
  `handleAudioData()` entry point. Caught by actually reading
  `CConnection.cxx`'s negotiation logic (`framebufferUpdateEnd()`) before trusting an
  assumption: it only ever requests one extension or the other per
  connection, preferring the native one when both are offered, so
  `server.supportsTigerAudio` reliably tells `handleAudioData()` which
  wire format this particular connection's audio actually is. Gated the
  decode on that flag -- QEMU audio now passes through unchanged, same
  as before this patch.
- **Stood up `vncviewer`'s build in this repo for the first time**,
  same pattern as `w0vncserver` before it: extended
  `server/CMakeLists.txt` (symlinking `vncviewer/`, `po/`, and `media/`
  from the submodule, mirroring the existing `common`/`cmake`/`unix`
  symlinks) rather than using vendor's own top-level `CMakeLists.txt`
  (same "pulls in concerns this doesn't need" reasoning as always).
  Needed one new package (`libfltk1.3-dev`; everything else FLTK's X11
  build needs -- Xft/Xinerama/Xcursor/Xrender/fontconfig/Xi/Xrandr --
  was already installed) and one incidental fix along the way:
  `vncviewer.cxx`'s app-icon lookup referenced
  `CMAKE_INSTALL_FULL_DATADIR`, normally supplied by `GNUInstallDirs`
  via vendor's top-level file; defined it as an empty string, same
  precedent as the existing `CMAKE_INSTALL_FULL_LOCALEDIR` definition
  (the icon's `stat()` check just fails gracefully, harmless for an
  uninstalled dev build).
- **Verified for real, not just a clean build**: connected a real,
  running `vncviewer` window to a real `x0vncserver`, played a genuine
  440Hz tone through this machine's real PipeWire/PulseAudio-compatible
  sound server, and confirmed via `wpctl status` that both sides of the
  round trip were simultaneously `[active]`: `x0vncserver`'s own
  `AudioCapturePulse` (capturing the monitor source) and `vncviewer`'s
  own `AudioOutputPulse` (playing to the real output device) -- two
  separate, simultaneously-active PulseAudio streams, both named
  "TigerVNC", confirming genuine live audible playback through the new
  decode path. No crashes on either side. (Hit the same unrelated,
  pre-existing `x0vncserver` remote-resize-on-connect crash found
  testing Android earlier -- `RRSetScreenSize` `BadMatch` against this
  sandbox's virtual X display -- worked around with
  `-AcceptSetDesktopSize=0`, not something this patch touches.)
- **Patch**: `patches/vendor-tigervnc/0006-vncviewer-audio-decode.patch`
  (new), round-trip-verified by applying all six vendor patches in
  order to a pristine `vendor/tigervnc` checkout and confirming the
  result matches the live submodule exactly, same discipline as every
  earlier patch in this series.
- **Known, deliberately out-of-scope limitation**: the Opus code in
  `CConn.h`/`CConn.cxx` isn't gated behind `HAVE_AUDIO`/platform checks
  the way `AudioOutput.h`'s own backends are (Windows uses
  `AudioOutputWin32`, doesn't need Opus at all). Costs nothing in
  practice since this repo only ever builds `vncviewer` for Linux, but
  a genuinely upstream-quality version of this patch would need that
  same `#ifdef` discipline, and Windows/macOS would need their own
  separate Opus-decode wiring for their own `AudioOutput` backends.

## Build scripts (2026-10-04)

Everything up to this point in the audio work was built by hand-running
CMake/Gradle/xcodebuild commands directly, one at a time, as each piece
was developed. Added one-shot scripts per platform so building this
project's binaries (not just the ones involved in the audio extension
-- the whole real build) doesn't require reconstructing those command
sequences from memory or from `PLAN.md`'s own prose.

- **`server/install-deps.sh`** (new): `apt install`s everything
  `server/build-linux.sh` needs, grouped and commented by which binary
  needs which packages (base/`core`-shared deps, X11, audio, w0vncserver
  -specific, vncviewer-specific) -- same package list `server/
  CMakeLists.txt`'s own top comment already documented piecemeal across
  several commits, now runnable instead of just readable. Idempotent
  (plain `apt-get install`, no custom "already installed" checks
  needed).
- **`server/build-linux.sh`** (new): applies vendor patches (same
  defensive check-then-apply loop `third_party/{android,ios}/
  build-core.sh` already used, needed for a fresh clone), then
  configures and builds `server/CMakeLists.txt` -- `x0vncserver`,
  `w0vncserver`, and `vncviewer` in one pass. Deliberately matches the
  existing `server/build` directory's generator ("Unix Makefiles", the
  CMake default) rather than forcing `-G Ninja`, which would have
  conflicted with re-running it against this repo's own already-
  configured build directory (CMake refuses to switch generators
  in-place). Does not build Xvnc -- that stays `server/build-xvnc.sh`'s
  own, much bigger, separate job.
- **`android/build-android.sh`** (new): the real, complete pipeline --
  `third_party/android/build-deps.sh` (cross-compiled deps incl.
  libopus), apply vendor patches, configure+build `core/` directly
  against `core/CMakeLists.txt` (not the old phase-1
  `third_party/android/build-core.sh` spike, which only builds a
  minimal `common/` sanity-check shim, not the real `tigervnc_core`
  wrapper the app actually links), then `./gradlew assembleDebug`
  (or `--release`). Carries forward the exact `PKG_CONFIG_LIBDIR`/
  `PKG_CONFIG_PATH`/`PKG_CONFIG_SYSROOT_DIR` isolation found necessary
  during the original Android Opus pass (a host machine with its own
  `libopus-dev` installed -- this one has one, from the Linux server
  work -- can otherwise have `pkg_check_modules` silently resolve the
  wrong, host x86_64 `opus.pc`).
- **`ios/build-ios.sh`** (new), same shape as the Android script but
  for both the device and Simulator sysroots/toolchains, finishing with
  `xcodegen generate` + `xcodebuild`. Defaults to a Simulator build
  (needs no code-signing/provisioning setup to actually run); `--device`
  builds for a physical device instead. **Closed iOS's own Opus
  cross-compile gap as part of writing this** -- unlike Android, nobody
  had added an `opus` build step to `third_party/ios/build-deps.sh`/
  `build-deps-sim.sh` yet, or `-lopus` to `ios/project.yml`'s
  `OTHER_LDFLAGS` -- both added now, mirroring the already-proven
  Android pattern (libopus ships its own CMakeLists.txt, same
  toolchain-file-driven `cmake`/`cmake --build`/`cmake --install`
  shape already used for `libjpeg-turbo` in both of those scripts).
- **Verification is asymmetric, and that's recorded honestly**:
  `server/install-deps.sh` + `build-linux.sh` and `android/
  build-android.sh` were actually run end-to-end in this environment
  and work (confirmed by building real binaries/a real APK from a
  mix of fresh and cached state). `ios/build-ios.sh` could not be run
  at all -- this repo's own environment is Linux-only, no Xcode/
  Simulator/physical device available -- so it's written to mirror the
  same proven patterns as closely as possible, syntax-checked
  (`bash -n`), but is unverified, same standing caveat as every other
  iOS deliverable in this project.
- **`server/build-xvnc.sh` had the same gap, found by being asked
  directly whether it already handled this**: it copies
  `unix/xserver/hw/vnc` straight from the `vendor/tigervnc` submodule
  (that's where `0004-xvnc-audio-capture.patch`'s `AudioCapturePulse.cxx`
  lives), but never applied the vendor patches itself -- it only ever
  applied `xserver21.patch` and `patches/xorg-server-source/*.patch`
  (both against the *separately fetched* X.Org source tree, via plain
  `patch -p1`, not `git apply`, since that tree isn't a git repo). On a
  fresh clone, running `build-xvnc.sh` before ever running
  `build-linux.sh` would have silently produced an audio-less `Xvnc`
  instead of failing loudly. Added the same defensive
  `git apply --reverse --check` loop every other build script uses, and
  verified it for real by actually running the script afterward --
  confirmed the new step correctly detects "already applied" (no-ops
  cleanly) and the rest of the build proceeds exactly as it did before
  this change.
- **`BUILD.md`** (new, linked from `README.md`): consolidates build
  instructions for every platform in one place -- a table of which
  script builds what and whether it's actually been verified, plus a
  "common gotchas" section capturing the real failure modes found
  while writing these scripts (patches not applied, the cross-compile
  `pkg-config`/Opus mis-resolution risk, the submodule always showing
  dirty by design).

## AudioCapturePulse reconnect: automatic + manual retry (2026-10-06)

Direct follow-up to `server/setup-audio.sh`/`BUILD.md`'s Audio setup
section: user asked specifically what happens if PulseAudio gets
(re)started *after* the VNC server already did (a real scenario they
hit deploying `Xvnc` on a fresh VM over SSH). Answer at the time: you
had to restart the whole server, since `AudioCapturePulse` only ever
attempted the PulseAudio connection once, at construction, with no
retry at all. Closed that gap with two complementary mechanisms.

- **Automatic background retry**: `AudioCapturePulse` (both copies,
  `unix/x0vncserver/` and `unix/xserver/hw/vnc/` -- identical change,
  kept in sync as always) now owns a `core::Timer` and retries
  connecting every `audioCaptureRetryMs` (a fixed 5000ms constant, not
  configurable -- matches every other tuning constant in this
  extension) for as long as it stays unavailable. Required real
  surgery to `connect()`, not just adding a timer: the original
  function was only ever called once per object lifetime and never
  cleaned up its own partial state on failure (a failed attempt left
  `mainloop`/`context` allocated and running, `timedOut` stuck `true`).
  Factored that cleanup out of the destructor into a new `teardown()`,
  called both by the destructor and at the top of `connect()` itself --
  so every retry attempt, not just the final one, starts from a clean
  slate. Without the `timedOut` reset specifically, every retry after
  the first would have bailed out of its wait loop instantly instead of
  actually waiting up to `connectTimeoutMs`.
- **Manual retry on client enable**: added `VNCServerST::
  setAudioRetryCallback()`/`requestAudioRetry()` -- a small new channel
  in the shared `common/rfb` server classes, notable because it's the
  *first* one in this whole extension that goes from the shared code
  back out to the platform-specific capture backend, rather than the
  established direction (capture backend → `sendAudioBegin/Data/End()`
  → shared code → clients). `VNCSConnectionST::handleAudioControl()`
  now calls it unconditionally whenever a client enables audio;
  `AudioCapturePulse::retryNow()` on the other end is a cheap no-op if
  already connected, or an immediate reconnect attempt if not. Net
  effect: toggling mute off then on (Android/iOS's existing button --
  `vncviewer` deliberately excluded here, an explicit scope decision,
  no live mid-session mute toggle exists there to hang this off of
  anyway) is a free "try again right now" trigger, on top of whatever
  the periodic timer would eventually do on its own. Wired into all
  three servers' own `main()`/constructor (`x0vncserver.cxx`,
  `w0vncserver.cxx`, `XserverDesktop.cc` -- the last one via the same
  `static_cast<rfb::VNCServerST*>(server)` pattern its existing audio-
  drain code already uses, since `server` is declared as the abstract
  `rfb::VNCServer*` interface type there).
- **Added a real, independently-useful log line while in here**:
  `Sound server connection established` on a successful (re)connect --
  the original code had a log line for every failure mode but none at
  all for success, making it impossible to confirm from the log alone
  whether a reconnect (automatic or manual) ever actually worked.
- **Verified both paths for real, and found + ruled out a false alarm
  along the way**: redirected `AudioCapturePulse`'s PulseAudio discovery
  to an isolated fake `XDG_RUNTIME_DIR` (initially pointed at an empty
  directory, later symlinked to the real socket) to simulate "PulseAudio
  starts after the server" without touching this machine's actual
  desktop audio session at all. First pass looked alarming: retries
  appeared to stop firing after exactly one attempt. Added temporary
  `fprintf` tracing to investigate -- which made the problem
  "disappear," a classic sign of a test-harness artifact rather than a
  real bug. Confirmed via `wpctl status` that the capture stream was
  genuinely `[active]` the whole time regardless, and via `stdbuf -oL
  -eL` that the log output had simply been sitting in a full (not
  line-)buffer the entire time -- a background process's output
  redirected to a file uses full buffering by default, not line
  buffering, so `vlog`'s own output just wasn't reaching the file
  promptly. Not a code bug at all. With that sorted, confirmed cleanly:
  the periodic timer fires every 5000ms while unavailable and
  reconnects automatically once PulseAudio comes up; separately
  confirmed the manual path by timing a client connection to land
  *between* two periodic retries and observing the reconnect go
  straight through `attemptConnect()` (not `handleTimeout()`, visible
  in a debug trace) and succeed immediately, rather than waiting for
  the next scheduled attempt.
- **Patches regenerated**: `0003`, `0004`, and `0005` (the three
  touched by this change), round-trip-verified by applying all six
  vendor patches in order to a pristine `vendor/tigervnc` checkout and
  confirming the result matches the live submodule exactly, same
  discipline as every earlier patch in this series. All three real
  servers (`x0vncserver`, `w0vncserver`, `Xvnc`) and `vncviewer`
  rebuilt clean from scratch afterward.

## Code review follow-up: two real fixes (2026-10-08)

A pasted code review of the audio extension listed six issues; four
were already-defensible or genuinely harmless by the review's own
characterization (asymmetric fail-open/fail-closed unknown-op
handling, build-time-hard-require vs. runtime-graceful-degradation
differing by design between Xvnc's `configure.ac` and everything else,
a harmless missing `length == 0` check, and a plain observation that
`vncviewer` never sends `audioControl(false)` in v1). Two were real:

- `vncviewer/CConn.cxx::handleAudioData()` picked Opus-decode vs.
  raw-PCM based on `server.supportsTigerAudio` -- what the server
  *advertised*, not which wire format is actually arriving -- since
  both the QEMU and native-extension paths funnel into the same
  `audioBegin/End/Data()` surface by design. A server advertising both
  extensions but sending the "wrong" one would get silently
  mis-decoded. Not a new attack surface and no real server exists yet
  to trigger it, but fixed properly: added `CConnection::
  audioFormatIsNative`, set only in `requestTigerAudio()` (what the
  *client* actually asked for), and switched the decode branch to that.
  `VncClientImpl` (mobile) was never affected -- it always assumes Opus
  and never implements the QEMU path at all.
- `AudioCapturePulse::retryNow()` (see entry above) blocks the caller
  for up to `connectTimeoutMs` (500ms) -- the whole X server, in
  Xvnc's case -- and is wired to fire on every client audio-enable, so
  a client rapidly toggling mute could force repeated back-to-back
  stalls. Added a 1-second debounce (`manualRetryMinIntervalMs`) so a
  retry attempt can't be forced more often than once a second,
  regardless of how fast the client toggles.

Verified via `core/tests/smoke_test` (still passes, including the
audio round-trip) plus a clean rebuild of all four binaries. Patches
`0002`, `0003`, `0004`, `0006` regenerated and round-trip-verified.

**Follow-up (same day)**: a second look caught that the
`audioFormatIsNative` fix above only landed on `vncviewer`'s side --
`core/src/VncClientImpl.cxx::handleAudioData()` (Android/iOS) had the
exact same bug, just more consequential: `tigervnc_core.h`'s own doc
comment explicitly promises a QEMU-audio fallback (and
`sendAudioControl()` genuinely implements the outgoing half of it,
branching on which extension to send), but `handleAudioData()`
unconditionally Opus-decoded every incoming chunk with no raw-PCM
branch at all -- against a real QEMU-audio server, raw PCM fed into
`opus_decode` would reliably fail to decode (`decoded < 0`) and get
silently dropped, so the documented fallback would have produced total
silence, not degraded-but-working audio. `core/` isn't vendored (not
under a patch), so this is a plain source fix, not a patch
regeneration: added the same `audioFormatIsNative` branch, verified via
`core/tests/smoke_test` (native/Opus path unaffected; no QEMU-audio
mock exists in this suite to exercise the other branch directly, so
that side is verified by code inspection + mirroring the already-tested
`vncviewer` logic exactly, not by a fresh end-to-end run).

## Fork sync: optional audio build + supportsNativeAudio (2026-10-20)

The user integrated this extension into their own TigerVNC fork
(`../tigervnc`, branch `audio-extension-fltk-1.4`). The audio commit is
`b0ccb88a`, on top of an FLTK 1.4 commit and an upstream base that is 42
commits newer than our `vendor/tigervnc` pin. The `common/rfb` protocol
files and both `AudioCapturePulse` copies are byte-identical to our
patches. The fork's real changes are:

- **`CConnection::supportsNativeAudio`** (new, default false). Only when
  it is set does `updateEncodings()` advertise `pseudoEncodingTigerAudio`,
  and only then does `framebufferUpdateEnd()` pick `requestTigerAudio()`.
  `vncviewer` sets it only `#ifdef HAVE_OPUS`. This makes sense: an
  Opus-less viewer used to advertise the extension anyway and then
  couldn't decode what it received.
- **Optional audio build.** The top-level `CMakeLists.txt` finds Opus
  under `HAVE_AUDIO` and defines `HAVE_OPUS`, plus `HAVE_AUDIO_CAPTURE`
  when Pulse is also found (both go into `config.h.in`). The x0/w0
  server sources, their `CMakeLists.txt`, and `vncviewer`'s Opus code are
  all `#ifdef`/`if()` gated on these. For Xvnc, `unix/xserver{21,120}.patch`
  gains an `--enable-vnc-audio[=auto]` configure option and an
  `AM_CONDITIONAL(VNC_AUDIO)`, which `hw/vnc/Makefile.am` uses.
- `vncviewer/CConn.h` forward-declares `struct OpusDecoder` instead of
  including `<opus/opus.h>`.
- Also in that commit, but not ported here: `AUDIO.md` and
  `scripts/build-{server,viewer}.sh`/`common.sh`, which are fork-specific
  build docs and scripts (this repo has `BUILD.md` and `server/*.sh`).
  The FLTK 1.4 commit (`f49eca9f`) was not ported either.

Two problems surfaced when porting it here, and both are fixed in our
patches:

1. **The mobile client would have lost audio.** `VncClientImpl` only set
   `supportsAudio`, so with the new default it would have stopped
   advertising the native extension. It would then have fallen back to
   QEMU audio, which none of our servers speak. Fixed by setting
   `supportsNativeAudio = true` in `VncClientImpl` (core always links
   Opus). This is the one change outside `vendor/` the user's fork
   couldn't have caught, because the fork doesn't contain `core/`.
2. **Xvnc's `HAVE_AUDIO_CAPTURE` came from two sources.** `Makefile.am`
   passed `-DHAVE_AUDIO_CAPTURE`, but `XserverDesktop.cc` also includes
   the CMake build's `config.h` (via `-I$(TIGERVNC_BUILDDIR)`), which
   defines the same macro whenever CMake found Pulse+Opus. That causes a
   macro-redefinition warning (`1` vs empty). Worse, with CMake on but
   `--disable-vnc-audio`, `XserverDesktop` compiles its capture code
   while `AudioCapturePulse.cxx` is never built, so the link fails.
   Fixed by giving Xvnc its own `VNC_AUDIO_CAPTURE` macro, set only by
   `Makefile.am`. Worth porting back to the fork.

Smaller observation, not changed: the fork's top-level CMake nests Opus
detection, and so the servers' capture, under `HAVE_AUDIO`, which is
documented as "audio playback in the viewer". So `-DENABLE_AUDIO=OFF`
also disables server capture. That's harmless on Linux, where both mean
"Pulse found", but the option name now covers more than it says.

Repo-side changes:

- `server/CMakeLists.txt` sets `HAVE_OPUS`/`HAVE_AUDIO_CAPTURE` to 1. It
  already hard-requires Pulse+Opus, and without these the new gating
  would have silently built audio-less servers and viewer.
- `server/build-xvnc.sh` passes `--enable-vnc-audio`, which keeps the old
  "fail loudly if libpulse/opus is missing" behaviour. It no longer
  applies `patches/xorg-server-source/`. That directory was deleted: its
  two hard-error `PKG_CHECK_MODULES` patches are superseded by the
  fork's `xserver*.patch` changes, which now live in our 0004.
- Patches 0002-0006 were regenerated by applying the fork's diff onto
  clean `vendor/tigervnc` HEAD + 0001. Most hunks applied cleanly. The
  `config.h.in` and `vncviewer/CMakeLists.txt` hunks needed hand-placing
  because their context depends on the newer upstream/FLTK commits.
  Top-level `CMakeLists.txt` and `config.h.in` changes went into 0003.

Verified: `server/build-linux.sh` builds clean. `config.h` has both new
defines, and `ldd` shows x0vncserver, w0vncserver and vncviewer all
linking libpulse+libopus. `core/build/host` `smoke_test` against
`smoke_test_server` reports ALL TESTS PASSED, with Opus 440Hz magnitude
ratio 0.97. `server/build-xvnc.sh` rebuilds Xvnc from scratch with the new
`--enable-vnc-audio` configure path. `AudioCapturePulse.cxx` is compiled
into `libvnccommon`, there are no macro-redefinition warnings, and the
`Xvnc` binary links libpulse+libopus. Android/iOS core builds were not re-run;
they apply the same patches and the client-side change is the one-line
`supportsNativeAudio` set above.

### Follow-up: vendor pin bumped to the fork's base (2026-10-20)

The hand-placed hunks above were caused by version drift: the fork sits
on upstream `30df0c7f`, and our pin was `886800c7`, 42 commits older.
The user asked for the pin to match the fork's `master`. `30df0c7f` is
on upstream TigerVNC's own `master`, so `.gitmodules` still points at
`TigerVNC/tigervnc`.

Done by reverse-applying 0006..0001, checking out `30df0c7f`, and
re-applying. Only 0003's `config.h.in` hunk needed re-placing (upstream
dropped `HAVE_VIDEO_PROCESSOR_MFT` from that context). All six patches
were then regenerated against the new base, and their `+`/`-` content is
unchanged. On the same base, the vendor tree now matches fork commit
`b0ccb88a` exactly, apart from 0001 (`i18n.cxx`), the
`VNC_AUDIO_CAPTURE` fix (`hw/vnc/Makefile.am`, `XserverDesktop.{h,cc}`),
and the fork-only FLTK 1.4/`AUDIO.md`/`scripts/` files.

Rebuilt on the new pin: `server/build-linux.sh --clean`, the
`core/build/host` smoke test (ALL TESTS PASSED, Opus ratio 0.97),
`third_party/android/build-core.sh`, `android/build-android.sh` (APK
builds), and `server/build-xvnc.sh`. No repo-side build changes were
needed for the 42 upstream commits. The `master` worktree is still on
`886800c7`.

## Appendix: `common/core/i18n.cxx` fixes

Originally `patches/vendor-tigervnc/0001-ios-nls-fixes.patch`, applied to
the submodule at build time; now committed directly to
`common/core/i18n.cxx`. Found while getting the iOS arm64 spike to
compile; the Android spike hit the same NLS-fallback bug plus one more of
the same shape, so these are not iOS-specific:

1. `#ifdef __APPLE__` around `<Carbon/Carbon.h>` covers iOS too (Carbon is
   macOS-only, deprecated even there), so it doesn't compile for iOS/tvOS.
   Narrowed with `TargetConditionals.h`'s `TARGET_OS_OSX` to macOS only;
   `getlocaledir()` returns `nullptr` on iOS/tvOS (no bundle resource
   lookup or gettext catalogs there -- mobile UIs use native localization).
2. Latent upstream bug, not iOS-specific: when NLS is disabled (no gettext
   found), `i18n.cxx`'s own `#undef dgettext` block (meant only to peel back
   `core/i18n.h`'s `#define dgettext dgettext_rfb` redirection) also strips
   `gettext.h`'s no-NLS fallback macros for the same names, leaving
   `dgettext()`/`dcgettext()`/`dngettext()`/`dcngettext()` completely
   undeclared. Never hit on desktop Linux (gettext is always found there),
   but real on iOS/Android, which don't ship libintl at all. Fixed by
   re-declaring the no-NLS fallback semantics locally after the `#undef`.
   Worth upstreaming independent of the mobile port.
3. `getlocaledir()`'s final `#else` branch returns
   `CMAKE_INSTALL_FULL_LOCALEDIR`, a macro tigervnc's own top-level
   CMakeLists.txt defines via GNUInstallDirs -- undefined (compile error)
   for any build, like ours, that doesn't go through that file. Added an
   explicit `#elif defined(__ANDROID__)` branch returning `nullptr`, same
   as iOS/tvOS: no filesystem-installed locale directory or gettext
   catalogs on Android either (native string resources instead).
