# Session 85 — Serial port terminal (QCom-style), feeding the console

Only the VCC logs over Ethernet; the IOA's input, output and analog logs come
out of a serial port. **Tools ▸ Serial Port Terminal… (Ctrl+Alt+S)** opens
one port, shows its traffic like QCom does, and can feed its lines into the
console, where they are handled like UDP traffic.

## 1. Port and line settings

- **Port:**
  - enumerated, showing name and description (e.g. "COM3 — USB Serial Port");
  - ⟳ to look again;
  - a path can be typed if it isn't listed.
- **Baud:** the standard rates, or any rate typed in.
- **Data bits** 5–8.
- **Parity:** None / Even / Odd / Mark / Space.
- **Stop bits:** 1 / 1.5 / 2.
- **Flow control:** None / RTS/CTS / XON/XOFF.
- **DTR and RTS lines.** Under RTS/CTS the driver owns RTS, so the switch is ignored.
- **Open / Close:**
  - the line settings are locked while the port is open;
  - the title and status show what is open, e.g. "COM3 115200 8N1".
- **A refused setting is never silently swapped.** If the driver opens the
  port but refuses a setting (a parity it can't do, say), the port is closed
  again and the status says "opened, but refused 9600 8E1: …". It never runs
  on at some other setting.
- **Cable pulled / adapter gone:** the port closes and the status says why.
- **Several windows can be open at once, one per port** (input, output, analog).
- The last settings are remembered (`serial/…`).

## 2. Receive view

- **Text or Hex.** Hex shows the bytes in the chunks they arrived in.
- **Timestamps.** Each line is stamped with the time its **first** byte
  arrived, so a line that trickles in at 9600 baud belongs to when the card
  started printing it.
- **Line endings.** LF, CR+LF and CR alone all end a line, and a CR/LF pair
  split across two reads is still one line end.
- **Idle flush.** A line with no newline is delivered once the port has been
  quiet for 300 ms after its last byte (a prompt, a card that stopped
  mid-line). A line still arriving is not cut.
- **Show sent** echoes transmissions as `TX> …`.
- **Hold** freezes the view so it can be read. What arrives meanwhile is
  kept, not dropped; the status counts it ("holding 3 lines") and it appears
  in order on release. Logging and feeding carry on while held.
- **Clear.**
- **Log to file…** appends what the view shows to a file (default name
  `serial_<port>_<date>_<time>.log`).
- The view keeps the last 20,000 lines.
- **Counters:** RX bytes, TX bytes, lines fed to the console, with a Reset.

## 3. Send

- **Text** with escapes: `\r \n \t \\ \xHH`.
- **Hex** bytes: `AA 55 0d0A`, `0xAA,0x55`, … Bad hex (an odd digit count,
  a non-hex character) is **refused and nothing is sent**, rather than
  sending something other than what was typed.
- **Line end:** none / CR / LF / CR+LF.
- **History** of the last 30 sends, newest first, remembered.
- **Repeat every N ms.** It stops itself if the port closes.
- **Send file…**

## 4. Feed console

**Feed console** (on by default) puts every received line into the console
through the same pipeline as a UDP datagram, in a tab named "Serial COM3".
So IOA logs get:
- the same decoding (`@dip`, `@dop`, `@analog_top` / `@analog_bottom`, …);
- colour rules, find, query, pins, plots, the DMI time travel;
- recording and `.dlr` session files.

Each line keeps its first-byte time as its arrival time.

How the tab is formed:
- a synthesised header: source 254, destination 101, message 1, and a kvch
  id derived from the port name;
- the kvch id is stable across runs and machines, and "COM3" and "com3" are
  the same port;
- so the same port always lands in the same tab.

Fed lines are not counted as UDP receiver traffic (its queue accounting is
unchanged).

## Files

- **New:**
  - `seriallink.h/.cpp`: the port as a plain object with no window, so the
    mission driver's RFID-over-TTL output can reuse it. It contains:
    - `SerialConfig` with save/load and `summary()`;
    - `SerialLineSplitter`;
    - the hex parse/print and escape helpers.
  - `serialconsolewindow.h/.cpp`: the window.
- **`messagedispatcher.h/.cpp`:** `ingestLocal()` (lines from another route into
  the normal pipeline, with a tab name) and `drainNow()` for tests.
- **`mainwindow.cpp`:** Tools ▸ Serial Port Terminal… (Ctrl+Alt+S).
- **Project files:**
  - `QT += serialport` in `DLConsole.pro`, `tests.pro`, `menuaudit.pro` and `shot.pro`;
  - on Linux, `tests.pro` also links `-lutil` for `openpty()`.

**Windows build:** needs the **Qt Serial Port** module for Qt 5.15. It
ships with the standard Qt 5.15 installs; if qmake reports an unknown module
"serialport", add it in the Qt maintenance tool.

## Tests

**`session85` (48)**, all run in the test suite:

- **Line splitting:**
  - LF, CR+LF and CR alone;
  - a CR/LF split across reads;
  - blank lines;
  - over-long lines split;
  - the idle flush measured from the last byte, not the first.
- **Hex parse and print:** refusals for an odd digit count and for a non-hex character.
- **Escapes.**
- **Line-setting summaries.** Settings save and load unchanged, and nonsense in the ini falls back to 115200 8N1.
- **`ingestLocal`, with real `@dop` lines (the IOA's output log) from `replay/`:**
  - they decode as `@dop`;
  - they land in the "Serial COM3" tab;
  - "COM3" and "com3" give the same tab;
  - the entry's own tab key agrees.
- **End to end through a pseudo-terminal (Linux), with the pty standing in for the card:**
  - a refused parity is refused, not swapped;
  - the port opens at 9600 8N1;
  - a log line split across writes arrives whole, in the terminal and in the console tab, in order;
  - RX and TX counts are exact;
  - CR+LF text, hex bytes and refused hex behave as described;
  - echo and history, newest first;
  - a prompt with no newline appears once the line goes quiet;
  - Hold keeps the line back from the view, the console still gets it, and it appears on release;
  - the hex view;
  - close, and no sending on a closed port;
  - a missing port says why it did not open.

## verify.sh

Each stage run with the script's own commands, **0 stages failed**:
- validators 11/11;
- unit suite **146 suites / 4453 checks** (145 / 4405 before);
- menu audit **136 ok** (Tools ▸ Serial Port Terminal on Ctrl+Alt+S);
- headless smoke alive (500 datagrams).

**Not seen yet:** a real USB–serial adapter or a real IOA card, and Windows
COM ports. The pty checks the port handling, but not a driver's quirks.
