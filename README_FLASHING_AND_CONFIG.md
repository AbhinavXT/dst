# Flashing firmware and sending the loco configuration

This guide covers two tools in DLConsole:

- **Firmware Flasher**: puts a new `.appimage` on a Kavach card.
- **Loco Configuration**: sets a loco's configuration (LOCO_INFO) and sends
  it to the VCC.

Both are in the **Tools** menu.

---

## Before you start

- Connect the PC to the loco's Ethernet with a cable.
- The PC needs an IP address **on the same network as the VCC**. For
  example, the VCC is `192.168.25.168`, so the PC could be `192.168.25.10`.
- Keep the chassis power switch within reach. Flashing needs a power cycle.

---

# Part 1: Flashing firmware

**Open it:** Tools ▸ Firmware Flasher… (or **Ctrl+Alt+F**)

## The one rule to remember

> **One card per power cycle.**
> Press **Flash first**, then **switch the chassis off and on**.

Why: the board only listens for new firmware for **5 seconds** after it
powers on. After that it starts its normal program. The flasher keeps
calling the board while you power-cycle, so you don't have to be quick.

## Steps

1. **Pick the profile** for your bench or site (top bar). This fills in the
   VCC IP address and port.
2. **Choose the image** for the card you want to flash. Either:
   - click the **Image** cell on that card's row and pick the file, or
   - drag the `.appimage` file onto the row.
3. Make sure **that card's round button is selected**. Only one card can be
   selected.
4. Check the **Pre-flight** list on the left. Every line should be ✓. A ⚠
   line tells you what's wrong (see the table below).
5. Press **Flash [card name]**.
6. When the yellow bar says **"Power-cycle the chassis now"**, switch the
   chassis **off, then on**. You have the time shown on the bar, 60 seconds
   by default.
7. Watch the transfer. The block map fills in, and a normal transfer takes
   seconds.
8. Read the result on the **Summary** page.

To flash another card: go **Back to queue**, select the next card, and
repeat from step 5. **Every card needs its own power cycle.**

## Flashing several VCCs at once

Up to **8 VCCs** can get the same VCC image in one run, for example a
batch of locos on one switch. Each VCC needs **its own IP address**.

1. Under **VCC IP address**, add a row per VCC:
   - **+ Add VCC** adds the next address (`.169` after `.168`);
   - **Add range…** adds several at once: `192.168.25.168-175` is 8 VCCs;
   - **×** removes a row; **untick** a row to leave that VCC out of this
     run.

   A row in red has a problem: hover it to see why (not an address, or
   listed twice). The line under the buttons says how many are ticked.
   To keep the list, put it in the profile (⚙) as
   `192.168.25.168, 192.168.25.169` or `192.168.25.168-175`.
2. Select the **VCC** row. Several addresses work only for the VCC card;
   Input, Output and Analog images go through their own VCC, one chassis
   at a time.
3. Press **Flash 8 VCCs**, then **power-cycle every chassis**. Each VCC is
   waited for on its own, up to the time on the bar.

Every VCC runs its own transfer at the same time: its own resends after
lost packets, and its own speed. A VCC that fails does not stop the
others. **Abort all** stops all of them.

While it runs, the left column has one row per VCC with its progress.
**Click a row** to see that VCC's block map and numbers. The log shows
every VCC, each line starting with its address.

On the **Summary**, each VCC has its own result. **Try again** goes back
with only the VCCs that were **not** updated in the address field, so a
verified VCC is not flashed twice. History has one record per VCC.

## Which updater: Classic or 2.6 (top bar)

The top bar has an **Updater** switch: **Classic | 2.6**. Pick the one
your boards run. It is saved **with the profile**, so a bench's profile
remembers it. It can't be changed while flashing.

