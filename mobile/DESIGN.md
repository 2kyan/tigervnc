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

## Progress Log

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
