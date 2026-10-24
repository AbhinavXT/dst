# Brake Test Bench and BIU firmware — what is next

Written at the end of the session that built patches 359-360 (2026-10-24), so
the next conversation can pick up from here. Read this, then CHANGELOG
sessions 360 and 359.

## Where things stand

- **DLConsole patch 360:** Tools ▸ Transmit ▸ Brake Test Bench
  (`brakebenchwindow`, `biusim`, `biuprotocol`, `brakeplant`, `simwidgets`). The BIU
  listens on UDP 51234 and answers on 50001, as in RDSO MP.0.01.00.31 Rev.05
  Annexure-1. The analog card and the brake relays moved here from the Cab I/O
  Simulator.
- **Firmware BIU interface, reviewed and rewritten:** `docs/biu-firmware/` is
  a reference copy of what was handed to Abhinav. It is not built by DLConsole.
  `CHANGES.md` there lists the fixes, what the call sites must change, and the
  host test (62 checks, gcc). It has not been built with the TI compiler or run
  on target. Abhinav updates the call sites and tests it himself.

## Queued (agreed with Abhinav; scope set by what he sends)

1. **Real BIU frames.** Abhinav will send captured frames. Then:
   - check `biuprotocol` byte by byte against them;
   - use them in place of the spec-derived bytes in `tests/test_session360.cpp`;
   - add a golden validator (`schema/validate_biu.py`) like the other packets.
2. **Scenarios from test cases.** Abhinav will send a list of test cases.
   - Make them timed, repeatable sequences on the bench, e.g. "EB; at 2 s BC
     sticks at 1.0; at 10 s the BIU goes silent".
3. **Firmware-response verdicts.** Line up what the VCC does (its logs) against
   what the bench injected, e.g. "malfunction flagged within t_apply".
   - Implement alongside the test cases.
   - Verdict language per CLAUDE.md: say what was observed.
4. **Reply to all four VCCs.** Done in session 361: Network ▸ *these VCCs*
   takes a list, Loco ▾ fills in a loco's four controllers (the Firmware
   Flasher's table), and each frame goes to every VCC on the list. Still to
   confirm on a loco: that the VCC application answers on those addresses.
5. **Saved presets.** Save the bench's fault setup under a name and load it
   later: plant faults, rates, analog offsets, forced bits, manual pressures,
   validity modes, queued codes, frames off, silence.
   - Today these reset when the window reopens, on purpose.
   - Opening the window still starts clean; a preset is loaded only on request.
6. **DPCS/DPWCS and TSS nodes:** not simulated. Leave for now.
7. **Windows:** Abhinav tests it himself.

## Firmware decisions still open (Abhinav)

- **Loco list:** `kBrakeTable` has rows for WAP7, WAG9 and WAP5 only. Any other
  loco type (WAG7, WAP4, WDG4, ...) has no row, so it is sent BP 0
  (emergency). Which loco types the VCC must support, and their BC values and
  times. Abhinav is researching this.
- **Light engine brake:** read as the SA9 full application (BC 3.5, BP left at
  5.0). Abhinav will send the relevant test cases.
- **Responses, set from the test cases:**
  - on a malfunction (escalate, log, DMI);
  - on BIU fault and display codes (display codes go to the DMI on priority,
    Annexure-1 3);
  - on the panel test result (D2) and BIU / valve health (D1 bit 0, D3);
  - on losing the BIU link (`biu_link_up()` false);
  - on an analog vs BIU-reported pressure mismatch.

## Known environment notes

- In the cloud container, the Qt 5 gate fails sessions 134 and 135 (4
  window-size checks). They fail the same way on unchanged `main` there, and
  pass on Qt 6 and in CI.
- Tag pushes are refused from the container (HTTP 403). `patch-359` and
  `patch-360` were pushed from Abhinav's machine, or still need to be.