| | Classic | 2.6 (updater 2.2 or later) |
|---|---|---|
| While you power-cycle | The flasher offers the image until the board answers in its 5 s window. | The flasher sends **HOLD**. The board answers and then **stays in the updater for 2 minutes** instead of 5 s. |
| The board refuses the image (wrong card, older version, damaged trailer) | Not reported by older boards. | Stops at once with **"image refused by the board"** and the reason. Sending it again won't help. |
| The answer is lost at the very end | **Not updated** | **Result unknown**: the board may have flashed. Let it boot and check its version before flashing again. |
| After an Input/Output/Analog image | Nothing more. | The flasher sends **BOOT** so the VCC starts its program once it has passed the image on. |

A board with an updater older than 2.2 still works in **2.6**: it isn't
held, and the Summary says "updater older than 2.2". **Classic** is the
flasher exactly as before.

### Loco addresses (2.6)

A 2.6 board answers on its **loco's address**, the same one its
application uses:

| | VCC 1 ctrl 1 | VCC 1 ctrl 2 | VCC 2 ctrl 1 | VCC 2 ctrl 2 |
|---|---|---|---|---|
| **Loco 1** | .16 | .102 | .103 | .104 |
| **Loco 2** | .77 | .105 | .106 | .107 |
| **Loco 3** | .87 | .88 | .96 | .97 |

