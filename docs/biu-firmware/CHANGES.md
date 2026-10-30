# BIU interface: what changed and what callers must do

Against RDSO MP.0.01.00.31 Rev.05, Annexure-1 (CAN protocol) and Annexure-4 (test values).

Each change below says where in the spec it comes from. The clauses are the ones written in the source
comments and tests when the change was made (there is no copy of the spec in this repo); "clause not
recorded" means none was written down, and "not from the spec" means a code defect or tidy-up.

## Files

- `brake_pressure_interface_user.c` / `.h`: replace yours. Both are ASCII now; the old `.c` was Windows-1252.
- `biu_can_messages.h` (new): replaces the `STRUCT_LKAVACH_BIU_*` / `STRUCT_BIU_LKAVACH_*` block
  and `LKAVACH_BIU_COMMAND_MSG`. Delete those from the header they were in.

## Fixed

| # | Was | Now | Where in the spec |
|---|-----|-----|-------------------|
| 1 | A9/SA9 sent at 0.1 kg/cm2 per bit (`* 10`), truncated: BIU applied half (no brake -> BP 2.5) | 0.05 per bit, rounded (`biu_kgcm2_to_byte`) | Annexure-1 2.2.2 (command frame: A9 / SA9 reference bytes, 0.05 kg/cm2 per bit) |
| 2 | COB-ID `0x181` | `0x190` = 0x180 + BIU node 1 (0x10) | Annexure-1 1.2 (node IDs: BIU node 1 = 0x10), 2.2 (COB-IDs: command = 0x180 + node) |
| 3 | Bit-field structs memcpy'd; feedback structs unpacked (28 bytes for 19) | Frames built and read byte by byte with explicit shifts; no layout depends on the compiler | Annexure-1 2.2 (each frame's byte layout). Not a spec change: the old structs did not match it on this compiler |
| 4 | `process_biu_reply` copied into a local and dropped it | Dispatches 0x220 / 0x320 / 0x420, scales by 0.05, keeps each validity bit; `biu_status_get()` | Annexure-1 2.2 (status frames 0x220 / 0x320 / 0x420), 4 (use a pressure only while its validity bit is 1) |
| 5 | Settling time always the new command's `t_apply`: EB -> release judged at 5 s | Apply time when the command is at least as strong as the one before, the previous command's `t_release` when weaker | Annexure-4 (apply and release times per loco; clause not recorded). Apply vs release per direction is my reading |
| 6 | No table row -> "no malfunction"; zeroed range passed exactly 0.0; NULL deref in two prints | `BRAKE_CHECK_NO_SPEC`; validate returns 0; no deref | Not from the spec: a code defect |
| 7 | One `cmd_type` used as table row and as check kind | Brake and pressure (`PressureCmd`: `CMD_MR / CMD_BP / CMD_BC`) are separate arguments | Not from the spec: a code defect |
| 8 | Dynamic braking always 1, speed valid always 1, A9/SA9 valid always 1 | Dynamic braking from a hook (default 0); speed validity from a hook; A9 valid only <= 6.0, SA9 only <= 4.0; no row -> BP 0 on purpose, BC not valid | Annexure-1 2.2.2 (discrete byte 1: speed valid, dynamic braking; discrete byte 2: A9 valid 0-6.0, SA9 valid 0-4.0); 4 c (BIU applies emergency on losing KAVACH) |
| 9 | Fault / display codes never acked | Acked for one command cycle after each new code | Annexure-1 3 (BIU holds a fault / display code until KAVACH acknowledges it) |
| - | `ClockP_getTicks()` taken as ms | `ClockP_getTimeUsec() / 1000` (`BIU_NOW_MS`, overridable) | Not from the spec: TI SDK clock API |
| - | EB BP commanded as 0.15 (middle of "below 0.3") | 0 | Clause not recorded ("below 0.3" was the old comment's reading) |
| - | `1.02` double literal | `1.02f` | Not from the spec: code hygiene |
| - | Third table block commented WAG9 | WAP5 | Not from the spec: wrong comment |
| - | Unused `StdPressureSpec`, `MR_STD`, `BP_CHARGED_STD`, `BC_STD`, `STD_CMD_COUNT`, application CRC | Removed | Not from the spec: dead code |

## Second round (your answers)

- **Table to Annexure-4.** BC is NB 1.2 +/- 0.1 for every loco. FSB and EB are 3.5 +/- 0.1 in light engine
  (8.6 note) and 2.4 +/- 0.1 in formation (9.8); formation EB follows formation FSB, as it did before. WAP5 now
  matches the other locos (it had 2.16 and 5.15). The times are unchanged, except WAP5 formation EB `t_apply`,
  which goes from 5 to 9 s, in line with its FSB row. *Spec: Annexure-4 8.1, 9.1 (NB BC), 8.6 note (light
  engine FSB / EB BC), 9.8 (formation BC); the time tables' clauses were not recorded.*
- **Light engine brake has its own row** (`CMD_LE` = the VCC's brake type 5). The BIU spec has no LE command,
  so this row is its SA9 full application: BC 3.5 +/- 0.1 with BP left charged at 5.0 (Annexure-4 4.2, 2.4).
  This is my reading; it needs a check against how the VCC means the LE brake. *Spec: Annexure-4 4.2, 2.4.*
- **Settling per pressure.** Moving toward more braking (BP down, BC up) waits for the new command's apply
  time. Moving back waits for the previous command's release time. For LE -> NB, BP waits NB's apply and
  BC waits LE's release.
- **No table row -> BP 0** stays. It matches the spec: the BIU itself applies emergency when it loses KAVACH
  (Annexure-1 4 c).
- **Network management and heartbeat.**
  - `biu_link_init()` after power-on sends the boot-up message (0x720 [00]) and waits pre-operational.
    *Spec: Annexure-1 2.1 (boot-up), 1.2 / 2.2 (KAVACH node 0x20: heartbeat 0x700 + 0x20).*
  - `biu_link_tick()` (call it at least every 250 ms) sends the heartbeat (0x720, our state) every 500 ms.
    *Spec: Annexure-1 4 (500 ms heartbeat).*
  - The BIU's NMT on COB-ID 0x000 is handled: start, stop, pre-operational, reset node, reset communication.
    *Spec: Annexure-1 4 (the BIU is the network manager and starts KAVACH).*
  - `send_biu_command` sends nothing until we are operational. *Spec: Annexure-1 4 (pre-operational or
    stopped: heartbeat only); command every 250 ms, same clause.*
  - The BIU's heartbeat (0x710) is watched; `biu_link_up()` says whether it is fresh (1.5 s) and operational.
    *Spec: Annexure-1 2.2 (0x700 + BIU node 0x10). The 1.5 s timeout is not from the spec: it is three missed
    500 ms heartbeats, my choice.*
- **Speed** defaults to m/s x 100 (0.01 m/s on the wire), rounded. You round to 2 places already, so this matches.
  *Spec: Annexure-1 2.2.2 (speed, 0.01 m/s, lower byte first).*
- **Two links (third round, 2026-10-27).** The BIU is wired to the VCC's own CAN controller, so the frames
  go on the bus as CAN frames; a UDP link to the DLConsole Brake Test Bench stays for bench testing. Only the
  selected link (`biu_link_select`, CAN by default) is sent on and listened to; the frames are the same.
  - **CAN:** `biu_can_send_frame` hands over identifier = COB-ID (11-bit), DLC = n, and the n data bytes, with
    no header (the heartbeat has DLC 1, NMT 2, data frames 8). The send itself is the one marked line in that
    function: put your CAN driver's send there, or define `BIU_CAN_SEND(cob_id, data, length)`. Until then nothing goes
    on the bus. Your CAN receive calls `process_biu_can_frame(cob_id, data, dlc)`.
    *Spec: Annexure-1 2.2 (COB-IDs and frame data); the identifier and length are CAN's own fields.*
  - **UDP (DLConsole bench):** `biu_udp_send_frame` / `process_biu_udp_frame`, one frame per datagram,
    `[COB-ID lo][COB-ID hi][3 + n][n bytes]`, to 192.168.25.126:51235 by default (`BIU_SIM_IP0..3` /
    `BIU_SIM_PORT`). *Not from the spec: the 3-byte header is the bench's format, because a datagram has no CAN
    identifier or DLC; it came from your original `brake_pressure_interface_user.c`.*
  - The two paths share no send or receive code, only the frame contents and the state they update.

## Fourth round: copies to DLConsole (2026-10-30, DLConsole patch 419)

- **Every BIU frame, both ways, can also go to DLConsole** as a capture packet, like `@dmi` or `@lout`, through
  your `UDP_SendCapturePkt` (utility.c) to the log port 51234. The real BIU stays on CAN and sees nothing
  different; the Brake Test Bench and port 51235 are not needed for it. Each copy holds the frame's data bytes
  as they are on CAN, no COB-ID or length:

  | Frame | Way | Capture id (utility.h) | Token | Bytes |
  |-------|-----|------------------------|-------|-------|
  | `0x190` command | sent | `LOG_PKT_BIU_CMD` | `biu_cmd` | 8 |
  | `0x720` our heartbeat | sent | `LOG_PKT_BIU_BEAT` | `biu_beat` | 1 |
  | `0x000` NMT | received | `LOG_PKT_BIU_NMT` | `biu_nmt` | 2 |
  | `0x710` BIU heartbeat | received | `LOG_PKT_BIU_BEATRX` | `biu_beatrx` | 1 |
  | `0x220` pressures | received | `LOG_PKT_BIU_PRESS` | `biu_press` | 8 |
  | `0x320` codes | received | `LOG_PKT_BIU_CODES` | `biu_codes` | 8 |
  | `0x420` discretes | received | `LOG_PKT_BIU_DISC` | `biu_disc` | 8 |

  A frame received is copied whether or not it was taken (an NMT for another node, a short frame), so the log
  shows what arrived. Only the selected link's frames are copied. *Not from the spec: a test aid.*
- **Two switches.** `biu_dlconsole_mirror(1 / 0)` at run time, **off** by default. `BIU_DLCONSOLE_MIRROR 0` at
  build time leaves the copies out (then `utility.h` is not included and `biu_dlconsole_mirror` does nothing).
- **Traffic:** about 20 copies a second with the BIU running (command 4, heartbeats 2 + 2, status 3 x 4).

## What callers must change

1. **Define `BIU_SPEED_VALID()`** (1 while the speed sensor is healthy). Without it, the file stops with `#error`.
2. **`validate_brake_pressure(loco, mode, brake, CMD_MR/CMD_BP/CMD_BC, value)`.**
3. **`check_brake_malfunction(which, value)`** judges against the command last sent and returns `BrakeCheck`.
   Treat `BRAKE_CHECK_NO_SPEC` like `BRAKE_CHECK_MALFUNCTION`.
4. **CAN send:** put your CAN driver's send on the marked line in `biu_can_send_frame` (or define
   `BIU_CAN_SEND(cob_id, data, length)`): standard 11-bit identifier `cob_id`, DLC `length`, `data[0..length-1]`.
5. **CAN receive:** call `process_biu_can_frame(cob_id, data, dlc)` with every CAN frame from the BIU (ids 0x000,
   0x710, 0x220, 0x320, 0x420; a filter on those is enough). `process_biu_reply` is gone.
6. **Bench testing over UDP:** `biu_link_select(BIU_LINK_UDP)` before `biu_link_init()`, and give
   `process_biu_udp_frame(buffer, len)` every datagram from the DLConsole Brake Test Bench.
7. **Call `biu_link_init()` once, then `biu_link_tick()` and `send_biu_command()` every 250 ms.**
8. **DLConsole copies** (fourth round): in `utility.h`, add to `LOG_PKT_TYPE`, before `LOG_PKT_TYPE_COUNT`:
   ```c
   LOG_PKT_BIU_CMD, LOG_PKT_BIU_BEAT, LOG_PKT_BIU_NMT, LOG_PKT_BIU_BEATRX,
   LOG_PKT_BIU_PRESS, LOG_PKT_BIU_CODES, LOG_PKT_BIU_DISC,
   ```
   and in `utility.c`, `log_pkt_token[]`, at the same places:
   ```c
   "biu_cmd", "biu_beat", "biu_nmt", "biu_beatrx", "biu_press", "biu_codes", "biu_disc"
   ```
   Then `biu_dlconsole_mirror(1)` wherever the copies should start (a DMI or maintenance setting, or at boot
   while testing). Or build with `BIU_DLCONSOLE_MIRROR 0` to leave them out.
9. **Bench port 51235** (patch 390): `BIU_SIM_PORT` defaults to 51235, where the Brake Test Bench now listens.
   51234 is DLConsole's log port; sharing it split the traffic (the BIU frames were counted as malformed logs).

## Tested

`test_biu.c` is a host test. Build it with `test_common_include.h` / `test_enet_user.h` / `test_utility.h` renamed
to `common_include.h` / `enet_user.h` / `utility.h`:
`gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I. test_biu.c brake_pressure_interface_user.c -lm`

It runs 89 checks:
- the frame bytes against Annexure-1, with values from Annexure-4 (on the UDP link, where the bytes are visible);
- the CAN link: identifier and DLC per frame, the same 8 command bytes with no header, nothing on UDP, status
  frames taken from CAN, and a frame from the link not selected refused;
- boot-up, heartbeat, start, stop and reset, and the BIU heartbeat timeout;
- settling per pressure (EB -> release, LE -> NB);
- the three status frames, acks and refusals;
- the DLConsole copies: off by default; on, each frame sent and received as its capture id with the CAN data
  bytes (the command's the same 8 bytes as on CAN), an NMT for another node copied though not taken, an unknown
  COB-ID and the link not selected not copied, the UDP link copied without its header; off again, none. Also
  built with `BIU_DLCONSOLE_MIRROR 0` and no `utility.h`.

Not built with the TI compiler or on target, and not run against a real BIU. The CAN send is yours to put in.
