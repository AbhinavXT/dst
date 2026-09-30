# Session 77 — Loco Configuration: bulk send to up to 4 VCCs

## What changed

The send bar has **four target rows** (tick box, IP, port) instead of one IP
and port. **Send** goes to every ticked row with an address.
- **Same bytes to every target:** the same 375-byte datagram, CRC and all.
- **The button says how many:** "Send to VCC…" or "Send to 3 VCCs…".
- **The confirmation lists every target.** With more than one it also says
  *"All 3 receive the same configuration, including loco_unit_id N"*, next
  to the `vcc_crc`. Sending one unit id to what are actually different locos
  is the mistake bulk send makes easy, so it is stated where the operator
  presses Yes.
- **Each target is sent to separately.** `UdpSender`'s multi-target send
  only reports whether *any* target got through, so it isn't used here. A
  failed target is named in the status line with its reason ("Sent to 2 of
  3 — FAILED: 10.0.0.9:50001 (…)"), and the others are still sent to.
- **One history record per target**, each naming its own `ip:port`, so the
  History dialog, CSV and re-export work per VCC as before.
- **Checks before Send:**
  - at least one ticked row with an address;
  - every ticked address a valid IPv4 address (reported by row number);
  - the same `address:port` not listed twice;
  - an unticked row is ignored, whatever is in it.
- "Last sent … to" lists every target of the last send.
- **New** copies the current configuration's targets.

## Saved files

- `loco_configs.json` gains a `targets` array (ip, port, enabled × 4).
- A file from before this session loads its single `target_ip` / `port`
  into row 1.
- Row 1 is still written under the old keys, so an older DLConsole reads
  the file.

## Files

- `lococonfig/lococonfigcore.{h,cpp}`: `SendTarget`, `kMaxTargets`,
  `targetProblem()`, and targets in the JSON (read, write, legacy).
- `lococonfig/lococonfigwindow.{h,cpp}`: the four-row send bar,
  `setTargets()`, per-target send and history, the confirmation, the
  button label.
- `README_FLASHING_AND_CONFIG.md`: the "send the configuration" steps
  describe the targets.
- `tests/test_lococonfig.cpp`.

## Tests

- **`lococonfig` (70, +13):**
  - all four rows round-trip (address, port, ticked);
  - a pre-bulk-send file loads into row 1 and still writes the old key;
  - the rules: nothing to send to, a bad address refused by row, an
    unticked junk row ignored, the same `address:port` twice refused, same
    address on a different port fine.
- **`lococonfigrun` (30, +8):** the real window, three stand-in VCCs on
  127.0.0.1 and an unticked junk row.
  - The button says "Send to 3 VCCs…".
  - All three receive exactly the same datagram.
  - Three history records, each naming its own target.
  - The same target twice blocks Send.
- **Not tested end to end:** one target *failing* while the others succeed.
  On localhost there is no dependable way to make a single UDP send fail.
  The code path (per-target result, failure named in the status line) is
  small and straightforward.
- **Checked by eye:** the send bar with three targets filled in.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
131 suites / 3902 checks, menu audit 91 ok, headless smoke alive. Built
with Qt 5.15 only.
