# BIU interface: what changed and what callers must do

Against RDSO MP.0.01.00.31 Rev.05, Annexure-1 (CAN protocol) and Annexure-4 (test values).

## Files

- `brake_pressure_interface_user.c` / `.h`: replace yours. Both are ASCII now; the old `.c` was Windows-1252.
- `biu_can_messages.h` (new): replaces the `STRUCT_LKAVACH_BIU_*` / `STRUCT_BIU_LKAVACH_*` block
  and `LKAVACH_BIU_COMMAND_MSG`. Delete those from the header they were in.

## Fixed

| # | Was | Now |
|---|-----|-----|
| 1 | A9/SA9 sent at 0.1 kg/cm2 per bit (`* 10`), truncated: BIU applied half (no brake -> BP 2.5) | 0.05 per bit, rounded (`biu_kgcm2_to_byte`) |
| 2 | COB-ID `0x181` | `0x190` = 0x180 + BIU node 1 (0x10) |
| 3 | Bit-field structs memcpy'd; feedback structs unpacked (28 bytes for 19) | Frames built and read byte by byte with explicit shifts; no layout depends on the compiler |
| 4 | `process_biu_reply` copied into a local and dropped it | Dispatches 0x220 / 0x320 / 0x420, scales by 0.05, keeps each validity bit; `biu_status_get()` |
| 5 | Settling time always the new command's `t_apply`: EB -> release judged at 5 s | Apply time when the command is at least as strong as the one before, the previous command's `t_release` when weaker |
| 6 | No table row -> "no malfunction"; zeroed range passed exactly 0.0; NULL deref in two prints | `BRAKE_CHECK_NO_SPEC`; validate returns 0; no deref |
| 7 | One `cmd_type` used as table row and as check kind | Brake and pressure (`PressureCmd`: `CMD_MR / CMD_BP / CMD_BC`) are separate arguments |
| 8 | Dynamic braking always 1, speed valid always 1, A9/SA9 valid always 1 | Dynamic braking from a hook (default 0); speed validity from a hook; A9 valid only <= 6.0, SA9 only <= 4.0; no row -> BP 0 on purpose, BC not valid |
| 9 | Fault / display codes never acked | Acked for one command cycle after each new code |
| - | `ClockP_getTicks()` taken as ms | `ClockP_getTimeUsec() / 1000` (`BIU_NOW_MS`, overridable) |
| - | EB BP commanded as 0.15 (middle of "below 0.3") | 0 |
| - | `1.02` double literal | `1.02f` |
| - | Third table block commented WAG9 | WAP5 |
| - | Unused `StdPressureSpec`, `MR_STD`, `BP_CHARGED_STD`, `BC_STD`, `STD_CMD_COUNT`, application CRC | Removed |

## Second round (your answers)

- **Table to Annexure-4.** BC is NB 1.2 +/- 0.1 for every loco. FSB and EB are 3.5 +/- 0.1 in light engine
  (8.6 note) and 2.4 +/- 0.1 in formation (9.8); formation EB follows formation FSB, as it did before. WAP5 now
  matches the other locos (it had 2.16 and 5.15). The times are unchanged, except WAP5 formation EB `t_apply`,
  which goes from 5 to 9 s, in line with its FSB row.
- **Light engine brake has its own row** (`CMD_LE` = the VCC's brake type 5). The BIU spec has no LE command,
  so this row is its SA9 full application: BC 3.5 +/- 0.1 with BP left charged at 5.0 (Annexure-4 4.2, 2.4).
  This is my reading; it needs a check against how the VCC means the LE brake.
- **Settling per pressure.** Moving toward more braking (BP down, BC up) waits for the new command's apply
  time. Moving back waits for the previous command's release time. For LE -> NB, BP waits NB's apply and
  BC waits LE's release.
- **No table row -> BP 0** stays. It matches the spec: the BIU itself applies emergency when it loses KAVACH
  (Annexure-1 4 c).
- **Network management and heartbeat.**
  - `biu_link_init()` after power-on sends the boot-up message (0x720 [00]) and waits pre-operational.
  - `biu_link_tick()` (call it at least every 250 ms) sends the heartbeat (0x720, our state) every 500 ms.
  - The BIU's NMT on COB-ID 0x000 is handled: start, stop, pre-operational, reset node, reset communication.
  - `send_biu_command` sends nothing until we are operational.
  - The BIU's heartbeat (0x710) is watched; `biu_link_up()` says whether it is fresh (1.5 s) and operational.
- **Speed** defaults to m/s x 100 (0.01 m/s on the wire), rounded. You round to 2 places already, so this matches.
- **Defaults** are 192.168.25.126:51234 (the DLConsole BIU simulator). Override `BIU_SIM_IP0..3` / `BIU_SIM_PORT`.
- **Gateway frames have a variable length:** `[COB-ID lo][COB-ID hi][3 + n][n bytes]`. The heartbeat has 1 byte,
  NMT 2, data frames 8.

## What callers must change

1. **Define `BIU_SPEED_VALID()`** (1 while the speed sensor is healthy). Without it, the file stops with `#error`.
2. **`validate_brake_pressure(loco, mode, brake, CMD_MR/CMD_BP/CMD_BC, value)`.**
3. **`check_brake_malfunction(which, value)`** judges against the command last sent and returns `BrakeCheck`.
   Treat `BRAKE_CHECK_NO_SPEC` like `BRAKE_CHECK_MALFUNCTION`.
4. **`process_biu_reply(buffer, len)`**: give it every UDP datagram that comes in from the BIU or gateway.
5. **Call `biu_link_init()` once, then `biu_link_tick()` and `send_biu_command()` every 250 ms.**

## Tested

`test_biu.c` is a host test. Build it with `test_common_include.h` / `test_enet_user.h` renamed to
`common_include.h` / `enet_user.h`:
`gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I. test_biu.c brake_pressure_interface_user.c -lm`

It runs 62 checks:
- the frame bytes against Annexure-1, with values from Annexure-4;
- boot-up, heartbeat, start, stop and reset, and the BIU heartbeat timeout;
- settling per pressure (EB -> release, LE -> NB);
- the three status frames, acks and refusals.

Not built with the TI compiler or on target, and not run against a real BIU.
