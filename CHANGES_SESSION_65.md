# Session 65 — Gate repair: the Python half had drifted, and nothing saw it

Patch 64 shipped with the unit suite one check red (`contrastaudit`) and the
menu audit one check red (Ctrl+Shift+B). Running the Python golden validators
— which were **not part of the gate** — found four of eleven red:

| validator            | before          | cause                                                    |
|----------------------|-----------------|----------------------------------------------------------|
| `validate_arp_lsrp`  | 0 / 12460       | engine.py: exact-string `match=`, no `&` mask; oracle stale |
| `validate_dmi`       | 0 / 10658       | oracle predates `num coaches`; reads CRC one byte early   |
| `validate_slrp`      | 3324 / 3340 (B) | oracle and engine disagree on abs-location origin (open)  |
| `validate_uba`       | 0 frames        | hardcoded path to a log that was never committed          |

In each of the first three the schema and C++ had moved on purpose and the
Python side did not follow. This patch repairs that and makes the gate one
command, so it cannot happen quietly again.

## `verify.sh` — the whole gate

```
./verify.sh              validators + dltests + menu audit + headless smoke
./verify.sh --py         validators only (seconds)
./verify.sh --no-smoke
```

Exit code = number of failed stages. Builds go to `build-verify/`, nothing
is written into the tree. The smoke stage is the one sessions 54–64 ran by
hand, scripted: the real app offscreen, 500 real capture lines over UDP
(LSRP, DMI, SLRP, ARP) addressed to console id 101, pass = still alive when
the 14 s timeout fires.

## engine.py (the Python reference) — three fixes

1. **`match=` tokens.** `captype==arp,arprecv` (session 43) was compared as one
   string, so ARP never matched anything. Now `captype_tokens()`, same as C++
   `captypeTokens()`.
2. **`&` mask conditions.** `cond_ok("FRAME_NUM & 7 == 1")` looked up a field
   literally named `"FRAME_NUM & 7"`, got 0, and returned false — so all four
   LSRP health groups were skipped and `FRAME_NUM & 1 == 0` held for every
   frame. The Python reference reported *every* LSRP health word as stale.
3. **`<flags order="lsb" expand="all">` were ignored.** Found while fixing 2.
   Without `order="lsb"` the bits are named in read order, so each fault is
   reported as its mirror-image neighbour — B22 SPEED_SENSOR2_FAULT reads as
   B19 SESSION_KEY_MISMATCH. The real captures carry set bits (B22 × 366,
   B16 × 301, B17 × 117, …), so this was live, not theoretical. Now mirrors
   the C++ walker, including the per-bit `1 (FAULT)` / `0 (ok)` rows.

## Oracles brought up to the schema

- **arp_lsrp_oracle.decodeLSRP** — health decoded by group from `FRAME_NUM & 7`,
  LSB-numbered, with its own name table; even frames reported raw as stale.
  Written independently of the XML, so it still checks the engine.
- **dmi_oracle** — reads `num coaches` and takes the CRC over `[3:110]` from
  `[110:114]`. The CRC is the proof: with the byte, every captured frame
  passes; without it, every frame fails.

Result: arp **9207/9207**, lsrp **3253/3253**, dmi **10658/10658**.

## DMI CRC was not being checked in the app at all

The hand decoder only accepted the 115-byte DMI frame. Every one of the 10658
captured frames is 116 bytes, so `crcChecked` stayed false — no FAIL, just no
check, for as long as the emitter has sent `num coaches`. Both lengths are now
accepted, with the CRC word located by length. `crcheader` gains six checks:
the real 116 B frame passes; corrupting the num-coaches byte fails (it is
inside the span); a body corruption fails; a rebuilt 115 B frame still passes
and still fails when corrupted.

## Conditions: refuse, don't guess

One literal parser for every path, C++ and Python alike: decimal or `0x` hex,
optionally negative, **never octal** (`015` is fifteen). Before this, the mask
path took hex and the plain path read `0x0F` as 0.

