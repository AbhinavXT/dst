# BIU, brake pressure and analog firmware code: how it works

This explains `brake_pressure_interface_user.c` / `.h` and `biu_can_messages.h`, the KAVACH (VCC) side of the
link to the Brake Interface Unit (BIU), and `loco_sim/`, a program that runs that code on a PC as the loco so you
can test DLConsole's Brake Test Bench and analog card against it.

Spec: RDSO MP.0.01.00.31 Rev.05, Annexure-1 (the CAN protocol) and Annexure-4 (test values). `CHANGES.md` says
where in the spec each rule comes from.

---

## 1. The files

| File | What it is |
|------|------------|
| `biu_can_messages.h` | The wire: node IDs, COB-IDs, NMT codes, the bit of every discrete flag, `BiuCommand` (what we send) and `BiuStatus` (what the BIU told us). No code. |
| `brake_pressure_interface_user.h` | The public API, the brake table's types (`Range`, `BrakeSpec`), the enums (`LocoType`, `Mode`, `Command`, `PressureCmd`, `BrakeCheck`), and the analog reading (`PressureReading`). |
| `brake_pressure_interface_user.c` | Everything that runs: the brake table, the frame codec, the two links (CAN, UDP), network management, sending the command, taking BIU frames, the checks, and the 4-20 mA conversion. |
| `test_biu.c` + `test_common_include.h` + `test_enet_user.h` | Host test: 74 checks with a fake clock and fake sends. |
| `loco_sim/` | The loco simulator (section 9). |

The `.c` needs two headers from the VCC project: `common_include.h` (it uses `loco_params`, `SERIAL_PRINTF`,
the brake-type enum, `ClockP_getTimeUsec`) and `enet_user.h` (it uses `UDP_Send`, `set_pc_ipAddr`). The test
and the loco simulator each bring their own stand-ins.

---

## 2. The big picture

```
            every 250 ms                                   on every frame received
  VCC main loop ─► biu_link_tick()     (heartbeat 0x720)   CAN rx ─► process_biu_can_frame()
                 ─► send_biu_command() (command 0x190)     UDP rx ─► process_biu_udp_frame()
                                  │                                        │
                                  ▼                                        ▼
                          biu_send_frame()                         take_biu_frame()
                       ┌────────┴────────┐                  NMT 0x000 ─► our NMT state
               CAN link│                 │UDP link          0x710     ─► BIU heartbeat / link up
       biu_can_send_frame()    biu_udp_send_frame()         0x220     ─► pressures + validity
       (BIU_CAN_SEND: id,      ([cob lo][cob hi][3+n][data]  0x320     ─► isolation / fault / display codes
        DLC, data)              to the bench, UDP 51234)    0x420     ─► discrete status bytes
```

Only **one link** is live at a time (`biu_link_select`). CAN is the default: the real BIU on the VCC's CAN
controller. UDP is for DLConsole's Brake Test Bench (a BIU simulator on a PC). The frame bytes are the same on
both; only the envelope differs.

---

## 3. The order you call things in

1. `biu_link_select(BIU_LINK_UDP)`: only for the bench. Skip it for the real BIU (CAN is the default).
2. `biu_link_init()`: once after power-on. Sends the boot-up message (`0x720 [00]`) and goes **pre-operational**.
3. Every 250 ms:
   - `biu_link_tick()`: sends our heartbeat (`0x720 [state]`) when 500 ms have passed;
   - `send_biu_command(loco_type, train_mode, brake_type)`: sends the `0x190` command frame, but **only while
     operational**. Call it every cycle, not only when the brake changes: the fault / display acks ride on it.
4. For every frame from the BIU: `process_biu_can_frame(cob_id, data, dlc)` (CAN) or
   `process_biu_udp_frame(buffer, len)` (UDP).
5. Whenever you want to judge a pressure: `check_brake_malfunction(CMD_BP / CMD_BC / CMD_MR, value)`.

The BIU is the network manager. It starts us with NMT "start remote node" (`0x000 [01 20]`); until then we
send heartbeats only.

---

## 4. Build hooks (macros you can define)

