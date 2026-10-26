#!/bin/bash
# Watches an Xvnc (Xtigervnc) server and, whenever its main thread stays
# busy for a while, records where it is spinning: stack samples plus the
# state of its threads and client connections.
#
# Usage: scripts/watch-xvnc-spin.sh [pid]
#   e.g. nohup scripts/watch-xvnc-spin.sh >/dev/null 2>&1 &
#
# Without a pid it follows the oldest running Xtigervnc/Xvnc of this
# user, picking up a new one if the server is restarted.
#
# Settings (environment):
#   THRESHOLD   main thread CPU percentage that counts as busy (default: 90)
#   DURATION    seconds it must stay busy before capturing    (default: 10)
#   SAMPLES     stack samples per capture                     (default: 20)
#   COOLDOWN    seconds to wait after a capture               (default: 60)
#   OUT_DIR     where reports go           (default: ~/.cache/xvnc-spin)
#
# Stacks come from gdb when it is installed (proper backtraces), and
# otherwise from a small ptrace sampler built on first use, which reports
# the exact function being executed and a best-effort list of callers
# found by scanning the stack.

set -euo pipefail

THRESHOLD="${THRESHOLD:-90}"
DURATION="${DURATION:-10}"
SAMPLES="${SAMPLES:-20}"
COOLDOWN="${COOLDOWN:-60}"
OUT_DIR="${OUT_DIR:-$HOME/.cache/xvnc-spin}"
FIXED_PID="${1:-}"

TOOL_DIR="$OUT_DIR/tools"
SAMPLER="$TOOL_DIR/sampler"
SYMBOLIZER="$TOOL_DIR/symbolize.py"

log()
{
  echo "$(date '+%F %T') $*"
}

build_tools()
{
  mkdir -p "$TOOL_DIR"

  # Rebuilt when this script changes, as the source lives here
  if [ ! -x "$SAMPLER" ] || [ "${BASH_SOURCE[0]}" -nt "$SAMPLER" ]; then
    cat > "$TOOL_DIR/sampler.c" <<'EOF'
/* Prints one line per sample: the thread's RIP followed by the raw words
 * at the top of its stack. Each sample stops the thread only briefly. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv)
{
  pid_t tid;
  int count, i, k;

  if (argc != 3)
    return 2;
  tid = atoi(argv[1]);
  count = atoi(argv[2]);

  for (i = 0; i < count; i++) {
    struct user_regs_struct regs;
    int status, sig;

    if (ptrace(PTRACE_SEIZE, tid, 0, 0) != 0) {
      perror("ptrace");
      return 1;
    }
    ptrace(PTRACE_INTERRUPT, tid, 0, 0);
    if (waitpid(tid, &status, __WALL) < 0) {
      perror("waitpid");
      return 1;
    }
    /* A real signal may have stopped it before our interrupt did;
     * hand it back on detach instead of swallowing it */
    sig = 0;
    if (WIFSTOPPED(status) && (status >> 16) == 0)
      sig = WSTOPSIG(status);
    ptrace(PTRACE_GETREGS, tid, 0, &regs);

    printf("%llx", regs.rip);
    for (k = 0; k < 512; k++)
      printf(" %lx", ptrace(PTRACE_PEEKDATA, tid, regs.rsp + 8 * k, 0));
    printf("\n");

    ptrace(PTRACE_DETACH, tid, 0, sig);
    usleep(100000);
  }

  return 0;
}
EOF
    gcc -O1 -o "$SAMPLER" "$TOOL_DIR/sampler.c"
  fi

  cat > "$SYMBOLIZER" <<'EOF'
# Usage: symbolize.py SAMPLES MAPS
# Turns sampler output into a histogram of the executing function and,
# per sample, the callers that could be resolved from the stack scan.
import collections, subprocess, sys

def load_maps(path):
    maps = []
    for line in open(path):
        f = line.split()
        if len(f) < 6 or 'x' not in f[1] or not f[5].startswith('/'):
            continue
        lo, hi = (int(x, 16) for x in f[0].split('-'))
        maps.append((lo, hi, int(f[2], 16), f[5]))
    return maps

def load_segments(path):
    out = subprocess.run(['readelf', '-lW', path], capture_output=True,
                         text=True).stdout
    segs = []
    for line in out.splitlines():
        f = line.split()
        if f and f[0] == 'LOAD':
            segs.append((int(f[1], 16), int(f[2], 16), int(f[4], 16)))
    return segs

maps = load_maps(sys.argv[2])
segments = {}

def to_file_addr(addr):
    for lo, hi, off, path in maps:
        if lo <= addr < hi:
            fileoff = addr - lo + off
            if path not in segments:
                segments[path] = load_segments(path)
            for o, va, sz in segments[path]:
                if o <= fileoff < o + sz:
                    return path, fileoff - o + va
            return path, fileoff
    return None

samples = []
wanted = collections.defaultdict(set)
for line in open(sys.argv[1]):
    words = [int(w, 16) for w in line.split()]
    resolved = []
    for w in words:
        r = to_file_addr(w)
        if r:
            wanted[r[0]].add(r[1])
        resolved.append(r)
    samples.append(resolved)