`Decoder::load()` now runs `validateConditions()` over every `when=` and
`test=` and refuses the schema if one does not parse or names a field no
packet or struct declares. A refused schema leaves the decoder **empty**, the
same guarantee a malformed one already had. `engine.Schema` does the same and
raises. `RejectRules::load()` checks its `when=` guards with the same
`checkCondition()`.

The run-time rule is unchanged on purpose: a declared field the frame does not
carry reads as 0. Sub-packet fields are absent from most frames and the
reject rules depend on that.

All 20 conditions in `kavach.xml` and the one in `rejectrules.xml` pass.

`schemareload` +21 checks (refusal, empty-after-refusal, recovery, grammar,
hex, no-octal, absent-reads-0); `rejectrules` +2.

## Braking panel colours → `UiColor::series()`

The six-colour cycle in `brakingcurveplot.cpp` measured **2.12–3.09 : 1** on
Ayu Light's `#FCFCFC` — five of six under the 3 : 1 floor for graphical
objects. `UiColor::series(i)` keeps the hues and holds a light set lowered to
≥ 3.2 : 1; the dark set is the original cycle (≥ 6.1 : 1 on `#0B0E14`).
`uicolors` checks every entry against Base in both themes. Clears
`contrastaudit`.

## Ctrl+Shift+B → Braking Curves moves to **Ctrl+Shift+U**

Ctrl+Shift+B has meant *Selected row → Decode Workbench* since session 23, in
the main, compare and session windows. Session 57 also gave it to Braking
Curves, and Qt fires neither action on an ambiguous shortcut. U for the
`@uba` stream. **This was a call made here, not by you; revert is one line
(`mainwindow.cpp`, `actBraking`).**

## validate_uba

Looks for real `@uba` captures in `replay/*.cap|*.log` first. With none —
the case today — it runs on `schema/fixtures/uba_synthetic.log` (20 frames,
from `make_uba_fixture.py`, deterministic) and **says so in its PASS line**.
On synthetic frames the layout check is real (engine vs `struct.unpack`); the
curve invariants hold by construction and prove nothing about firmware.
Mutation-checked: a corrupted fixture fails 20/20 on invariants.

## Warnings: 21 → 0

`LogQuery::fieldName` handles `Field::Decoded` (describe() printed an empty
name for `field:` terms); fifteen dead `rfid*` coded-value maps plus
`eventDef` and `beUInt` removed from `capturedecoder.cpp` (RFID decoding has
lived in the schema since it moved); unused `mid` in `uistyle.cpp`; the
misleading indentation went with the condition rewrite.

## Verified

- Unit suite: **120 suites, 3140 checks, 0 failed** (was 3095 / 1 failed).
- Menu audit: **59 ok, 0 failed** (was 1 failed).
- Headless smoke: 500 datagrams incl. 100 DMI through the new CRC path;
  alive at timeout (124), no crash.
- Validators: **10 of 11 green**. App and tests build with 0 warnings.
- `./verify.sh` end to end: **13 of 14 stages ok, exit 1** — the one failure
  is `validate_slrp.py`, below. Expect exit 1 until that is settled.

## Open — one red, deliberately

**`validate_slrp` mode B, 16 frames**, all with `DIST_PKT_START ≠ 0`:

| | start-signal origin | tag list measured from |
|---|---|---|
| `kschema_oracle` | `refAbs − dps` (Nominal) | `refAbs` |
| `engine.py`, `schemadecoder.cpp` | `refAbs + travel · dps` | `blockAbs` |

The engine and C++ agree with each other and their comments describe a
deliberate choice; the oracle is probably just stale. But this is the one
question the oracle exists to settle, so it is left red until the sign is
confirmed against Annex-C rather than "fixed" by making the oracle agree.

Also outstanding, not in this patch's scope: no C++ suite for the braking
code (`brakingcurves`, `brakingcurveplot`, `brakingpanel`); a real `@uba`
capture in `replay/`; one pre-existing test-build warning (`test_quota.cpp:47`,
unused `w`).