All on `192.168.25.x`. With **2.6**, the target box has **Loco
addresses ▾**: pick one controller, or **All 4 controllers** of a loco
for a VCC image. Each known address is labelled under its row ("Loco 1 ·
VCC 1 controller 1").

- **Input, Output and Analog images** go to **VCC 1 controller 1** of
  the loco, which passes them on over CAN. Sending one to any other
  controller is refused before anything is sent.
- **`.167`** is a controller whose ID is unknown (a strap fault). It
  refuses every image.
- **Any other `192.168.25.x`**, such as the old `.168`, gets a ⚠: a 2.6
  board won't answer there.

### The image trailer (2.6)

A **stamped** image (made with `fw_stamp.py`) ends in a short trailer
that says which card it is built for and its version. With **2.6** the
queue reads it:
- **It matches the card:** the pre-flight shows "Image trailer: Input
  card · 1.2.9 · built …", and the file name doesn't matter.
- **It's built for another card:** Flash is off. The board would refuse
  it.
- **The trailer is damaged:** Flash is off. Re-stamp the image.
- **No trailer (unstamped):** a ⚠ line. The card is checked by the file
  name, as before.

The image's row shows the version (`… · v1.2.9`). The detail strip shows
the whole trailer.

### Card results (2.6)

With **2.6**, an Input, Output or Analog image doesn't stop at "the VCC
accepted it". The VCC passes it to the card over CAN, and **each card
controller reports back** how its update ended. The flasher waits for
those reports (up to 20 s, less once they stop coming), then releases the
VCC.

| Summary says | Meaning |
|---|---|
| **updated, the card reported OK** | Every controller that reported wrote the image. Each one is listed. |
| **card update failed** | A controller reported VERIFY_FAIL, FLASH_FAIL, INCOMPLETE or REJECTED. It is named, and the card keeps its old firmware. |
| **accepted by the VCC, no card reported back** | No report arrived. The card may still have updated: use Ask the board, or check its version. |

### Ask the board (2.6)

**Ask board…** (top bar, 2.6 only) asks a board's updater what it holds:
- the updater version and the controller ID;
- both flash banks, with the firmware version in each;
- any transfer in progress;
- on VCC 1 controller 1, the result each card reported.

Press **Ask**. If the board is running its program, power-cycle the
chassis while the flasher asks. A 2.2+ board is then **held** in the
updater. **Release (BOOT)** lets it start its program; otherwise it
starts by itself after 2 minutes.

If the queue has images loaded, the dialog says whether the board's
**active bank is one of them (same CRC)**. That settles a **Result
unknown**: the Summary has an **Ask the board…** button for it.

### Signed images and Make image… (2.7)

Updater 2.7 boards can require images signed with the release key. The
key pair is made once, on the release machine, with the updater's
`tools/fw_sign.py keygen`; the public key goes into the updater's
`source/inc/fw_pubkeys.h` (`fw_sign.py header`), and every card's updater
is rebuilt with it.

- **Edit profile ▸ Signed images:** point it at that `fw_pubkeys.h`.
  Pre-flight then checks each signed image on this PC and stops one the
  boards would refuse. Tick **Boards require signed images** if the boards
  were built with `FW_REQUIRE_SIGNATURE = 1`.
- **Make image… (top bar):** turns a built `.appimage` into a release
  image, written to a new file (the built one is never changed).
  - **Stamp:** card, version (`1.2.9`) and hardware rev, as `fw_stamp.py`
    does. Filled in from the file's name, or its existing trailer.
  - **Sign:** runs the updater's own `fw_sign.py` with your private key
    (`release.pem`) and key id. Needs Python and its `cryptography`
    package on this PC. The passphrase is asked each time and never saved.
  - The result is checked against the profile's `fw_pubkeys.h`.
- Keep the private key off shared drives; sign only on a PC you trust.

## What the result means

| Result | Meaning | What to do |
|---|---|---|
| **Verified** | The board received the whole image and checked it. | **Keep the chassis powered** until the card starts up again. |
| **Not updated** (badge: *Failed*) | Something went wrong. The reason is on the Summary page. | See "Common problems" below. |
| **Aborted** (badge: *Cancelled*) | You pressed Abort. | The card still has its old firmware. Flash again. |
| **Image refused by the board** (2.6) | The image arrived intact, but the board won't take it. The reason is shown. | Pick the right file for this card. |
| **Result unknown** (2.6) | The board stopped answering at the very end, perhaps while flashing. | Let it boot, then check its firmware version before flashing again. |

**Input, Output and Analog cards:** the image goes to the VCC first, and
the VCC then passes it to the card over CAN. "Verified" means the VCC
checked it. The PC gets no message from the card itself, so keep the
chassis powered for about a minute and check the card comes up with the
new version.

## Common problems

| You see | Why | Fix |
|---|---|---|
| **"The board never answered"** | The chassis wasn't power-cycled in time, or the network is wrong. | Press Flash, **then** power-cycle. Check the cable and the IP address. |
| **IMAGE_FAIL** | The board rejected the image: wrong file, or wrong card. | Check you picked the right file for **this** card. Sending the same file again won't help. |
| **"image is … the updater takes at most 800 KB"** | The file is too big for the board. | Use the correct build. |
| **"looks like the Output card image"** (or similar) | The file name suggests a different card. | Pick the right file. Operator mode blocks this; Engineer mode asks first. |
| **No adapter on the board's subnet** | The PC isn't on the VCC's network. | Set the PC's IP to the same network as the VCC. |

**File names the flasher recognises:**
- `LKAVACH…` → VCC
- `…Input…` → Input card
- `…Output…` → Output card
- `…Analog…` → Analog card

## Engineer and Operator modes (top bar)

| | Operator | Engineer |
|---|---|---|
| File name doesn't match the card | **Blocked** | Allowed after a warning |
| PC not on the VCC's network | **Blocked** | Warning only |
| Transfer settings (⚙) | Locked | Can change |

## Other useful things

- **History** (top bar): every card flashed from this PC, with who, when,
  the file and its CRC. You can export it as CSV.
- **⚙ (profile settings):** the VCC IP address, port, default image for
  each card, and how long to wait for the power cycle.
- **Load images from folder…** finds the newest image for each card in a
  build folder.

---

# Part 2: Sending the loco configuration

**Open it:** Tools ▸ Loco Configuration… (or **Ctrl+Alt+L**)

This does the same job as `loco_config_v12`: it sets values like the loco
unit ID, wheel diameters, speeds and timeouts, and sends them to the VCC.

> **Important:** the loco must be **running normally** (not in flashing
> mode). **The VCC does not reply.** DLConsole records exactly what it
> sent, but it cannot confirm the loco applied it.

## Steps

1. **Pick the loco's configuration** from the **Configuration** list (top
   bar). New loco? Use **Manage ▸ New from defaults…** and name it, for
   example after the loco number.
2. **Find the field** you want to change:
   - click a group on the left (for example *Loco & wheels*), or
   - type in **Search fields…**
