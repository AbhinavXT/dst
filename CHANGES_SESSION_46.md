# Patch 46 — the field index

## What it answers

"Which packets carry FRAME_NUM?"

The schema has always known this and nothing showed it. Thirty-five field
names occur in more than one packet and FRAME_NUM is in five, which is the
fact that makes narrowing necessary — and the pin chooser could not tell you:
it says a packet HAS a field, never how many others do. So "narrow this pin
to a packet" was a decision made without the one number that decides it.

**Tools ▸ Schema ▸ Field index…**, or right-click any decoded field and
choose "Which packets carry X?", which opens the dialog already filtered to
that field.

## The table

    Field              In   Packets
    FRAME_NUM           5   ARP (arp), ARP (arprecv), LSRP, SLRP, …
    SUB_PKT_TYPE        1   SLRP
    LOCO_ID             3   …

- **In** is the point of the table, so counts above one are coloured and the
  column sorts numerically rather than as text.
- **Packets** names them rather than only counting, with the captype token
  shown where it differs from the packet name — `LOCO_SOS (lsos)`,
  `ARP (arprecv)` — because the token is what a pin narrows on and what a
  capture line actually says.
- The filter matches **field names and packet names**, so "what does LSRP
  carry" is the same table read from the other end.
- **"Only fields carried by more than one packet"** is the subset where
  narrowing matters at all, and the checkbox says how many there are.

Built entirely from `Schema::Decoder::fieldsByCaptype()`, added in patch 44
for the nested pin chooser — this is the second reader of it and needed no
new schema code. The dialog holds no traffic and works with no session
loaded.

## Schema reload

The index is rebuilt on reload, because the decoder loop is edit `kavach.xml`
→ reload → look again, and that is exactly when someone has this dialog open.
A field index still showing the previous schema would be worse than none.

## Reachable from both windows

The compare window grew a decoded-fields panel in patch 45, so it forwards
the same request. Neither window opens the dialog itself — both raise a
signal and `MainWindow` opens it, as with pin, plot and Packet Maker.

## Files touched

    fieldindexdialog.{h,cpp}   NEW
    fieldinspector.{h,cpp}     locateFieldRequested
    comparewindow.{h,cpp}      forwards it
    mainwindow.{h,cpp}         Schema menu entry, opener, reload refresh
    tests/test_fieldindex.cpp  NEW — counting, filtering, pre-fill, no-schema
    DLConsole.pro, tests/*.pro

## Verification

    113 suites, 2917 checks, 0 failed      (was 111 / 2891)
    menu audit passed, 24 shortcuts, 59 palette commands
    headless smoke run clean

Note for the audit build: a new Q_OBJECT class needs its header in
menuaudit.pro's HEADERS as well as the source in SOURCES, or moc never runs
on it and the link fails on staticMetaObject. Caught by the audit.