| Macro | Default | What it does |
|-------|---------|--------------|
| `BIU_SPEED_VALID()` | **none: you must define it** (`#error` otherwise) | 1 while the speed sensor is healthy. Goes into discrete byte 1, bit 4. |
| `BIU_SPEED_TO_CM_PER_S(speed_m_per_s)` | `speed * 100` | Converts `loco_params.sensor_speed` to the wire's 0.01 m/s units. |
| `BIU_DYNAMIC_BRAKING()` | `0` | Dynamic braking active (discrete byte 1, bit 7). |
| `BIU_NOW_MS()` | `ClockP_getTimeUsec() / 1000` | Milliseconds clock for heartbeats, settling and timeouts. |
| `BIU_CAN_SEND(cob_id, data, length)` | does nothing | **Your CAN driver's send.** Standard 11-bit identifier `cob_id`, DLC `length`, `data[0..length-1]`. Until you define it, nothing goes on the bus. |
| `BIU_SIM_IP0..3`, `BIU_SIM_PORT` | 192.168.25.126 : 51234 | Where the UDP link sends (the PC running the bench). |
| `BIU_HEARTBEAT_PERIOD_MS` | 500 | Our heartbeat period (Annexure-1 4). |
| `BIU_HEARTBEAT_TIMEOUT_MS` | 1500 | How old the BIU's heartbeat may be before `biu_link_up()` says 0 (three missed beats; not from the spec). |

---

## 5. Every public function

### Link and network management

| Function | What it does |
|----------|--------------|
| `biu_link_select(link)` | Chooses `BIU_LINK_CAN` or `BIU_LINK_UDP`. Frames from the other link are refused. |
| `biu_link_selected()` | Which link is selected. |
| `biu_link_init()` | Sends boot-up `0x720 [00]`, sets state pre-operational (`0x7F`), schedules the next heartbeat, clears pending acks. |
| `biu_link_tick()` | If 500 ms have passed, sends `0x720 [our state]`. Calls `biu_link_init()` itself if you forgot. Wrap-safe: compares `(int32_t)(now - due)`. |
| `biu_nmt_state()` | Our state: `0x7F` pre-operational, `0x05` operational, `0x04` stopped. |
| `biu_link_up()` | 1 when the BIU's heartbeat (`0x710`) arrived in the last 1.5 s **and** said operational. |

### Sending the command

`send_biu_command(loco_type, train_mode, brake_type)`, step by step:

1. Looks up the brake table row `kBrakeTable[loco][mode][brake]` and records the command for the settling clock
   (`note_command`: the clock restarts only when the command, loco or mode changes, so resending the same brake
   every 250 ms does not restart it).
2. If we are not operational, it stops here (heartbeat only, Annexure-1 4).
3. Fills a `BiuCommand`:
   - **speed**: `loco_params.sensor_speed` × 100, rounded, clamped to 0..65535;
   - **speed valid**: `BIU_SPEED_VALID()`;
   - **A9** (BP target) and **SA9** (BC target) from the row. BP is the middle of its range, except the EB range
     (which starts at 0) is commanded as 0, an empty brake pipe. BC is the middle of its range. A9 is marked
     valid only up to 6.0 kg/cm², SA9 only up to 4.0;
   - **no table row** (unknown loco, mode or brake): commands BP 0 (valid) and BC not valid, which is an
     emergency on purpose, the same thing the BIU does when it loses KAVACH. Logged once, not every cycle;
   - **light engine** from `loco_params.train_type`, **brake test request** from `loco_params.mbt`,
     **BIU isolate** from the DMI context, **health** from `loco_params.loco_health_status`, **dynamic braking**
     from the hook; BP charging cut-out and remote loco are always 0 (only DPCS / DPWCS set them);
   - **fault / display code ack**: 1 for one cycle after the BIU reported a new code (see below).
4. `biu_encode_command` turns it into the 8 bytes and `biu_send_frame` sends them on the selected link.

### Command frame `0x190`, byte by byte

| Byte | Content |
|------|---------|
| 0-1 | speed, 0.01 m/s, low byte first |
| 2 | discrete byte 1: bit0 brake test request, 1 health, 2 BP charging cut-out, 3 BIU isolate, 4 speed valid, 5 light engine, 6 remote loco, 7 dynamic braking |
| 3 | discrete byte 2: bit0 fault code ack, 1 display code ack, 2 A9 valid, 3 SA9 valid |
| 4 | A9 (BP target), 0.05 kg/cm² per bit, rounded |
| 5 | SA9 (BC target), 0.05 kg/cm² per bit, rounded |
| 6-7 | spare, 0 |

