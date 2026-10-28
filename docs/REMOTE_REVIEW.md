# Reviewing a test from somewhere else

Testing runs at the site; the review happens elsewhere. Patches 368-371 make
one file carry what the site saw, and give the reviewer the same tools the
site has.

## At the site

1. **Mark the moments while testing.** Right-click a row ▸ **Add note…**
   ("EB at 14:03, no reason on the DMI"). Ctrl+B marks a row without a note.
   F2 / Shift+F2 step through the marks.
2. **If the Test Case Catalog is in use, keep its run open** (Pass / Fail per
   clause). It goes into the pack.
3. **File ▸ Export review pack…**
   - Site, Tester, Loco: filled in once, remembered after that.
   - **What to look at:** a sentence or two. The reviewer reads this first.
   - **Attachments:** Add files… for what is not in a frame (a DMI clip,
     a photo of the cab). The reviewer sees them in the Review pack panel.
   - Tabs: all of them by default. Span: everything, or From / To around the
     test.
   - **Export…** writes `ReviewPack_<site>_<date>_<time>.zip`. Send that one
     file as it is (mail, drive, chat).

Save in the log header also writes `<name>.notes.json` beside the `.log` and
`.dlr`. Sending those three files works too, but the pack is the whole
picture: it adds the test run and the kavach.xml the site decoded with.

## Where the review happens

1. **File ▸ Open review pack…**, or drop the `.zip` on the window, or use
   File ▸ Open Recent. It unpacks beside itself (`Pack.zip` → `Pack_files/`)
   and opens a **Review pack — <site>** window. Opening the same pack
   again keeps the notes you made in it (patch 381).
2. The **Review pack** panel: what to look at, who, where, when; whether the
   site's kavach.xml **is the same as yours**; **Open its test run…**.
3. The site's notes are on their rows (F2 / Shift+F2).
4. **Play** (F5) on the Playback bar, at 1× to 16×. The cursor moves in time.
   Press **DMI** to open the DMI window, which plays along with the cursor.
   **Skip quiet spans** jumps over silences longer than 3 s and says so.
   Clicking a row moves the playback there.
5. On any row, right-click:
   - **DMI clip around here…**: a GIF to send back.
   - **Radio chart around here…**: SLRP / LSRP / ARP as a sequence diagram,
     PNG or SVG.
   - **Incident report around here…**: the incident pack, with the notes as
     *Operator notes*.
   - **Add note…**: your own note. It also goes into the `.notes.json` beside
     the unpacked `.dlr`, so sending that file back carries it.

## Sending the answer back (patch 384)

In the Review pack panel, **Reply to the site…** writes a reply pack: the
same captures with your notes on them, your **Answer**, any files you
attach, and the site's own kavach.xml. The site opens it with File ▸ Open
review pack…. Your notes show on their rows, and the panel shows what they
asked and your answer. **Answer the reply…** goes on from there.

## More on a recording (patches 372-375)

- **Tools** in the recording window also has, for the tab on show: Run
  summary report, Mission report, Speed vs distance, Radio and GPS health,
  Odometry at the tags, Fault timeline, Cab inputs and outputs, Track
  diagram. They are the same windows as live; "show in log" lands in the
  recording.
- **Test cases** menu: the test cases loaded in the live window run over the
  recording by themselves. You can also run the built-in set or a file. The
  Test cases dock shows OBSERVED / NOT ANSWERED per clause; double-click to
  reach the evidence. *Save observation report…* writes the report. Long
  recordings show a progress bar with Stop.
- **Tools ▸ What changed on the DMI…** lists every change on a loco's DMI:
  mode, brake, target, signal, messages, tag. Speed and countdowns are left
  out unless you ask for them. **F8 / Shift+F8** jump to the next / previous
  change.
- **Two runs of the same scenario:** File ▸ Compare two runs…, or Tools ▸
  Compare with another run… from a recording. The two DMIs play side by side
  on one clock, aligned at the first DMI frame, the first FSB/EB or the first
  mode change. Below them, a table lists what differs between the two DMIs
  at each moment.

## What a pack is

A plain zip, readable without DLConsole: `summary.html` opens in any
browser with what to look at, the notes, what changed on the DMI, the test
run and the attachments (patch 383); `README.txt` says what is in it,
and `manifest.json` lists every file with its size and CRC-32. A file that
does not match is reported as damaged and not opened. The pack records what
was observed; it makes no pass/fail judgement.
