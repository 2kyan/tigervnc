# Shared helpers for build-viewer.sh and build-server.sh. Sourced, not run.
#
# Settings (override from the environment):
#   BUILD_TYPE    CMake build type                  (default: Release)
#   ENABLE_AUDIO  AUTO, ON or OFF; see AUDIO.md     (default: AUTO)
#   JOBS          parallel build jobs               (default: nproc)
#   CMAKE_ARGS    extra arguments passed to cmake

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
ENABLE_AUDIO="${ENABLE_AUDIO:-AUTO}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
CMAKE_ARGS="${CMAKE_ARGS:-}"

log()
{
  echo "==> $*"
}

die()
{
  echo "error: $*" >&2
  exit 1
}

# need_cmd <command> [package hint]
need_cmd()
{
  command -v "$1" >/dev/null 2>&1 || die "missing '$1'${2:+ (install $2)}"
}

# need_pkg <pkg-config module> [package hint]
need_pkg()
{
  pkg-config --exists "$1" 2>/dev/null || die "missing pkg-config module '$1'${2:+ (install $2)}"
}

# Audio needs PulseAudio and Opus unless explicitly turned off. With
# AUTO a missing library only disables audio, so just warn.
check_audio_deps()
{
  local mod hint
  [ "$ENABLE_AUDIO" = "OFF" ] && return 0
  for mod in libpulse:libpulse-dev opus:libopus-dev; do
    hint="${mod#*:}"
    mod="${mod%%:*}"
    if ! pkg-config --exists "$mod" 2>/dev/null; then
      [ "$ENABLE_AUDIO" = "ON" ] && die "ENABLE_AUDIO=ON but '$mod' is missing (install $hint)"
      echo "warning: '$mod' not found, audio support will be disabled (install $hint)" >&2
    fi
  done
}

check_common_deps()
{
  need_cmd cmake cmake
  need_cmd make make
  need_cmd pkg-config pkg-config
  need_cmd c++ g++
  check_audio_deps
}

# cmake_configure <build dir> [extra cmake args...]
cmake_configure()
{
  local dir="$1"
  shift
  log "configuring $dir"
  # shellcheck disable=SC2086
  cmake -S "$ROOT_DIR" -B "$dir" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_AUDIO="$ENABLE_AUDIO" \
    "$@" $CMAKE_ARGS
}

# cmake_build <build dir> [targets...]
cmake_build()
{
  local dir="$1" target
  shift
  if [ $# -eq 0 ]; then
    log "building $dir"
    cmake --build "$dir" -j "$JOBS"
    return
  fi
  for target in "$@"; do
    log "building $target"
    cmake --build "$dir" -j "$JOBS" --target "$target"
  done
}
