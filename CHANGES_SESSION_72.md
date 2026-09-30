# Session 72 — README for flashing and loco configuration

New `README_FLASHING_AND_CONFIG.md` at the project root: a plain-language
guide for operators and engineers.

- **Part 1, Firmware Flasher:** the one rule (press Flash, then
  power-cycle, one card per power cycle), step-by-step flashing, what each
  result means, a common-problems table, Engineer vs Operator mode, history
  and profiles.
- **Part 2, Loco Configuration:** editing and sending a loco's LOCO_INFO,
  what the bold/● marks mean, exporting and importing `loco_info.bin`, and
  undoing a change through History. It states clearly that the VCC does
  not reply.
- **Part 3, engineers:** adding a LOCO_INFO member in three steps, pointing
  to `lococonfig/ADDING_A_FIELD.md`.
- **Where DLConsole keeps its files:** profiles, configurations and the two
  history logs, all beside `dlconsole.ini`.

Every statement was checked against the code as of Session 71: menu names
and shortcuts, button labels, result words, the 5 s updater window and 60 s
default wait, the 800 KB limit, file-name rules, the mode table and file
names. No code changes.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite 125 suites / 3405 checks, menu audit 70 ok, headless smoke alive.