Example (WAP7 formation, NB, 10 m/s, healthy): `E8 03 12 0C 58 18 00 00`: speed 1000, D1 = health + speed
valid, D2 = A9 + SA9 valid, A9 88 = 4.4, SA9 24 = 1.2.

### Receiving

| Function | What it does |
|----------|--------------|
| `process_biu_can_frame(cob_id, data, dlc)` | CAN link only. Refuses a DLC above 8 or NULL data. |
| `process_biu_udp_frame(buffer, len)` | UDP link only. Reads the 3-byte header `[cob lo][cob hi][3 + n]` and refuses a length byte that does not fit. |
| `biu_status_get()` | The last `BiuStatus`: pressures with validity, codes, discretes, when each frame arrived, the BIU's NMT state. |

Both return 1 when the frame was taken. They hand the frame to `take_biu_frame`, which does:

| COB-ID | Frame | What happens |
|--------|-------|--------------|
| `0x000` | NMT `[command][node]` | Only for node `0x20` (us) or `0x00` (all). `01` start → operational, `02` stop → stopped, `80` → pre-operational, `81`/`82` reset → boot-up again. |
| `0x710` | BIU heartbeat `[state]` | Stores the state and the time (for `biu_link_up`). |
| `0x220` | pressures | Bytes 0-6: BP, BC, MR, A9 ref, SA9 ref, air flow, feed pipe (0.05 kg/cm² per bit). Byte 7: bit n = byte n valid. **Use a pressure only when its `valid` is 1.** |
| `0x320` | codes | Isolation counter, fault code, display code (each 16-bit, low byte first). A new non-zero code sets the ack for the next command; the same code again is not acked twice; 0 clears it. |
| `0x420` | discretes | Three status bytes (`BIU_ST_D1_*`, `D2_*`, `D3_*` in `biu_can_messages.h`). |

Any other COB-ID, or a frame too short for its type, returns 0.

### The checks

| Function | What it does |
|----------|--------------|
| `validate_brake_pressure(loco, mode, brake, which, value)` | Stateless: 1 when `value` is inside the table's range for that pressure, 0 otherwise or when there is no row. |
| `check_brake_malfunction(which, value)` | Judges against **the command last sent**, once the pressure has had time to settle. Returns `BRAKE_CHECK_WAIT` (still moving), `OK`, `MALFUNCTION` (settled and out of range, also printed), or `NO_SPEC` (no row: treat as a malfunction). |

**Settling time**, per pressure:
- moving toward more braking (BP falling, BC rising): wait the **new** command's `t_apply`;
- moving back (BP rising, BC falling): wait the **previous** command's `t_release`;
- MR always uses `t_apply`.

So EB → no brake waits EB's release time (60 s on a WAG9), not no-brake's apply time. LE → NB waits NB's
apply for BP and LE's release for BC.

### The brake table (`kBrakeTable`)

Rows by loco (WAP7, WAG9, WAP5) × mode (light engine `LEM`, `FORMATION`) × brake (no brake, overspeed, NB,
FSB, EB, LE). Each row: MR range, BP range, BC range, apply time, release time.

| Brake | BP (kg/cm²) | BC light engine | BC formation |
|-------|-------------|-----------------|--------------|
| no brake / overspeed | 5.0 ± 0.1 | 0 ± 0.1 | 0 ± 0.1 |
| NB | 4.4 ± 0.1 | 1.2 ± 0.1 | 1.2 ± 0.1 |
| FSB | 3.5 ± 0.2 | 3.5 ± 0.1 | 2.4 ± 0.1 |
| EB | below 0.3 (commanded 0) | 3.5 ± 0.1 | 2.4 ± 0.1 |
| LE (independent brake) | 5.0 ± 0.1 (left charged) | 3.5 ± 0.1 | 3.5 ± 0.1 |

MR is 8-10 everywhere. The times differ per loco (see the table in the `.c`). The range macros are
`RANGE_TOL(nominal, tolerance)`, `RANGE_UPPER(upper_limit)` (from 0) and `RANGE_BETWEEN(lower, upper)`.
A loco type not in the table gets BP 0 (section 5, "no table row").

### Codec helpers

