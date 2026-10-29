# Brake Test Bench and BIU firmware — what is next

Updated at the end of the session that built patches 361-363 (2026-10-24). The
next session picks up here **when Abhinav works on the hardware**. Read this,
then CHANGELOG sessions 363 back to 359.

## Where things stand

- **Patch 360:** Tools ▸ Simulators ▸ Brake Test Bench (Transmit until 392) (`brakebenchwindow`,
  `biusim`, `biuprotocol`, `brakeplant`, `simwidgets`). The BIU listens on UDP
  51235 (51234 until patch 390, when it clashed with the log port) and answers on 50001, as in RDSO MP.0.01.00.31 Rev.05 Annexure-1. The
  analog card and the brake relays moved here from the Cab I/O Simulator.
- **Patch 361:** the BIU answers a list of VCCs (Network ▸ *these VCCs*), one
  datagram per VCC. **Loco ▾** fills in a loco's four VCC controllers from the
  Firmware Flasher's table (session 300): loco 1 is .16 .102 .103 .104, loco 2
  is .77 .105 .106 .107, loco 3 is .87 .88 .96 .97. A command from an address
  not on the list is flagged. .126 and .101 are the PCs, not VCCs.
- **Patch 362:** saved presets (the **Presets** button). Save, Load, Start
  clean and Delete the fault setup: plant faults, rates, analog offsets, manual
  pressures, validity modes, forced bits (isolation included), queued codes,
  frames off, silence. Stored in `dlconsole.ini`, `brakebench/presets`, as JSON
  (`brakebenchpreset.{h,cpp}`). The window still opens clean. The network, the
  analog card and the relays stay out of a preset. The rates are both in a
  preset and a saved window setting, so loading a preset changes them for good
  (ask Abhinav whether presets should leave rates alone).
- **Patch 363:** "(changed)" on the Presets button counts only what the
  operator sets, not the code queue the BIU moves by itself (0x1040 on
  isolation, codes leaving on acks). `tests/screenshot_main.cpp` gained
  `SHOT_PRESET=1`. All 11 themes were looked at on Bench, BIU → VCC, Network
  and the Presets menu: all readable.
- **Firmware BIU interface, reviewed and rewritten:** `docs/biu-firmware/` is
  a reference copy of what was handed to Abhinav; DLConsole does not build it.
  `CHANGES.md` there lists the fixes, what the call sites must change, and the
  host test (62 checks, gcc). **Not yet built with the TI compiler or run on
  target.** Abhinav updates the call sites and tests it himself.

## Waiting on Abhinav (bring these to the hardware session)

1. **Real BIU frames** (captured). Then:
   - check `biuprotocol` byte by byte against them;
   - use them in place of the spec-derived bytes in `tests/test_session360.cpp`
     (CLAUDE.md: real frames wherever one exists);
   - add a golden validator (`schema/validate_biu.py`) like the other packets.
2. **The test-case list.** It sets which timed scenarios to build, and the
   firmware's responses:
   - on a malfunction (escalate, log, DMI);
   - on BIU fault and display codes (display codes go to the DMI on priority,
     Annexure-1 3);
   - on the panel test result (D2) and BIU / valve health (D1 bit 0, D3);
   - on losing the BIU link (`biu_link_up()` false);
   - on an analog vs BIU-reported pressure mismatch.
3. **Firmware decisions:**
   - **Loco list:** `kBrakeTable` has rows for WAP7, WAG9 and WAP5 only. Any
     other loco type (WAG7, WAP4, WDG4, ...) has no row, so it is sent BP 0
     (emergency). Which types the VCC must support, with their BC values and
     times. Abhinav is researching this.
   - **Light engine brake:** read as the SA9 full application (BC 3.5, BP left
     at 5.0), until the test cases say otherwise.
4. **Whether the firmware can log its BIU traffic** (see the gap below). That
   is the stronger evidence for verdicts, and it is Abhinav's call.

## Gaps found (2026-10-24) that block real testing

- **Done in patch 404:** Record (on the bench and the Loco Simulator) saves
  every BIU, analog, speed and tag frame, unfiltered and timestamped, to a
  `.benchrec` file; Tools ▸ Simulators ▸ Test recording… reads it back.
  What follows was the gap as found:
- **The bench's traffic is not saved.** The Frames tab keeps the last 2000 rows
  in memory (`kLogCap`, `brakebenchwindow.cpp`); there is no save or export.
  A test leaves no record of what the bench injected, or of what the VCC
  commanded and when. Every verdict needs that record, and the RDSO Generic
  Test Bench FRS asks for it (7.6.4.7: every BIU exchange logged with a
  timestamp).
