#!/bin/bash
# Installs, via apt, the system packages needed to build this tree on
# Linux: vncviewer (scripts/build-viewer.sh), x0vncserver, w0vncserver
# and Xvnc (scripts/build-server.sh), and the mobile/core host build
# and smoke test. The Android and iOS dependencies are cross-compiled
# from source by mobile/third_party/*/build-deps.sh instead; this only
# installs the host tools those need (meson, ninja).
#
# Debian/Ubuntu only. Safe to re-run.
#
# Usage: sudo scripts/install-deps.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Needs root (apt install) -- re-run as: sudo $0" >&2
  exit 1
fi

# Base build tooling (meson is for the Android/iOS pixman build)
PACKAGES=(build-essential cmake ninja-build meson pkg-config gettext git)

# common/{core,rdr,network,rfb}
PACKAGES+=(zlib1g-dev libpixman-1-dev libjpeg-dev libgnutls28-dev nettle-dev)

# Optional server features: PAM, systemd, password quality checks
PACKAGES+=(libpam0g-dev libsystemd-dev libpwquality-dev)

# X11: x0vncserver's desktop capture and vncviewer
PACKAGES+=(libx11-dev libxext-dev libxdamage-dev libxfixes-dev
           libxrandr-dev libxtst-dev libxi-dev)

# Audio extension (AUDIO.md): PulseAudio capture and playback, Opus codec
PACKAGES+=(libpulse-dev libopus-dev)

# w0vncserver: Wayland/xdg-desktop-portal server
PACKAGES+=(libglib2.0-dev libpipewire-0.3-dev libwayland-dev
           libwayland-bin libxkbcommon-dev uuid-dev)

# vncviewer with the bundled FLTK 1.4 (3rdparty/fltk): X11 font and
# window libraries it builds against
PACKAGES+=(libxft-dev libxinerama-dev libxcursor-dev libxrender-dev
           libfontconfig-dev)

# H.264 decoding in vncviewer (optional)
PACKAGES+=(libavcodec-dev libavutil-dev libswscale-dev)

# mobile/core host smoke test: libvncserver-based test server
PACKAGES+=(libvncserver-dev)

# Xvnc: the X.Org source matching the installed X server, plus autotools
PACKAGES+=(xorg-server-source xserver-xorg-dev autoconf automake
           libtool xutils-dev)

apt-get update
apt-get install -y "${PACKAGES[@]}"

# Xvnc also needs the X server's own build dependencies, which are too
# many and too version-specific to list by hand. This needs deb-src
# entries; don't let a failure here undo the packages above.
echo "==> apt-get build-dep xserver-xorg-core (Xvnc build dependencies)"
if ! apt-get build-dep -y xserver-xorg-core; then
  echo "!! apt-get build-dep failed, most likely because deb-src isn't enabled." >&2
  echo "!! Only Xvnc needs it. On Ubuntu, add deb-src to the 'Types:' line in" >&2
  echo "!! /etc/apt/sources.list.d/ubuntu.sources (or uncomment the deb-src" >&2
  echo "!! lines in /etc/apt/sources.list), then re-run this script." >&2
fi

echo "==> done. Next: scripts/build-viewer.sh and/or scripts/build-server.sh"
