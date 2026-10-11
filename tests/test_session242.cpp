#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "packetmakerdialog.h"

#include <QDateTime>
#include <QPushButton>
#include <QSignalSpy>

// =============================================================================
//  Session 242 — Packet Maker's "Show in tab": the built frame enters the
//  pipeline as an @-capture line under source 253 (serial is 254), so it is
//  decoded in a "Packet Maker loopback" tab like a received frame. Nothing
//  is sent. ARP/LSRP are logged with a 10-byte envelope (header + station
//  id); the window sends 8, so the last 2 are zero and the status says so.
//  Frames are real: SLRP reference frame 6 and an @arp_1_2 line (replay/).
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

// replay/loco_1_2_27062026_170159.cap
const char kArpLine[] =
    "@arp_1_2 2026-06-27T17:02:00 3822 02 07 0D 00 27 00 00 00 0F 02 D3 AE F8 90 00 01 40 9D "
    "68 C7 D0 3D 0C 04 1E 05 0D 00 00 00 00 00 08 32 00 4D 40 8A A7";

QString valueOf(const QVector<FieldRow> &rows, const char *field)
{
    for (const FieldRow &r : rows) if (r.field.trimmed() == QLatin1String(field)) return r.value;
    return QString();
}

} // namespace

TEST_SUITE(session242)
{
    const QDateTime at(QDate(2026, 10, 12), QTime(9, 30, 5));
    const QByteArray slrp = QByteArray::fromHex(QByteArray(kSlrpRef6).replace(' ', ""));

    // ---- SLRP: no envelope ------------------------------------------------------------------
    QString err, note;
    const QString sl = PacketMakerDialog::loopbackLine(QStringLiteral("slrp"), slrp, QByteArray(), 0, at, &err, &note);
    CHECK(sl.startsWith(QLatin1String("@slrp_0_0 2026-10-12T09:30:05 0 91 B9 AA")), "the line: " + sl.left(40).toUtf8());
    const CaptureLine c = CaptureDecoder::parseLine(sl);
    CHECK(c.valid && c.typeToken == QLatin1String("slrp") && c.bytes == slrp, "it parses back to the same bytes");
    CHECK(note.isEmpty(), "no note without an envelope");

    // ---- ARP: a 10-byte envelope, 8 given ---------------------------------------------------
    const CaptureLine orig = CaptureDecoder::parseLine(QString::fromLatin1(kArpLine));
    const QByteArray arpFrame = orig.bytes.mid(10);
    const QString al = PacketMakerDialog::loopbackLine(QStringLiteral("arp"), arpFrame, orig.bytes.left(8), 10, at, &err, &note);
    CHECK(!al.isEmpty() && note.contains(QLatin1String("2 byte(s)")), "8 of 10 B: padded, and said: " + note.toUtf8());
    const CaptureLine back = CaptureDecoder::parseLine(al);
    const QString fnWas = valueOf(CaptureDecoder::describe(orig), "FRAME_NUM");
    const QString fnNow = valueOf(CaptureDecoder::describe(back), "FRAME_NUM");
    CHECK(!fnWas.isEmpty() && fnWas == fnNow, "the tab decodes the same FRAME_NUM as the logged line (" + fnNow.toUtf8() + ")");
    const QString locoWas = valueOf(CaptureDecoder::describe(orig), "SOURCE_LOCO_ID");
    CHECK(!locoWas.isEmpty() && locoWas == valueOf(CaptureDecoder::describe(back), "SOURCE_LOCO_ID"),
          "and the same SOURCE_LOCO_ID: the body is aligned");
    err.clear();
    CHECK(PacketMakerDialog::loopbackLine(QStringLiteral("arp"), arpFrame, QByteArray(12, '\1'), 10, at, &err).isEmpty()
              && !err.isEmpty(), "an envelope longer than the log's is refused");

    // ---- into a tab -------------------------------------------------------------------------
    MessageDispatcher disp;
    QSignalSpy tabs(&disp, &MessageDispatcher::tabRequested);
    disp.ingestLocal(PacketMakerDialog::kLoopbackSourceId, 0, sl.toUtf8(), 1000, QStringLiteral("Packet Maker loopback"));
    disp.drainNow();
    LogModel *m = disp.modelForKey(QStringLiteral("253_0"));
    CHECK(m && m->count() == 1 && m->entryAt(0)->text == sl, "the line lands in tab 253_0");
    CHECK(tabs.count() == 1 && tabs.at(0).at(1).toString() == QLatin1String("Packet Maker loopback"),
          "named Packet Maker loopback");

    // ---- the window -------------------------------------------------------------------------
    PacketMakerDialog d;
    d.loadBuffer(QString::fromLatin1(kSlrpRef6));
    QPushButton *build = nullptr, *show = nullptr;
    for (QPushButton *b : d.findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Build && Verify")) build = b;
        if (b->text() == QLatin1String("Show in tab")) show = b;
    }
    CHECK(show && show->isHidden(), "hidden until a pipeline is connected");
    if (!(show && build)) return;
    d.setLoopbackAvailable(true);
    CHECK(!show->isHidden(), "shown once it is");
    build->click();
    CHECK(show->isEnabled(), "live after a build, without arming");
    QSignalSpy asked(&d, &PacketMakerDialog::loopbackRequested);
    show->click();
    CHECK(asked.count() == 1, "a click hands one line over");
    if (asked.count() == 1) {
        const CaptureLine got = CaptureDecoder::parseLine(asked.at(0).at(0).toString());
        CHECK(got.valid && got.typeToken == QLatin1String("slrp") && got.bytes.size() == slrp.size(),
              "an slrp line of the frame's size (rebuilt with the current FRAME_NUM)");
    }
}