3. **Double-click the Value** and type the new value. Press **Enter**.
   - A wrong value, for example too big for the field, is refused and the
     reason shows at the bottom.
   - IP addresses are typed normally, like `10.20.30.40`.
4. **Check `vcc_crc`** in the send bar. It must match the VCC firmware on
   this loco.
5. Check the **targets** in the send bar. There are up to **4**, each a
   tick box, IP and port. The default is the VCC's address and `50001`.
   - To send to one VCC, fill in row 1 only.
   - To send to several at once, fill in more rows and tick them.
     **Every ticked target gets exactly the same configuration**,
     including `loco_unit_id`, so only do this when that is what you want.
6. Press **Send to VCC…** (or **Send to 3 VCCs…**). A box lists **exactly
   what changed** since the last send, and every target. Read it, then
   press **Yes**. If one target fails, the others are still sent to, and
   the message at the bottom names the one that failed.

Your changes are saved automatically. Close and reopen, and they are still
there.

## Reading the table

| Mark | Meaning |
|---|---|
| **Bold value** | Different from the default |
| **●** before the name | Changed since the last send, not sent yet |
| **Default** column | The value from the loco_config tool |
| **Last sent** column | What was actually sent last time |

**Right-click a field** to put it back to the default or to the last-sent
value.

**Groups at the top of the list:**
- **Changed from defaults**: every field that differs from the defaults.
- **Changed since last send**: what the next Send will change.

## Checking what the loco actually holds

The VCC doesn't reply to a send, but it prints its configuration
(`@linfo`) every so often. DLConsole compares each one with what you sent,
and shows the result under **Last sent**:

| You see | Meaning |
|---|---|
| **✓ Loco 7_1 holds exactly what was last sent** | Confirmed: the loco has your configuration. |
| **Waiting for loco 7_1's next @linfo…** | You just sent; the loco hasn't printed its configuration since. Wait a moment. |
| **✗ Loco 7_1 holds something else: 3 fields differ** | The loco has a different configuration. Hover to see which fields. |
| **no @linfo … received yet** | Nothing from this loco yet. Check it is connected and running. |

The loco is matched by `loco_unit_id`. **Load the loco's values…** copies
what the loco holds into the editor, handy to start from a loco's real
settings.

## Making a `loco_info.bin` file

For flashing the configuration directly into memory (at `0x60700000`):

- Press **Export loco_info.bin…** in the send bar.
- The file is exactly what `loco_config_v12` would write for the same
  values.

## Loading an existing configuration

- **Manage ▸ Import loco_info.bin…** loads a file into the open
  configuration.
- If the file's CRC is wrong (damaged or hand-edited), you are warned
  first.

## Undoing a change

The VCC can't tell you what it currently holds, so use the **History**:

1. Open **History** (top bar).
2. Pick the send you want to go back to.
3. Press **Load into current configuration…**, then **Send to VCC…** again.

History can also **Export as loco_info.bin…** for any past send,
byte-for-byte the same.

---

# Part 3: For engineers — adding a new config field

When a new member is added to the LOCO_INFO struct:

1. Add it to the C struct in the firmware and in the loco_config tool, and
   give it a value in the tool.
2. Add **one line** for it in `schema/kavach.xml`, inside
   `<packet name="LINFO">`, at the same position as in the struct.
3. Run:
   ```
   python3 lococonfig/sync_linfo.py path/to/loco_config_vNN.cpp
   ```
   This checks the schema against the C struct and tells you exactly what
   doesn't match. Then it updates the defaults.
4. Build and run `verify.sh`.

Full details: `lococonfig/ADDING_A_FIELD.md`.

---

## Where DLConsole keeps things

All in the same folder as `dlconsole.ini`:

| File | What it is |
|---|---|
| `flasher_profiles.json` | Flasher profiles (IP, port, default images) |
| `flash_history.jsonl` | Every card flashed |
| `loco_configs.json` | Saved loco configurations |
| `loco_config_history.jsonl` | Every configuration sent, byte for byte |

Back up that folder to keep your profiles, configurations and history.
