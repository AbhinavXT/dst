# A full station and a loco tested against it, in DLConsole alone

The execution plan, patch by patch (Abhinav, 2026-11-01). The goal is
that one person, with DLConsole and nothing else, can do three things:

1. **Make a whole station:** its tags, TINs, lines, loops, points, signals,
   track circuits, routes (entry → exit signal), MA by aspect, and the
   track profile (SSP, gradient, LC gates, turnouts, tag linking, track
   conditions, TSR).
2. **Test a real VCC on the bench** with DLConsole as everything around
   it:
   - the station: regular packet (SLRP), access authority (AAP),
     emergencies (AEP, SoS);
   - other locos;
   - the cab: driver inputs and DMI;
   - speed sensors, RFID readers, brake pressures, the BIU;
   - NMS and the data logger.
3. **Test a whole loco**, with DLConsole as the station or stations, the
   other locos, and the NMS / logger.

Each patch follows CLAUDE.md:
- One patch is one commit, tagged `patch-<N>`, with its own CHANGELOG
  section.
- It goes in only with `verify.sh` green on Qt 5.15 and Qt 6.
- Its tests use real frames from `replay/` and real station data (LPI,
  HBL's sheets) wherever they exist.
- The schema is ground truth, and the encoder refuses rather than guesses.
- The tools say what the receiver would do or what was observed; verdicts
  belong to signatories.

Before each patch starts, its scope is confirmed with Abhinav.

Two words in the request are read as follows (to confirm):
- **"pin"** is **TIN**.
- **"the reward from the machinery tag linking"** is the **tag linking**
  data: the linking distances from tag to tag that the SLRP profile
  carries.

---

## 1. What is there today, and what is missing

| Wanted | Today (session) | Missing | Patch |
|---|---|---|---|
| Tags, TINs, lines, loops, points, signals | Station Layout (208–239, 431–439), guided steps and New station (459) | — | done |
| Track circuits | circuits sheet (436), its tab (459) | — | done |
| Entry / exit signals, routes (Table of Control) | KAVACH tab (422–425), ToC from HBL (455) | a Routes step quick to fill; routes proposed from the layout | 460 |
| Movement authority | MA in sections by aspect (422), cut behind a loco ahead (427) | the MA-by-aspect table as a step; checked against the signals | 461 |
| SSP, gradient | profile rows per route (422), from HBL (456) | a step with rows generated, pasted, lengths checked | 462 |
| LC gates, turnouts, tag linking, track conditions | profile rows (422) | the same; LC rows checked against LC tags; linking generated from the route's tags | 463 |
| TSR | TSR rows, by OHE pole (457) | a TSR step, the TSR route table editable | 464 |
| Next station, hand-over | NEXT_STN_COMM (447), stretch (458) | border table editable; a stretch made in the UI | 465 |
| A firm's sheets | `scripts/kavach_sheets.py` (454–458), command line | opened from the app | 466 |
| Station parameters (ID, version, keys) | in the KAVACH data, Session keys | one Station step for everything the packets carry | 467 |
| SLRP, AAP (station regular, access authority) | Packet Maker answers ARP → AAP, LSRP → SLRP (378, 422–449) | run from the open station, in one Run step | 471 |
| ARP, LSRP from the loco | decoded live; the VCC sends them | — | — |
| AEP (additional emergency) | schema has AEP; SoS kinds (426, 445) | AEP sent by the station; triggers in the Run step | 476 |
| Other locos | real locos' LSRPs seen by the station (427, 445) | virtual locos that send | 475 |
| Cab simulator inputs, loco outputs | Cab I/O Simulator (324–359), @lout (364) | driven by a scenario | 473 |
| Speed inputs | Loco Simulator speed sensors (391, 408) | moving along the station's routes, not one line | 472 |
| RFID input | Loco Simulator RFID readers (395, 405, 411) | the tags of the route the station set | 472 |
| Brake pressure inputs, brake outputs, BIU | Brake Test Bench and BIU sim (360–363, 390), analog card (346–357), loco brakes from the bench (398, 418–421), brake chain (453) | wired together by the Run step | 474 |
| NMS | NMS health / fault / RSSI tables (247–256, 377, 387) | part of the run's links and recording | 477 |
| Data logger | live capture, recordings (.dlr), review (368–375) | one recording of the whole run | 477 |
| Scenarios, results | test catalog and runs (231, 263, 306), review pack (370) | a scenario timeline; results observed per step | 478–479 |

So most of the pieces exist. The plan does three things:
- **(A)** makes the station data complete and easy to fill;
- **(B)** ties the simulators to the open station in one **Run** step with
  one clock, one Start and one recording;
- **(C)** points the same Run step at real hardware.

---

## 2. Phase A: the station's data, complete

Each patch adds a step to Station Layout's guided steps (459), next to the
drawing. Each step:
- edits a table;
- picks on the drawing where that helps;
- reports problems with their rows.

The data lives in the station file's `kavach` block (docs/STATION_KAVACH.md),
read by the same engine that answers the loco.

### 460: Routes (the Table of Control) as a step

- **Grid:** route name, entry signal, exit signal, direction (from the
  tags), section type, authority (FS / OS / SR, OS speed), points Normal /
  Reverse, track circuits to be clear, TINs (with the overlap's).
- **Tags in order:** picked on the drawing (425). Each linking distance is
  computed from the tags' locations and can be edited. A distance that
  disagrees with the tags is a problem (424's `locationChecks`).
- **Propose routes:** from every signal, follow the lines and points in its
  direction to the next signal. Each path is one proposed row, Normal
  route and Reverse route through the points. A path that cannot be
  decided (no next signal, a point with no tags) is listed, not guessed.
- **Done when:**
  - LPI's ToC rows (HBL, 455) are reproduced by Propose plus picks;
  - the station made by New station gets its routes from Propose alone;
  - Packet Maker answers LPI's real LSRP 40380 with MA 1956 m as before.

### 461: Signals and movement authority

- **One signal list:** the layout's signals and the KAVACH signals are
  shown as one list. A signal in one and not the other, or with a
  different foot tag, is a problem.
- **Per signal:** its KAVACH id, type, aspects, calling-on, stop board,
  CUR_SIG_INFO.
- **MA by aspect:** the table (Red / calling-on 1 section, Yellow 2,
  Double Yellow 3, Green 4, overridable per station), and the MA each
  route would get at each aspect, worked out live.
- **Done when:** for every route and aspect of LPI, the MA shown equals
  what the engine answers, and equals real SLRPs where `replay/` holds one.

### 462: Track profile I (SSP and gradient)

- **Rows per route,** each from the route start (m) and its length.
  - **Generate** makes one row the length of the route.
  - **Split here** cuts a row at a distance.
  - **Paste** takes Excel cells.
  - Values are entered with their unit (km/h, ‰; as Packet Maker does,
    342).
- **Gradient:** rise / fall as the gradient plan writes it (456's rule: R
  uphill, F and level downhill).
- **Checks:**
  - the rows add up to the route's length;
  - a speed is on the field's step;
  - a value too wide for its field is refused by name.
- **Done when:** LPI's profile from HBL's sheet (456) shows here row for
  row, and the SLRP built from it equals the real one sub-packet for
  sub-packet.

### 463: Track profile II (LC gates, turnouts, tag linking, track conditions)

- **LC gates:** gate id, distance, manned / unmanned, auto-whistle.
  Checked against the layout's LC tags (type 10): gate id and distance to
  the gate as their bits say.
- **Turnouts:** proposed from the route's reverse points (location, speed
  over them), then edited.
- **Tag linking:** generated from the route's tags: distance from the tag
  before, duplicate direction, and junction / adjustment tags at their
  location 1 or 2 (455's rule).
- **Track conditions:** the schema's kinds (neutral section, …) with start
  and length.
- **Done when:** each sub-packet built from these tables decodes back to
  the same rows, and LPI's real SLRP sub-packets are reproduced.

### 464: TSR

- **TSR rows per route:** id, start, length, speed (5 km/h steps), class,
  whistle, and TSR_STATUS on / off for the live station.
- **The TSR route table** (OHE poles, 457), edited here or read from the
  firm's .docx. A TSR given "from pole to pole" is placed on every route
  it covers.
- **Done when:** LPI's TSR route table round-trips, and a TSR from pole to
  pole gives the rows 457's tests give.

### 465: Next station, hand-over, stretch

- **The border table:** the next station's ID, the border tag,
  NEXT_STN_COMM, APPR_STN_ILC_IBS_ID.
- **A stretch made in the UI:** stations in order, saved as stretch.json
  (458's file).
- **Checks:** a border tag that is a station's own signal foot (SKP's S6
  and S35, seen in 458) is flagged.
- **Done when:** the GGD 521 → LPI 527 stretch is made in the UI, and the
  458 dry run gives the same hand-overs.

### 466: A firm's sheets opened in the app

- **Open ▸ A firm's KAVACH sheets…** takes a folder.
  - It runs `scripts/kavach_sheets.py`, which needs Python 3 and
    `pdftotext`.
  - Where those are missing (the offline office PC, RHEL), it reads the
    `.html` / `.json` the script wrote on another machine.
  - It never half-reads: a sheet it cannot read is named.
- The script's notes go to the Check step as "Sheet: …" (454).
- **Decision needed:** ship Python and poppler with the Windows / RHEL
  packages, or keep the script on a separate machine.

### 467: The station's own parameters

- **The Station step holds everything the station's packets carry that is
  not a route:**
  - station ID and STN_VERSION;
  - the fields of SLRP / AAP / AEP not from the ToC (each listed from the
    schema, so none is forgotten);
  - the session key it signs with (SessionKeyStore, owned by MainWindow).
- **Timing:** FRAME_NUM (seconds since midnight + 1) and the accept window
  (> 4 s old or ≤ −2 s early is rejected), shown so a mis-set clock is
  seen before a run.

### 468: Whole-station check and data pack

- **Check:** one list for layout, KAVACH data and parameters.
- **Data pack:** export the station as the firm's sheets lay it out (tag
  data, ToC, track profile, TSR table) to PDF, for scrutiny by
  signatories.
- **Compare with a firm's pack:** this station against the sheets it was
  read from, per field.
- **Milestone M1:** a complete station, made by hand or from a firm's
  sheets, that the engine answers from.

---

## 3. Phase B: the test rig (a VCC on the bench; DLConsole is the rest)

One **Run** step (the last of the guided steps) holds the whole rig.

```
         ┌──────────────── DLConsole ────────────────┐
 radio   │ Station(s): SLRP / AAP / AEP / SoS        │
 ◀──────▶│ Other locos: their LSRP / ARP / SoS       │
         │                                           │
 serial  │ Loco motion → RFID readers (2)            │       ┌─────────┐
 ◀───────│             → speed sensors (2)           │──────▶│   VCC   │
 UDP     │ Cab I/O: DIP inputs, DOP feedback, analog │◀─────▶│ (bench) │
 ◀──────▶│ BIU: BIU_CMD in → pressures out           │       └─────────┘
         │                                           │
 UDP     │ NMS sink: health / fault / RSSI           │◀────── VCC
 ◀───────│ Logger: @… logs, IOA serial logs          │◀────── VCC
         │                                           │
         │ One clock · one scenario · one recording  │
         └───────────────────────────────────────────┘
```

### 470: The Run step and the rig

- **`TestRig`** (core, testable without a window):
  - which parts run;
  - their endpoints, taken from the Loco Configuration profile (a loco's
    four VCC IPs, ports 50001 / 51234 / …);
  - one rig clock;
  - saved as `<station>.rig.json` beside the station.
- **The Run step:**
  - a target: Dry run (no VCC), VCC on the bench, or Full loco;
  - a checklist with a light per link (radio, RFID serial, speed, cab
    I/O, analog, BIU, NMS, logger);
  - **Start all / Stop all**.
- **Done when:** a dry run starts and stops every part with no VCC, and
  the session's sockets and ports are all released at Stop.

### 471: The station in the rig

- **Answers from the open station,** not a file path: Packet Maker's
  engine, headless, answers ARP → AAP and LSRP → SLRP.
- **SLRP cadence:** the cadence and profile sends as station 527 did
  (449), measured from `replay/`.
- Signed with the station's key.
- **The live drawing** follows every answer (451).
- **Done when:** LPI's real ARP / LSRP frames from `replay/` are answered
  as before (449's tests), through the rig rather than Packet Maker's
  dialog.

### 472: The loco moving over the station's routes

- **The Loco Simulator's path is the route the station set,** through the
  points as they lie in the live state, not one line (411). A route
  change mid-run is followed.
- **Its inputs:** start tag, direction, train length, loco ID.
- **What it sends:** the RFID frames of those tags (both readers, the
  duplicate 4 m on), and the speed sensors' messages (391, 408).
- **Faults per tag:** late, twice, bad CRC (405), set from the scenario.
- **Done when:** a dry run over LPI's S43 → A900 sends tags 18, 906, 898,
  900 at their locations, in order, and nothing from the other routes.

### 473: The driver and the cab

- **Cab I/O Simulator driven by the rig:**
  - cab active, direction handle, mode select, acknowledge, SIFA, at times
    or tags from the scenario;
  - the old cab and SIFA layouts (365).
- **The driver's speed:** a target speed with the loco's acceleration and
  braking limits. The motion and speed sensors follow it.
- **The DMI** shown live (the VCC's DMI frames).
- **Done when:** a scripted start (cab on, forward, mode, acknowledge)
  gives the same DIP frames as the real VCC ↔ IOA frames 325 checked
  against.

### 474: The brake loop closed

- **The loop:** the VCC's BIU_CMD → BIU sim → brake pressures (BP, BC,
  FP) on the analog card → VCC.
- **The loco slows from that air** (398, 418–421), not from a guessed
  deceleration.
- **Brake outputs:** LOUT shown. The brake chain (453) is drawn during the
  run.
- **Done when:** a VCC's recorded @biu_cmd sequence from `replay/` slows
  the simulated loco along the curve 418–421's tests fix, and each link of
  the chain is shown.

### 475: Other locos

- **Virtual locos:** ID, start tag, route, speed, direction, length. Each:
  - sends its ARP / LSRP to the station, so the MA is cut behind a loco
    ahead (427), and Head-on / Rear-end are seen (445);
  - sends its regular packet over the radio path to the VCC under test.
    This depends on decision 2 (below).
- **Scenarios:** a loco stopped ahead, a loco coming the other way on the
  same line, a loco on the adjacent line.
- **Done when:** a virtual loco stopped ahead on LPI's route cuts the MA
  where 427's tests cut it.

### 476: Emergencies

- **From virtual locos:** LOCO_SOS.
- **From the station:**
  - SoS for SPAD, Head-on, Rear-end, Station General, Shunt violation,
    Foreign RFID (426, 445);
  - the **Additional Emergency Packet (AEP)** with its sub-packets, as the
    schema has them. The encoder refuses what it cannot emit by name.
- **Buttons in the Run step**, and the same actions from the scenario.
- **Done when:** each SoS / AEP sent decodes back to what was set, and
  real AEP / SoS frames in `replay/` (where any exist) are reproduced.

### 477: NMS and the data logger

- **The rig receives** the VCC's NMS packets (health, fault, RSSI), its
  logs (@… on UDP 51234), and the IOA serial logs.
- **One recording (.dlr)** holds all of them, together with what the
  station answered and every simulator sent.
- **Per link:** counters, lost frames (214), and the clock-skew alarm
  (93).
- **Done when:** a dry run's recording plays back in the .dlr viewer
  (368) with the DMI, radio chart and the simulators' sends in step.

### 478: Scenarios

- **A scenario file:** a timeline of actions, each one triggered at a
  time, at a tag, at a speed, or on an event. The actions:
  - set an aspect, throw a point, occupy a circuit;
  - TSR on / off;
  - start / stop a virtual loco;
  - SoS / AEP;
  - a cab input, a target speed;
  - an RFID fault;
  - radio down for N s.
- **Reproducible:** a scenario runs the same way every time in a dry run,
  so the scenario itself is checked before a VCC sees it.
- **Built-in scenarios** taken from the test catalog's clauses (231,
  263).
- **Done when:** two dry runs of one scenario give byte-identical sends
  (the rig clock driven by the test).

### 479: What was observed, and the run report

- **Per step of a scenario:** what the station sent and what the loco did:
  - MA end and permitted speed;
  - where the brakes came (FSB / EB, normal brake);
  - where the loco stopped;
  - a signal passed at Red.

  All in "observed" words.
- **In the test catalog's run** (306): each case gets its evidence (time,
  frames). The verdict field is left to the signatory.
- **The run's report pack** (incident / review pack, 186, 370).
- **Milestone M2:** a dry run of the whole station, scenario-driven,
  recorded and reported.

---

## 4. Phase C: real hardware

### 480: A real VCC on the bench

- **Connection wizard:** a self-test per link before Start:
  - the VCC answers ping;
  - its logs arrive;
  - its ARP is heard;
  - the RFID serial port opens;
  - the IOA link answers readback (325);
  - the analog card frames are taken;
  - the BIU answers.
- **The setup the OS needs,** said plainly: the multicast route and the
  firewall, as kav_qt's README does.
- **Saved bench profiles.**
- **Milestone M3:** the first scenario run against a real VCC.

### 481: Real BIU, real cab I/O

- **Per link, simulated or real.** A real part is observed instead of
  driven: the real BIU over CAN through the BIU firmware (376), the real
  IOA cards.
- The Brake Test Bench follow-ups queued in `docs/BRAKE_BENCH_NEXT.md`
  (real BIU frames, scenarios, recording, verdicts) land here.

### 482: A whole loco

- **DLConsole is** the station or stations, the other locos and the NMS /
  logger. The loco's own sensors are real.
- **A stretch** with hand-overs (458) in the rig.
- **Blocked by decision 1:** the radio path to a real loco.
- **Milestone M4:** a loco run over a station made in DLConsole.

### 483: Soak, timing, packages

- A two-hour run: memory and CPU flat, no frames lost by DLConsole.
- The FRAME_NUM window and clock skew watched throughout.
- Windows and RHEL 8 / 9 offline packages carrying everything a run needs.

### 484: Operator guide and a demo station

- `docs/` walkthrough from New station to a reported run.
- A built-in demo station (New station's) with its routes, profile and
  three scenarios, so a first run needs no file at all.

---

## 5. Decisions needed (and the patch each one blocks)

1. **The radio path to a real loco (482).** What carries DLConsole's
   station packets to the loco's radio: a station radio unit taking UDP,
   or a gateway? Which device and which port?
2. **Other locos' packets to the VCC under test (475).** On the bench, are
   other locos' LSRPs sent to the VCC on the same port as the station's
   packets? On a real loco, through the same radio path?
3. **Keys (467, 471).** Where the station's session / authentication keys
   come from for a real VCC, and whether DLConsole may hold them on the
   bench PC.
4. **Python and poppler in the packages (466),** or the sheets read on
   another machine.
5. **Cab I/O on the bench (473):** serial or UDP, and which cab layout
   (old or SIFA) first.
6. **The first scenarios (478):** which RDSO test cases come first.

---

## 6. Order and milestones

| Milestone | Patches | What a user can do |
|---|---|---|
| (done) | 459 | Build a station's layout step by step, New station |
| M1 | 460–468 | A complete station, by hand or from a firm's sheets, checked and exported |
| M2 | 470–479 | A dry run of the whole station: station, loco, cab, brakes, other locos, emergencies, scenarios, recording, report |
| M3 | 480–481 | The same against a real VCC on the bench, with real BIU / IOA where wanted |
| M4 | 482–484 | A whole loco against DLConsole's stations; soak-tested; packaged; documented |

The patch numbers are a plan. A patch that proves too big is split and
numbered on, and a decision above can reorder them. Each patch's scope is
confirmed with Abhinav before it starts.
