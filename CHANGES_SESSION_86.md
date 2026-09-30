# Session 86 — The console never depends on a serial port

Only the IOA logs come over serial. The VCC logs come over Ethernet, and the
console must start and run on them alone.

## 1. At run time: nothing serial until asked

This was already true of session 85, and is now locked by the menu audit:

- **At startup:** DLConsole creates no serial port object, link or terminal.
  - Ethernet reception starts exactly as before.
  - No serial port needs to exist, be free, or be plugged in.
- **Opening Tools ▸ Serial Port Terminal** only makes the window.
  - The port stays closed until **Open** is pressed.
  - Settings are remembered, but a port is never re-opened automatically.
- **A port that fails** (refused setting, cable pulled, adapter gone) closes
  that one terminal and says why. UDP reception and every other window carry on.

## 2. At build time: the serial module is optional

**New `serial.pri`**, included by `DLConsole.pro`, `tests.pro`,
`menuaudit.pro` and `shot.pro`.
- **With Qt's Serial Port module** (the normal case):
  - the terminal is built in and `DL_HAVE_SERIAL` is defined;
  - `test_session85` and its `-lutil` are added.
- **Without it**, or with `qmake CONFIG+=no_serial`:
  - DLConsole builds and runs on Ethernet alone;
  - the binary does not even link `Qt5SerialPort`, so a missing
    `Qt5SerialPort.dll` can never stop the console starting;
  - Tools ▸ Serial Port Terminal is still in the menu, disabled, and its
    tooltip says why.

## 3. Terminal polish found while rendering it

- **The receive view now uses the console's own monospace font**
  (`UiStyle::monoFont()`, which follows View ▸ Text size), so hex columns
  line up. It was the system fixed font before, and did not zoom.
- **The Port box is wider** (240 px), so "COM3 — USB Serial Port" fits.
- **The console tab is named by the port's short name**, e.g. "Serial ttyUSB0"
  rather than "Serial /dev/ttyUSB0". Windows COM names are unchanged.

## 4. Design images

- `docs/images/serial_terminal_light.png`
- `docs/images/serial_terminal_dark.png`

These are the real window, rendered offscreen at 1180×720, open on a
pseudo-terminal standing in for an IOA card:
- it prints real `@dop1`/`@dop2` lines from `replay/`;
- one `DIAG?` has been sent (the `TX>` echo);
- Feed console is on (26 lines fed to the "Serial ttyUSB0" tab).

The Ayu Light and Ayu Dark themes are shown. The view font is DejaVu Sans
Mono here; on Windows it is the system fixed font.

## Files

- **New:** `serial.pri`, `docs/images/serial_terminal_{light,dark}.png`.
- `mainwindow.cpp`: the terminal action is guarded by `DL_HAVE_SERIAL`
  (disabled, with an explanation, otherwise).
- `serialconsolewindow.cpp`: the font, the port width and the short tab name.
- `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro`, `tests/shot.pro`:
  the serial sources, `QT += serialport` and `test_session85` moved behind
  `serial.pri`.
- `tests/menuaudit_main.cpp` gains three checks:
  - the console starts with no serial port, terminal or link;
  - opening the terminal opens no port;
  - built without serial, the action is present and disabled.

## verify.sh

Each stage run with the script's own commands, **0 stages failed**:
- validators 11/11;
- unit suite **146 suites / 4453 checks**;
- menu audit **138 ok**;
- headless smoke alive (500 datagrams).

**Also built and run WITHOUT the serial module** (`CONFIG+=no_serial`):
- the app starts, with no `libQt5SerialPort` in `ldd`;
- it takes the 500-datagram smoke;
- its menu audit is 137 ok, 0 failed.

**Flake seen once:** `lococonsolelive` failed two timing checks ("a field that
changed is highlighted", "and in bold while fresh") in one full-suite run on
the single-core build box. It passed 3/3 alone and in the next full run. That
suite was not touched here; it is worth a look if it recurs on your machine.
