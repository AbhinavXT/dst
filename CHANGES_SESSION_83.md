# Session 83 — DMI (LP-OCIP) window per Annexure-B; fields added to the cab view

## 1. DMI window — Tools ▸ Monitor ▸ DMI (LP-OCIP)… (Ctrl+Alt+D)

The loco pilot's panel drawn from the loco's `@dmi` frames, laid out as
**RDSO/SPN/196/2020 Annexure-B, Amendment-3**:
- the 800 × 600-unit screen scaled to the window, aspect kept;
- each region at the annexure's position and size (B4.2.1), with its font
  sizes in pixels of that screen and its colour codes (Table B.2).

| Region | Shows | From `@dmi` |
|---|---|---|
| A1–A4 | target type, "Target Distance", LOR bar on the A1 scale (0/250/500/1000/2000 m at y 337/272/207/157/107), four-digit value; blank with no target (B4.3.3) | `target_distance_type`, `target_distance` |
| B1 | pointer (130 units, 52-unit hub), speed in black in the hub | `train_speed` |
| B2 | 0–250 km/h over +149°…−149°, 125 divisions of 2 km/h; every 10th labelled (17 × 2), every 5th 15 × 2, rest 6 × 1 | |
| B3 | LGR ring to the permitted speed with a one-division edge mark; DGR to the target speed; LOR from permitted to actual speed when over, BRD while the system brakes. Pointer white within the limit, yellow at it, LOR over it, BRD braking (B4.4.6, B4.10) | `speed_limit_permissible`, `target_speed`, `brake_type` |
| B4 | loco id | `train_id` |
| B5 / B6 | DD-Mmm-YYYY / HH:MM:SS | the frame's DATE_TIME |
| B7 | next lower speed | `target_speed` (while there is a target) |
| B8 | NB / FSB / EB symbol; blank otherwise | `brake_type` |
| B9 | LOC : x.xxx Km | `abs_loco_loc` |
| B10 | mode symbol (SB, SR, LS, FS, OS, Trip, Post Trip, REV, SH, NL, SF) | `loco_mode` |
| B11 | section speed | `section_speed_info` |
| C1–C3 | "Mov. Authority", LBL bar on the C1 scale (0/100/200/250/500/1000/2000/3000/+++ at the annexure's y), five-digit value | `ma_w_r_t_sig` |
| D1 | post with YLW / GRN / YLW / RED, the aspect lit; white disc C / IB / G / A / AG; junction route 1–6 at Table B.4's positions, stencil 1–32; blank when there is no aspect (B4.6.4.1 (d)) | `current_sig_aspect`, `current_sig_info` |
| D2 / D3 | four-digit signal distance; "DN MAIN Adv-Str" in ORG (line number filled into L-X) | `appr_sig_dist`, `current_sig_info` |
| E / F / G | DC X.XX / TL n m / mode in words (Annexure-A1) | `deceleration_constant`, `train_length`, `loco_mode` |
| H | system messages in the annexure's LP-OCIP wording, numbers filled (End of Authority in 60m, Head On Collision with Loco … in … m, Override … in …s, TurnOut / TSR / PSR …, Reverse Mode Expires …, LC Gate …); several alternate every 2 s (B4.6.7 (e)) | `alarm_code` flags + their fields |
| I | context messages likewise (SOS, Over Speed, FSB/EB will be applied in …S, Train Type selected) | `context_values` flags + their fields |
| J | antenna, RF, five bars | `signal_strength` |
| L | Tid-n, Dir-N/R, T Dist-n | `rc`, `movement_dir`, `dist_next_rfid` |
| M | the last three tags on the track diagram | `rc/rcs`, `rl/rls`, `rll/rlls` |
| K | the ten soft keys, as labels | — |

- **Colours.**
  - By default the panel **follows the theme**: the theme's background and
    ink, each Annexure-B hue moved just far enough to reach 3:1 on that
    background (the test holds this on the light theme).
  - **Annexure-B colours** (remembered) draws Table B.2's exact RGB on black,
    as the real panel.
  - Signal lamps and the disc are the railway's colours in both.
- **Monitor only.** The soft keys are labels; nothing is sent back to the
  loco.
- **Stale frame.** An old `@dmi` (over 3 s) is muted and says so.
- **Source selector.** One entry per loco heard.
- **Save image…** writes the panel as a PNG.
- **Assumptions**, listed under **Field sources…** and in `dmiAssumptions()`,
  to confirm against the firmware:
  - C is `ma_w_r_t_sig`;
  - B4 is `train_id`;
  - B7 is `target_speed`;
  - M: tag status 1 = read (DGR), 2 = missed (BRD), otherwise grey;
  - J: `signal_strength` capped at 5.

  "SOS – From Loco …" is shown without a loco id: the frame carries none
  for it, and the collision loco's id is not borrowed.

## 2. Cab view — add your own fields

Right-click the cab view:
- **Add field ▸ \<packet\> ▸ \<field\>**, listing what this loco has
  actually sent, each with its current value;
- **Remove field ▸ …**;
- **Remove all added fields**.

Added fields sit in a band under the cab, three to a row. Each is read from
its freshest source, as the tiles are, and is muted when stale. They are
saved as `lococonsole/cabFields`; adding the same field twice is refused.

## Tests

- **`session83` (34), on 12,000 real `@dmi` frames from `replay/`:**
  - the A1 and C1 scales at the annexure's offsets;
  - lamps for each aspect;
  - Annexure-A1 mode names;
  - H and I wording with their numbers filled;
  - every real frame gives a panel state;
  - the richest frame's regions each equal their own field;
  - date/time format and "DC X.XX";
  - the window: waiting, then the frame, the colour switch, Table B.2
    values, theme hues ≥ 3:1 on the light theme, PNG, one source per loco;
  - cab: fields added (duplicate refused), live values from the real DMI,
    remembered across windows, removed.
- **Menu audit (134 ok):** Tools ▸ Monitor ▸ DMI (LP-OCIP) on Ctrl+Alt+D
  opens the panel.
- **Checked by eye** (offscreen, real frame): the panel in Annexure-B
  colours and in the light theme, against the photo of the bench panel;
  the cab with two added fields. Found and fixed that way: the IB disc was
  drawn in the theme's ink (dark on light themes); it is white, as on the
  post.

## verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **144 suites / 4354 checks** (143 / 4320 before);
- menu audit **134 ok**;
- headless smoke alive.
