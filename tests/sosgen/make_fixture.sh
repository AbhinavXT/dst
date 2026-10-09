#!/usr/bin/env bash
# Builds sosgen and rewrites the SYNTHETIC SoS captures in schema/fixtures/:
#   sos_synthetic_loco{1,2}.log  README 01's full layout (version 1)
#   sos_minimal_loco{1,2}.log    README 03's minimal layout (version 2)
# The same scenario for both. Deterministic: the same bytes every run.
# See sosgen.c for what they prove.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../../schema/fixtures"
LINFO="$HERE/../../replay/2026-10-08/loco_1_1_08102026_101300.cap"
TMP="$(mktemp -d)"
"${CC:-cc}" -std=c99 -Wall -Wextra -O1 -o "$TMP/sosgen" "$HERE/sosgen.c" -lm
"${CC:-cc}" -std=c99 -Wall -Wextra -O1 -DSOS_MINIMAL -o "$TMP/sosgen_min" "$HERE/sosgen.c" -lm
"$TMP/sosgen" "$OUT/sos_synthetic_loco1.log" "$OUT/sos_synthetic_loco2.log" "$LINFO"
"$TMP/sosgen_min" "$OUT/sos_minimal_loco1.log" "$OUT/sos_minimal_loco2.log" "$LINFO"
wc -l "$OUT"/sos_synthetic_loco*.log "$OUT"/sos_minimal_loco*.log