- **The VCC's logs say little about brakes.** `schema/kavach.xml` has the
  analog frames (`@analog_top/bottom`), the BIU-isolated event, the
  BIU-connectivity-fault flag, the BIU health bit, the static brake parameters
  (LINFO) and the relay feedbacks. **No packet carries the command the VCC sends
  the BIU (A9 / SA9) or the BIU status it received.** So the bench's own record
  is the only place the command is seen. "Malfunction flagged within t_apply"
  can only be judged by lining that record up against the VCC's DMI and event
  logs.
- **No timed sequences.** A preset is a starting state. "EB; at 2 s BC sticks at
  1.0; at 10 s the BIU goes silent" is done by hand today and cannot be
  repeated exactly.
- **No train movement.** Brake commands move the simulated air, not a train.
  The FRS's BIU-S feeds NSB / FSB / EB / traction cut-off into a Speed
  Simulator (7.6.4.5-6, 7.6.5.7). DLConsole has no speed simulator. Not checked
  yet: whether the bench reacts to traction cut-off at all.

## Proposed next (not started; confirm scope with Abhinav first)

1. **Record the bench's traffic:** BIU both ways, analog, and the bench's own
   actions ("BC stuck at 1.0", "preset loaded", "silenced"), to a file that
   DLConsole's replay and tabs open beside the VCC's logs. Items 2 and 3 build
   on it.
2. **A scenario runner:** timed steps made of presets and single changes ("at 2
   s set BC stuck"), run the same way every time and written into that record.
   Build the engine now; fill in the steps from Abhinav's test cases.
3. **Firmware-response verdicts:** line up the record against the VCC's logs.
   Verdict language per CLAUDE.md: say what was observed.

## Only a loco or the target can confirm

- The firmware BIU interface on target (TI compiler, the CAN send put in, a real BIU on the CAN bus).
- That a loco's VCC applications answer on the four flasher-table addresses,
  and that each VCC keeps its BIU state from the frames it is sent.
- The analog card's 34-byte frame against the real card.
- Windows (Abhinav tests it). DPCS/DPWCS and TSS nodes: not simulated, leave
  for now.

## The RDSO Generic Test Bench FRS (draft, July 2026)

Abhinav shared `RDSO/GTB Kavach/FRS/2026` v1.0 (a 26-page draft; not in the
repo). It is a lab test bench for system-level *software* tests of KAVACH,
driven by SIF 0533 v4.22. The loco (OVK), the station (SVK) and the RIU are
black boxes. Simulators reach them through OEM adapters. A Test Scenario
Editor and a Test Bench Controller run the scenarios, a Test Scenario Logger
records them, and a Test Evaluation Tool judges them afterwards.

Where DLConsole stands against its simulators:

| FRS module | DLConsole today |
|---|---|
| BIU-S (7.6.4) | **Brake Test Bench**, the closest match. Missing: brakes driving a speed simulator, a saved timestamped log, traction cut-off unchecked |
| Loco I/O (not in the FRS) | Cab I/O Simulator and the analog card (IOA serial / UDP) |
| RFID-S (7.6.6) | Tag Builder and SIF 14 scenarios make routes for the external `tags_sim`, and preview what it sends. Not a live feeder |
| RMS (7.6.7) | Packet Maker / Packet Sequence send packets over UDP. Delay / loss / radio holes not checked |
| DIS (7.6.8) | DMI window draws the screen from `@dmi` logs (the recording half). No loco-pilot inputs |
| TSL / TET (7.6.11, 7.6.15) | Capture, replay, Test Case Catalog, test runs, test assertions, partly |
| Not in DLConsole | Speed Simulator, GPSS, Power Analyzer, Yard Simulator, SMOCIP-S, Track Profile Sim., Adjacent SVK Sim., a scenario editor / controller driving the simulators together |

Which of these to build is Abhinav's call.

## Known environment notes

- In the cloud container, the Qt 5 gate fails sessions 134 and 135 (4
  window-size checks). They fail the same way on unchanged `main` there, and
  pass on Qt 6 and in CI. Patches 361-363 were gated with exactly those 4 and
  nothing else.
- Tag pushes are refused from the container. `patch-359` to `patch-363` are to
  be pushed from Abhinav's machine.
- Screenshots: build `tests/shot.pro` inside a `build-verify*/shot` folder, then
  `SHOT_WINDOW=brakebench SHOT_TAB=0..5 SHOT_PRESET=1 ./shot`.
