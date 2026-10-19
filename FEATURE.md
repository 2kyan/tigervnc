# Features

`fltk-1.4` adds FLTK 1.4 support for the viewer to upstream `master`.
The same feature combined with the audio extension is on `dev`.

## Branches

| Branch | Based on | Contains |
|---|---|---|
| `master` | upstream TigerVNC | Upstream code, FLTK 1.3 |
| `fltk-1.4` (this branch) | `master` | [FLTK 1.4](#fltk-14-fltk-14) |

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
- [ ] macOS and Windows builds with FLTK 1.4
