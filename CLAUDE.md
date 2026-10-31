# DLConsole — notes for Claude Code

Qt 5.15 / C++17 diagnostic console for the Kavach ATP system (RDSO SIF-0533
v4.22): live UDP (VCC logs) and serial (IOA input/output/analog logs)
capture, schema-driven decoding (`schema/kavach.xml`), replay, DMI and cab
views, Packet Maker, Firmware Flasher, Loco Configuration.
Primary target: **Windows, Qt 5.15**. Also built on Linux (GCC) and on
**Qt 6** (6.4 in CI, 6.11 on the Mac; 6.9 on the deployment machine): code
must compile on both — see `tests/test_qtcompat.cpp` for the patterns.

History until patch 96 was developed in claude.ai chats and delivered as
zips; `CHANGELOG.md` has one section per patch (newest first) and `git log`
has the commits. Read the top few CHANGELOG sections before starting.

## Build layout

- `dlcore.pri` — **the one list of shared sources.** A new source file is
  added here, and only here.
- `core/core.pro` — builds them once into the static library `dlcore`
  (`<build>/core/libdlcore.a`, `dlcore.lib` with MSVC).
- `dlcore_link.pri` — included by every program: headers, Qt modules, the
  serial switch, resources (`images.qrc`, `lococonfig.qrc` — kept OUT of the
  library on purpose), the link.
- `app/app.pro` — `main.cpp` + the library. The exe lands in the **main build
  folder** (`<build>/DLConsole.exe`, via `DESTDIR`).
- `DLConsole.pro` — subdirs: `core`, `app`; `CONFIG+=with_tests` adds
  `dltests` and `menuaudit`.
- `serial.pri` — the serial terminal is optional: without Qt Serial Port (or
  with `CONFIG+=no_serial`) everything still builds and runs on Ethernet.
- **CI builds and tests on Windows (MSVC 2019, Qt 5.15.2) and runs the full
  gate on Linux** (`.github/workflows/build.yml`, since patch 116). Watch it
  after a push: `gh run list`, `gh run view <id> --log-failed`. Windows-only
  traps it has caught: GCC `__attribute__`, `class`/`struct` mismatches
  (MSVC mangles them differently: now an error on clang/GCC), non-UTF-8
  source reading (`/utf-8`), file names Windows forbids, greedy `\x` escapes,
  `QVector<T>({ a })` / `({ a, b })` (MSVC: the size constructors; write
  `QVector<T>{ … }`, in parentheses inside `CHECK`).
  Windows tests run on the native platform (offscreen draws no text there).
- **Qt 6 is gated too** (CI job `linux-qt6`, since patch 127). Locally:
  `QMAKE=/opt/homebrew/opt/qtbase/bin/qmake VERIFY_OUT=$PWD/build-verify-qt6 ./verify.sh`.
  Plain `./verify.sh` uses the `qmake` on PATH, which on the Mac is **Qt 6
  too**. The local Qt 5.15 gate is
  `QMAKE=/opt/homebrew/opt/qt@5/bin/qmake VERIFY_OUT=$PWD/build-verify-qt5 ./verify.sh`
  (patch 266: 257-265 were "Qt 5" gated on Qt 6; CI caught the Qt 5 break).
  Traps seen: `QStringRef`/`splitRef`, `QRegExp`, `qBound<T>` with mixed
  args, `std::min(int, x.size())` (qsizetype), `QFont::resolve()`,
  `findChild<T>` on a T without `Q_OBJECT`, `QSerialPort::ParityError`,
  and `restoreGeometry` shrinking a window to its screen.

## The gate — non-negotiable

Nothing is committed as a patch unless **all** of these are green:

```
./verify.sh            # validators, unit suite, menu audit, headless smoke
```

