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
  source reading (`/utf-8`), file names Windows forbids, greedy `\x` escapes.
  Windows tests run on the native platform (offscreen draws no text there).
- **Qt 6 is gated too** (CI job `linux-qt6`, since patch 127). Locally:
  `QMAKE=/opt/homebrew/opt/qtbase/bin/qmake VERIFY_OUT=$PWD/build-verify-qt6 ./verify.sh`.
  Traps seen: `QStringRef`/`splitRef`, `QRegExp`, `qBound<T>` with mixed
  args, `std::min(int, x.size())` (qsizetype), `QFont::resolve()`,
  `findChild<T>` on a T without `Q_OBJECT`, `QSerialPort::ParityError`,
  and `restoreGeometry` shrinking a window to its screen.

## The gate — non-negotiable

Nothing is committed as a patch unless **all** of these are green:

```
./verify.sh            # validators, unit suite, menu audit, headless smoke
```

- 11 Python golden validators (`schema/validate_*.py`) against `replay/`.
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

## Queued features (in this order)

1. **Incident report pack** — pick a time window or moment; one HTML/PDF:
   the DMI as the loco pilot saw it at key moments (DMI time travel), the
   speed/permitted/target plot, mode changes, EB/FSB applications, reject
   findings, the raw frames around the event.
2. **Two-loco view** — both locos' positions and speeds and the gap between
   them over time, with SoS, collision-target and head-on/rear-end events.
3. **Track diagram by absolute location** — a linear line diagram (no map,
   no internet) from RFID tags, signals and the MA end; the loco moving
   along it over time, events pinned where they happened.

Confirm each feature's scope with Abhinav before building it.