| Function | What it does |
|----------|--------------|
| `biu_kgcm2_to_byte(kgcm2)` | kg/cm² → byte at 0.05 per bit, **rounded**; negative or NaN → 0, above 12.75 → 255. |
| `biu_byte_to_kgcm2(raw)` | byte → kg/cm². |
| `biu_encode_command(command, out)` | `BiuCommand` → the 8 bytes above, with explicit shifts (no struct layout depends on the compiler). |

---

## 6. The analog code: `current_to_pressure(current_mA, sensor_fs_bar)`

The pressure transducers on the analog card are 4-20 mA loops. This turns a loop current into kg/cm²:

| Current | Result |
|---------|--------|
| below 3.5 mA | `SENSE_FAULT_OPEN`: broken wire or sensor unpowered |
| above 20.5 mA | `SENSE_FAULT_SHORT`: shorted loop or failed transmitter |
| otherwise | `fraction_of_full_scale = (mA − 4) / 16`, clamped to 0..1; `value_kgcm2 = fraction × full scale (bar) × 1.02` |

`raw_mA` always holds the input, for logs. Example: 12 mA on a 10-bar sensor = 0.5 × 10 × 1.02 = 5.1 kg/cm².

**How the analog card's frame reaches it** (DLConsole `cabio.h`): a 34-byte frame, message `0xF1`, from card
15 (top) or 16 (bottom) to the VCC (1):

| Bytes | Content |
|-------|---------|
| 0 | message length (34) |
| 1 | source: 15 or 16 |
| 2 | destination: 1 |
| 3 | message id `0xF1` |
| 4-5 | sequence number, low byte first |
| 6-29 | six channels, `uint32` big-endian each; channels 1-3 are BP, BC, MR |
| 30-33 | CRC: JAMCRC with init 0 over bytes 0-29, low byte first |

The VCC reads a channel as `mA = raw / 2^31 × 75` and passes it to `current_to_pressure`.

---

## 7. Two links, what goes on the wire

| | CAN link (real BIU) | UDP link (DLConsole bench) |
|-|---------------------|----------------------------|
| select | default, or `biu_link_select(BIU_LINK_CAN)` | `biu_link_select(BIU_LINK_UDP)` |
| send | `BIU_CAN_SEND(cob_id, data, length)`: id = COB-ID, DLC = length, data | one datagram: `[COB-ID lo][COB-ID hi][3 + length][data]` to `BIU_SIM_IP:BIU_SIM_PORT` |
| receive | `process_biu_can_frame(cob_id, data, dlc)` | `process_biu_udp_frame(buffer, len)` |
| lengths | heartbeat DLC 1, NMT 2, data 8 | the same, plus the 3-byte header |

The COB-IDs: command `0x190` (0x180 + BIU node 0x10), status `0x220` / `0x320` / `0x420` (0x200/0x300/0x400 +
KAVACH node 0x20), our heartbeat `0x720`, the BIU's `0x710`, NMT `0x000`.

---

## 8. The host test

```
cd docs/biu-firmware
mkdir -p /tmp/biu && cp *.c *.h /tmp/biu && cd /tmp/biu
mv test_common_include.h common_include.h && mv test_enet_user.h enet_user.h
gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I. test_biu.c brake_pressure_interface_user.c -lm
./a.out          # 74 checks, 0 failed
```

It drives a fake clock (`fake_usec`) and captures what would be sent, then checks the boot-up, heartbeats,
NMT start / stop / reset, the command bytes for each brake against Annexure-1 and -4, the settling times, the
three status frames, the acks, refusals of bad frames, the codec edges, the 4-20 mA conversion, and the CAN
link (id, DLC, no header, nothing on UDP).

---

## 9. The loco simulator (`loco_sim/`)

It is the **real** `brake_pressure_interface_user.c`, built on a PC with stand-in headers that use real UDP
sockets and the real clock. It plays the VCC on the loco, so DLConsole's Brake Test Bench and analog card get
real firmware traffic without a VCC.

### Build (Mac or Linux)

```
cd docs/biu-firmware
/usr/bin/cc -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Iloco_sim \
    loco_sim/loco_sim.c brake_pressure_interface_user.c -o loco_sim/loco_sim
```

(`/usr/bin/cc` because `cc` is aliased to `clear` in your shell.) Windows is not supported yet: it uses POSIX
sockets.

### Run against DLConsole on the same PC

1. In DLConsole: Tools ▸ Transmit ▸ **Brake Test Bench…**. On **Network**, leave the listen port at 51234 and
   the reply port at 50001, and set the reply address to the sender (or 127.0.0.1). Press **Start BIU**.
