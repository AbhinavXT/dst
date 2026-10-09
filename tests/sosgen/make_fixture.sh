#!/usr/bin/env bash
# Builds sosgen and rewrites the two SYNTHETIC SoS captures in schema/fixtures/.
# Deterministic: the same bytes every run. See sosgen.c for what they prove.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../../schema/fixtures"
BIN="$(mktemp -d)/sosgen"
"${CC:-cc}" -std=c99 -Wall -Wextra -O1 -o "$BIN" "$HERE/sosgen.c" -lm
"$BIN" "$OUT/sos_synthetic_loco1.log" "$OUT/sos_synthetic_loco2.log" \
    "$HERE/../../replay/2026-10-08/loco_1_1_08102026_101300.cap"
wc -l "$OUT"/sos_synthetic_loco*.log