- 18 Python golden validators (`schema/validate_*.py`) against `replay/`
  (`validate_rdir.py` and `validate_dlcount.py` against synthetic fixtures
  until real `@rdir` / `@dlcount` captures exist; `@sos` and `@dllog` are
  real since patch 273; `validate_kavach_sheets.py` against extracts of
  HBL's KAVACH sheets, and the whole pack when the local PDFs are there).
- Unit suite `dltests` (152 suites / 4898 checks at patch 96). Run one suite
  with `cd tests && ../build-verify/tests/dltests <suite>`.
- Menu audit (`tests/menuaudit_main.cpp`, 138 checks): builds the real
  MainWindow and walks every menu.
- Headless smoke: the app runs offscreen and takes 500 real datagrams.
- Tests use **real frames from `replay/`**, not invented bytes, wherever a
  real frame exists. New behaviour gets a `tests/test_session<N>.cpp` suite.
- A flaky test is a bug until proven otherwise (patch 93 found a real clock
  race behind one).

## Conventions

- **One patch = one commit (or a few), tagged `patch-<N>`**, with a CHANGELOG
  section `<a id="session-N"></a>` / `## Session N — title` at the top:
  what changed and why, what the operator sees, files, tests, gate result.
- Patch numbers continue from 96.
- Changes are scoped: confirm scope with Abhinav when a request is
  ambiguous; do not expand it unasked. State plainly what was NOT tested.
- **Schema is ground truth.** Load the active external `kavach.xml`; verify
  decoded values bit by bit; signed vs unsigned matters.
- **The encoder refuses rather than guesses** — a packet it cannot emit
  correctly is refused by name (e.g. typed float/char fields, `<repeat>`).
- **Verdict language:** tooling reports what the receiver would do / what
  was observed; verdicts belong to signatories ("would not be processed",
  never "invalid").
- UI colours go through `UiColor` (contrast-audited, ≥ 4.5:1; High Contrast
  7:1). Status messages through `StatusLine` (`ok/warn/fail`).
- Session-state objects are **owned by MainWindow and handed out**
  (`FrameNumberWatch`, `TabTags`, `SessionKeyStore` — no `instance()`).
  `FieldCatalog` stays a process-wide singleton by decision (read-only
  reference data, like `kavachSchema()`).

## Domain notes that bite

- Only VCC logs arrive over Ethernet; IOA input/output/analog logs arrive
  only over serial (Tools ▸ Serial Port Terminal feeds them into tabs).
- FRAME_NUM = seconds since midnight + 1; accept window: > 4 s old rejected,
  ≤ −2 s early rejected.
- SLRP: TSR entries act only when `TSR_STATUS == 2`; `REF_PROF_ID` 0 = route
  unknown; start signal = last ref RFID abs loc ± `DIST_PKT_START` along the
  direction of travel. Silent-ignore conditions are annotated in the decoder.
- lsb-first packets (DMI, RFID, CCSYS, DLSYS, ...) carry `<crc>` elements;
  DMI frames are `AA AA 74 … BB BB` (116 B).
- Turnout decoding is owned by another person — out of scope here.

## Features already built (do not offer them as new)

The three features once queued here are all done:

- **Incident report pack:** sessions 97, 133, 186.
  Tools ▸ Monitor ▸ Reports ▸ Incident report….
- **Two-loco view:** sessions 98, 132, 172, 186.
- **Track diagram by absolute location:** sessions 99, 131, 207, 209.
- **Remote review** (testing at the site, review elsewhere): the review tools
  in the .dlr viewer (368), notes beside the capture (369), the review pack
  (370), Play with the DMI (371); the Monitor tools (372) and the test cases
  (373) over a recording, two runs in step (374), what changed on the DMI
  (375). How-to: `docs/REMOTE_REVIEW.md`.
- **Stationary KAVACH from data** (a miniature station answering the loco):
  routes as Table-of-Control rows, MA in sections from the aspects, profile
  cut at the MA end, live state in Packet Maker ▸ Station… (422); edited in
  Station Layout ▸ KAVACH (423); the routes and the MA drawn on the layout
  (424); a route's tags picked by clicking the drawing (425); SoS SPAD to a
  loco past a Red signal (426); the MA cut behind a loco ahead, Rear-end SoS
  echoed (427); the station live on the drawing: aspects, locos, MAs (428);
  every tag of a repeated name drawn where it is (429); a layout checked
  against the station's tag-TIN layout drawing (430, LPI from Annexure-H).
  The station's own signalling from the locos' ARPs / LSRPs
  (`stationinterlock.{h,cpp}`): track circuits occupied by the locos (441);
  routes locked, signals back to Red, routes released (442); automatic
  signals (443); conflicting routes refused, signals held at Red (444); SoS
  Head-on, Shunt violation, Foreign RFID, Station General (445); shortened
  MA asked for (446); hand-over to the next station (447); train-length
  times (448); answering as station 527 did (449).
  `docs/STATION_KAVACH.md`. Next steps proposed there under "Not yet".
- **Station XML** (`station_<id>.xml`, kav_sim's "kavsite-xml 1") opened and
  saved by Station Layout (431); signals drawn as on the SIP / tag-TIN
  sheets, 2-4 aspects, calling-on, shunt (432); a signal's heads from the
  drawing, Tables (Ctrl+T) to give the drawing the window (433).
  Signals, route indicators, points and track circuits changed on the
  drawing, sent to Packet Maker's live station or kept as a what-if (435,
  436); a form beside the drawing (437); a new signal with its heads (438);
  the drawing exported on paper with a title block (439).
- **A firm's KAVACH sheets read into a station** (`scripts/kavach_sheets.py`,
  `docs/KAVACH_SHEETS.md`): the RFID tag data, every field checked against
  its bits (454); the Table of Control as Stationary KAVACH routes, folders
  read together as one stretch, hand-overs from the border table (455); the
  track profile onto those routes, as station 527 sends it (456); the TSR
  route table's OHE poles, a TSR given from pole to pole (457). HBL's 2023 PDFs (GGD … LPI) stay local in
  `tests/fixtures/kavach_sheets/` (git-ignored).
- **Themes:** 14, six of them dark: Dark, Nord, Mocha, and One Dark,
  Solarized Dark, Gruvbox Dark (434).

Before proposing a feature, search `CHANGELOG.md` for it. **Queued:** the
Brake Test Bench follow-ups for the hardware session (real BIU frames,
scenarios from test cases, recording the bench's traffic, verdicts; replies to
all 4 VCCs and saved presets are done, 361-363) and the open BIU-firmware
decisions: see `docs/BRAKE_BENCH_NEXT.md`. Confirm each new feature's scope with
Abhinav before building it.