2. On **Analog → VCC**, set the host to `127.0.0.1` and the port to `20000`, and start sending.
3. Run:
   ```
   ./loco_sim/loco_sim --bench 127.0.0.1 --loco wag9 --mode formation --brake 0 --speed 10
   ```
4. The bench starts the loco (NMT start), the loco goes operational and sends its command every 250 ms.

### Options

| Option | Default | What it does |
|--------|---------|--------------|
| `--bench a.b.c.d` | the firmware's `BIU_SIM_IP` (192.168.25.126) | Where the BIU frames go. |
| `--bench-port N` | 51234 | The bench's listen port. |
| `--reply-port N` | 50001 | Where the loco listens for the bench's frames (and sends from). |
| `--analog-port N` | 20000 | Where the loco listens for analog frames. |
| `--loco wap7\|wag9\|wap5` | wap7 | Brake table loco. |
| `--mode formation\|lem` | formation | Brake table mode; `lem` also sets the light-engine bit. |
| `--brake 0-5` | 0 | Starting brake: 0 no brake, 1 overspeed, 2 NB, 3 FSB, 4 EB, 5 LE. |
| `--speed m/s` | 0 | Speed sent in the command. |
| `--analog-bar N` | 10 | Sensor full scale for `current_to_pressure`. |
| `--can` | off | Runs the CAN link with no driver: nothing is sent, and the bench's UDP frames are refused. Shows the link switch. |
| `--help` | | The usage. |

### Keys while it runs (type, then Enter)

| Key | What it does |
|-----|--------------|
| `0`-`5` | Change the brake (the settling clock restarts). |
| `i` | Toggle BIU isolate (discrete byte 1, bit 3). |
| `s` | Toggle speed valid (discrete byte 1, bit 4). |
| `q` | Quit. |

### Reading its output (once a second)

```
[NB] us operational, BIU link up, isolate 0
  BP  BIU  4.40 ok      analog 10.90 mA =  4.40 ok     analog - BIU +0.00
  BC  BIU  1.20 wait    analog  5.88 mA =  1.20 wait   analog - BIU -0.00
  MR  BIU  9.00 ok      analog 18.12 mA =  9.00 ok     analog - BIU +0.00
  analog card 15 seq 7, CRC ok
```

- **us**: our NMT state. **BIU link**: `biu_link_up()`.
- **BIU**: the pressure from the bench's `0x220`, and what `check_brake_malfunction` says of it
  (`wait`, `ok`, `MALFUNCTION`, `NO SPEC`); `not valid` when its validity bit is 0; `--` before any `0x220`.
- **analog**: the analog card's channel in mA, the pressure `current_to_pressure` makes of it, its check, and
  the difference from the BIU's value. `OPEN` / `SHORT` for a loop fault; `--` when no frame came in the last 2 s.
- **CRC**: whether the analog frame's CRC matches JAMCRC/0.
- Fault and display codes appear when the bench sends them; the firmware acks them on the next command.
- The firmware's own messages (`BIU: brake … expected …`) print too.

The firmware has no analog-versus-BIU cross-check yet, so the difference is shown, not judged.

### What to try with the bench

- **Normal brake ladder:** step 0 → 2 → 3 → 4 → 0 and watch `wait` turn to `ok` after each settling time.
- **Stuck or slow pressure:** set a plant fault (stuck / slow / leak) on the bench and watch `MALFUNCTION`
  after the settling time.
- **Analog offset:** give the analog card an offset and watch the `analog - BIU` difference.
- **Analog loop faults:** drive a channel below 3.5 mA or above 20.5 mA: `OPEN` / `SHORT`.
- **Validity:** set a 0x220 pressure's validity to 0: `not valid`.
- **Codes:** queue a fault or display code: it appears, and the bench shows the ack.
- **Link loss:** silence the bench's heartbeat: `BIU link DOWN` after 1.5 s.
- **NMT:** send Stop from the bench: the loco stops sending commands (heartbeats go on).
- **Unknown loco / brake:** not reachable from the options; the host test covers it (BP 0, BC not valid).

### Not checked

Run against a stand-in bench script (boot-up, NMT start, `0x190` bytes, `0x220`, an analog frame with
DLConsole's CRC), not yet against the DLConsole window itself. Not built on Windows.
