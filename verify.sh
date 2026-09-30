#!/usr/bin/env bash
# =============================================================================
#  verify.sh -- the whole ship gate, one command, one exit code.
#
#      ./verify.sh              everything
#      ./verify.sh --py         Python golden validators only (seconds)
#      ./verify.sh --no-smoke   skip the headless smoke run
#
#  Gate = unit suite (dltests) + every schema/validate_*.py + menu audit +
#  headless smoke. Exit code is the number of stages that failed.
#
#  WHY
#    Until session 65 the gate was run by hand and the Python validators were
#    not part of it. Four of eleven had gone red without anyone seeing:
#    arp/lsrp 0/12460, dmi 0/10658, slrp 3324/3340, uba 0 frames. Each was a
#    deliberate change to kavach.xml or the C++ that the Python side never
#    followed. A stage that is not in the command does not get run.
#
#  Build dirs live under build-verify/ (git-ignored) so nothing is written
#  into the source tree: core/ (the shared static library, built first),
#  tests/, menuaudit/ and app/, which all link it. Set JOBS to override parallelism, QMAKE to pick a Qt.
# =============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/build-verify"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
QMAKE="${QMAKE:-qmake}"
PY="${PY:-python3}"
DO_CPP=1; DO_SMOKE=1
for a in "$@"; do
  case "$a" in
    --py)       DO_CPP=0; DO_SMOKE=0 ;;
    --no-smoke) DO_SMOKE=0 ;;
    *) echo "unknown option: $a"; exit 64 ;;
  esac
done

failed=0; summary=()
stage() {                      # stage NAME rc
  if [ "$2" -eq 0 ]; then summary+=("  ok    $1"); else summary+=("  FAIL  $1"); failed=$((failed+1)); fi
}
build() {                      # build DIR PROFILE
  mkdir -p "$1" && (cd "$1" && "$QMAKE" "$2" >/dev/null && make -j"$JOBS" >build.log 2>&1) \
    || { echo "build failed: $2 (see $1/build.log)"; tail -20 "$1/build.log"; return 1; }
}

# ---- 1. Python golden validators ------------------------------------------
echo "== golden validators"
for v in "$HERE"/schema/validate_*.py; do
  n="$(basename "$v")"
  res="$(cd "$HERE/schema" && "$PY" "$n" 2>&1)"; rc=$?
  line="$(printf '%s\n' "$res" | grep -E '[0-9]+/[0-9]+|PASS|FAIL' | tail -1)"
  printf '  %-24s rc=%d  %s\n' "$n" "$rc" "$line"
  stage "$n" "$rc"
done

# ---- 1b. the shared library -------------------------------------------------
# Session 87: every program below links build-verify/core/libdlcore.a, so the
# application sources are compiled once here instead of once per program.
core_ok=1
if [ "$DO_CPP" -eq 1 ] || [ "$DO_SMOKE" -eq 1 ]; then
  echo "== shared library (dlcore)"
  if build "$OUT/core" "$HERE/core/core.pro"; then echo "  ok"; else core_ok=0; stage "dlcore (build)" 1; fi
fi

if [ "$DO_CPP" -eq 1 ] && [ "$core_ok" -eq 1 ]; then
  # ---- 2. unit suite -------------------------------------------------------
  echo "== unit suite"
  if build "$OUT/tests" "$HERE/tests/tests.pro"; then
    (cd "$HERE/tests" && QT_QPA_PLATFORM=offscreen "$OUT/tests/dltests" >"$OUT/dltests.log" 2>&1); rc=$?
    tail -1 "$OUT/dltests.log"; grep -B3 '  FAIL' "$OUT/dltests.log" | head -30
    stage "dltests" "$rc"
  else stage "dltests (build)" 1; fi

  # ---- 3. menu audit -------------------------------------------------------
  echo "== menu audit"
  if build "$OUT/menuaudit" "$HERE/tests/menuaudit.pro"; then
    (cd "$HERE/tests" && QT_QPA_PLATFORM=offscreen "$OUT/menuaudit/menuaudit" >"$OUT/menuaudit.log" 2>&1); rc=$?
    echo "  $(grep -c '^ok' "$OUT/menuaudit.log") ok, $(grep -c '^FAIL' "$OUT/menuaudit.log") failed"
    grep '^FAIL' "$OUT/menuaudit.log"
    stage "menuaudit" "$rc"
  else stage "menuaudit (build)" 1; fi
fi

if [ "$DO_SMOKE" -eq 1 ] && [ "$core_ok" -eq 1 ]; then
  # ---- 4. headless smoke ---------------------------------------------------
  # The real app, offscreen, fed real capture lines over UDP (dest 101, the
  # console's id) for 12 s. Pass = still running when the timeout fires
  # (exit 124). A crash exits with a signal instead.
  echo "== headless smoke"
  if build "$OUT/app" "$HERE/app/app.pro"; then
    port=50002
    # app/app.pro puts the program in the main build folder: $OUT/DLConsole.
    (cd "$OUT/app" && QT_QPA_PLATFORM=offscreen timeout 14 "$OUT/DLConsole" >"$OUT/smoke.log" 2>&1; echo $? >"$OUT/smoke.rc") &
    sleep 3
    "$PY" - "$HERE/replay" "$port" <<'EOF'
import glob, socket, struct, sys, time
replay, port = sys.argv[1], int(sys.argv[2])
lines = []
for tag, n in (("@lsrp", 200), ("@dmi", 100), ("@slrp", 100), ("@arp", 100)):
    got = 0
    for cap in sorted(glob.glob(replay + "/*.cap")):
        for ln in open(cap, errors="replace"):
            if ln.startswith(tag):
                lines.append(ln.rstrip("\r\n")); got += 1
                if got >= n: break
        if got >= n: break
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for ln in lines:
    p = ln.encode()
    s.sendto(struct.pack("<BBBHH", 2, 101, 1, len(p), 1) + p, ("127.0.0.1", port))
    time.sleep(0.005)
print("  sent %d datagrams" % len(lines))
EOF
    wait
    rc="$(cat "$OUT/smoke.rc")"
    echo "  app exit $rc (124 = alive at timeout)"
    [ "$rc" -eq 124 ]; stage "smoke" $?
  else stage "smoke (build)" 1; fi
fi

echo
echo "== summary"
printf '%s\n' "${summary[@]}"
echo "$failed stage(s) failed"
exit "$failed"
