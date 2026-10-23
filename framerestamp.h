#ifndef FRAMERESTAMP_H
#define FRAMERESTAMP_H

// =============================================================================
//  framerestamp.h -- replayed frames that read as now (session 340)
//  ---------------------------------------------------------------------------
//  Stream session (.dlr) as live re-sends a recording through the live path,
//  and every frame in it carries the FRAME_NUM of the day it was recorded:
//  the frame clocks read hours off, the clock-skew alarm fires, the Packet
//  Maker seeds from a stale counter, and the accept window (> 4 s old) says
//  a receiver would drop everything.
//
//  Restamped, each capture line's FRAME_NUM moves on by the time since the
//  recording (seconds since midnight + 1, wrapping at midnight), its own
//  timestamp token by the same, and its CRC is made again. Offsets inside
//  the recording are kept: a loco that was 2 s behind the station still is.
//
//  Not remade: an SLRP's MAC. It is keyed with the session key, and a frame
//  whose FRAME_NUM changed fails its MAC check where one is made.
// =============================================================================

#include <QByteArray>
#include <QString>

namespace FrameRestamp {

// `text` with every FRAME_NUM in its frame moved on by `deltaSec` (taken mod
// one day), its timestamp token moved by `deltaSec`, and the frame's CRC
// remade. Only ARP, LSRP, SLRP and AAP frames carry a FRAME_NUM this
// rewrites; any other line has only its timestamp moved. Not a capture
// line: returned as it is. `frames` (optional) is set to how many
// FRAME_NUMs were rewritten.
QString line(const QString &text, qint64 deltaSec, int *frames = nullptr);

// A recorded datagram (STRUCT_MESSAGE_HEADER + capture line) with its line
// restamped as above and message_len set to the new line's length.
QByteArray wire(const QByteArray &wire, qint64 deltaSec);

// The frame number `deltaSec` after `frameNum` (1..86400).
int shifted(int frameNum, qint64 deltaSec);

}  // namespace FrameRestamp

#endif  // FRAMERESTAMP_H