names = {}
for path, addrs in wanted.items():
    addrs = sorted(addrs)
    out = subprocess.run(['addr2line', '-f', '-C', '-e', path] +
                         [hex(a) for a in addrs],
                         capture_output=True, text=True).stdout.splitlines()
    for i, a in enumerate(addrs):
        func = out[2 * i] if 2 * i < len(out) else '??'
        loc = out[2 * i + 1].split('/')[-1] if 2 * i + 1 < len(out) else '??'
        names[(path, a)] = None if func == '??' else \
            '%s: %s (%s)' % (path.split('/')[-1], func, loc)

def name(r):
    return names.get(r) if r else None

hist = collections.Counter(name(s[0]) or 'unknown' for s in samples if s)
print('Executing function (%d samples):' % len(samples))
for func, n in hist.most_common():
    print('  %3d  %s' % (n, func))

for i, s in enumerate(samples):
    print('\nSample %d: %s' % (i + 1, name(s[0]) or 'unknown'))
    shown = []
    for r in s[1:]:
        n = name(r)
        if n and n not in shown:
            shown.append(n)
            print('    ' + n)
        if len(shown) >= 20:
            break
EOF
}

find_pid()
{
  if [ -n "$FIXED_PID" ]; then
    echo "$FIXED_PID"
  else
    pgrep -o -u "$(id -u)" -x Xtigervnc || pgrep -o -u "$(id -u)" -x Xvnc || true
  fi
}

# Prints the main thread's utime+stime, in clock ticks
cpu_ticks()
{
  # The command name may contain spaces, so count fields from the end
  sed 's/.*) //' "/proc/$1/task/$1/stat" 2>/dev/null | awk '{print $12 + $13}'
}

capture()
{
  local pid="$1" report samples maps

  report="$OUT_DIR/spin-$(date '+%Y%m%d-%H%M%S')-$pid.txt"
  samples="$report.samples"
  maps="$report.maps"

  {
    echo "Xvnc pid $pid, main thread busy for ${DURATION}s or more"
    echo "Captured $(date)"
    echo "Command: $(tr '\0' ' ' < "/proc/$pid/cmdline")"
    echo "Binary: $(readlink "/proc/$pid/exe")"
    echo
    echo "== Threads"
    for t in /proc/"$pid"/task/*; do
      echo "$(basename "$t") $(cat "$t/comm") state=$(awk '/^State/{print $2}' "$t/status")" \
           "wchan=$(cat "$t/wchan" 2>/dev/null) syscall=$(cut -d' ' -f1 "$t/syscall" 2>/dev/null)"
    done
    echo
    echo "== Memory"
    grep -E 'VmRSS|VmSwap' "/proc/$pid/status"
    echo
    echo "== Client connections"
    ss -tnpi 2>/dev/null | grep -A1 "pid=$pid," || true
    echo
    echo "== Stacks"
  } > "$report"

  if command -v gdb >/dev/null; then
    for _ in $(seq "$SAMPLES"); do
      gdb -p "$pid" -batch -nx -ex bt 2>/dev/null |
        grep '^#' || true
      echo "--"
      sleep 0.2
    done >> "$report"
  elif build_tools >> "$report" 2>&1; then
    "$SAMPLER" "$pid" "$SAMPLES" > "$samples" 2>> "$report" || true
    cp "/proc/$pid/maps" "$maps"
    python3 -I "$SYMBOLIZER" "$samples" "$maps" >> "$report" 2>&1 || true
  else
    echo "No gdb, and the ptrace sampler could not be built" >> "$report"
  fi

  {
    echo
    echo "== Server log tail"
    tail -40 "$(readlink "/proc/$pid/fd/2")" 2>/dev/null || true
  } >> "$report"

  log "captured $report"
}

mkdir -p "$OUT_DIR"

hz="$(getconf CLK_TCK)"
log "watching (threshold ${THRESHOLD}% for ${DURATION}s), reports in $OUT_DIR"

pid=""
busy=0
prev=""
while true; do
  cur_pid="$(find_pid)"
  if [ -z "$cur_pid" ] || [ ! -d "/proc/$cur_pid" ]; then
    [ -n "$FIXED_PID" ] && { log "pid $FIXED_PID is gone"; exit 0; }
    pid=""
    sleep 5
    continue
  fi
  if [ "$cur_pid" != "$pid" ]; then
    pid="$cur_pid"
    log "following Xvnc pid $pid"
    busy=0
    prev=""
  fi

  ticks="$(cpu_ticks "$pid")"
  if [ -n "$prev" ] && [ -n "$ticks" ]; then
    pct=$(( (ticks - prev) * 100 / hz ))
    if [ "$pct" -ge "$THRESHOLD" ]; then
      busy=$((busy + 1))
    else
      busy=0
    fi

    if [ "$busy" -ge "$DURATION" ]; then
      capture "$pid"
      busy=0
      sleep "$COOLDOWN"
      prev=""
      continue
    fi
  fi
  prev="$ticks"
  sleep 1
done
