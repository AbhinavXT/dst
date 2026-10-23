#include "testutil.h"

#include "Structures.h"
#include "capturedecoder.h"
#include "dlrplayer.h"
#include "framerestamp.h"
#include "sessionfile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QThread>
#include <QUdpSocket>

// =============================================================================
//  Session 340 — replayed frames that read as now: FRAME_NUM moved on by the
//  time since the recording, the line's timestamp by the same, the CRC made
//  again. Real lines: replay/loco_1_1_27062026_170217.cap.
// =============================================================================

namespace {
QString firstLine(const char *prefix)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String(prefix))) return l;
    }
    return QString();
}

// A datagram as the archives hold it (test_dlrplayer's shape).
QByteArray makeWire(const QByteArray &payload)
{
    QByteArray wire(int(sizeof(STRUCT_MESSAGE_HEADER)), '\0');
    char *p = wire.data();
    p[0] = char(21);
    p[1] = char(101);
    p[2] = char(7);
    SessionFile::putU16(p + 3, quint16(payload.size()));
    SessionFile::putU16(p + 5, 1);
    return wire + payload;
}

qint64 frameNum(const CaptureLine &c)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(c, nullptr, 0, &raw);
    return raw.value(QStringLiteral("FRAME_NUM"), -1);
}
}  // namespace

TEST_SUITE(session340)
{
    using FrameRestamp::shifted;
    CHECK(shifted(61339, 3600) == 64939, "an hour on");
    CHECK(shifted(86400, 1) == 1 && shifted(1, -1) == 86400, "wraps at midnight, both ways");
    CHECK(shifted(100, 86400 * 3 + 5) == 105, "whole days drop out");

    const qint64 delta = 118 * 86400 + 5 * 3600 + 17;   // 27 June 17:02 -> about 23 October 22:02
    for (const char *prefix : { "@arp_", "@lsrp_", "@slrp_" }) {
        const QString was = firstLine(prefix);
        const QByteArray name = QByteArray(prefix).mid(1, QByteArray(prefix).size() - 2).toUpper();
        CHECK(!was.isEmpty(), name + ": fixture, a real line");
        if (was.isEmpty()) continue;
        const CaptureLine a = CaptureDecoder::parseLine(was);
        int n = 0;
        const QString now = FrameRestamp::line(was, delta, &n);
        const CaptureLine b = CaptureDecoder::parseLine(now);
        CHECK(a.crcChecked && a.crcOk && b.valid && b.crcChecked && b.crcOk, name + ": CRC good before and after");
        CHECK(n >= 1 && frameNum(b) == shifted(int(frameNum(a)), delta),
              name + ": FRAME_NUM " + QByteArray::number(frameNum(a)) + " -> " + QByteArray::number(frameNum(b)));
        CHECK(b.rtc == a.rtc.addSecs(delta), name + ": the line's timestamp moved by the same");
        CHECK(b.seq == a.seq && b.bytes.size() == a.bytes.size() && now.size() == was.size(),
              name + ": the same sequence, frame size and line length");
        // Nothing else in the frame moved: only FRAME_NUM's bits and the CRC.
        QHash<QString, qint64> ra, rb;
        CaptureDecoder::describe(a, nullptr, 0, &ra);
        CaptureDecoder::describe(b, nullptr, 0, &rb);
        QStringList moved;
        for (auto it = ra.cbegin(); it != ra.cend(); ++it)
            if (it.key() != QLatin1String("FRAME_NUM") && rb.value(it.key()) != it.value()) moved << it.key();
        CHECK(moved.isEmpty(), name + ": every other field as it was (moved: " + moved.join(QLatin1Char(',')).toUtf8() + ")");
    }

    // A line with no FRAME_NUM: only its timestamp; not a capture line: as it is.
    const QString dmi = firstLine("@dmi_");
    int n = -1;
    const QString dmiNow = FrameRestamp::line(dmi, 60, &n);
    CHECK(n == 0 && CaptureDecoder::parseLine(dmiNow).bytes == CaptureDecoder::parseLine(dmi).bytes
              && CaptureDecoder::parseLine(dmiNow).rtc == CaptureDecoder::parseLine(dmi).rtc.addSecs(60),
          "a DMI line: bytes as they were, timestamp moved");
    CHECK(FrameRestamp::line(QStringLiteral("Link 1 up"), 60) == QStringLiteral("Link 1 up"), "text: untouched");

    // ---- end to end: Stream session (.dlr) as live, with the box ticked ---------
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("21_1.dlr"));
        const QString arp = firstLine("@arp_"), lsrp = firstLine("@lsrp_");
        const CaptureLine ca = CaptureDecoder::parseLine(arp);
        const qint64 recMs = ca.rtc.toMSecsSinceEpoch();
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            SessionFile::FileHeader h;
            h.createdMs = recMs; h.sourceId = 21; h.kvchId = 1;
            f.write(SessionFile::encodeHeader(h));
            f.write(SessionFile::encodeRecord(recMs, makeWire(arp.toUtf8())));
            f.write(SessionFile::encodeRecord(recMs + 100, makeWire(lsrp.toUtf8())));
        }
        DlrPlayer::Source src;
        QString err;
        CHECK(DlrPlayer::probe(path, &src, &err), "fixture: a .dlr of a real ARP and LSRP");
        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "fixture: a receiving socket");
        DlrPlayer player;
        player.setSources({ src });
        player.setTarget(QHostAddress::LocalHost, rx.localPort());
        player.setSpeed(0);              // burst
        player.setRestamp(true);
        const qint64 sentAround = QDateTime::currentMSecsSinceEpoch();
        player.play();
        QVector<QByteArray> got;
        QElapsedTimer t;
        t.start();
        while (got.size() < 2 && t.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            while (rx.hasPendingDatagrams()) {
                QByteArray buf(int(rx.pendingDatagramSize()), '\0');
                buf.truncate(int(rx.readDatagram(buf.data(), buf.size())));
                got << buf;
            }
            QThread::msleep(1);
        }
        player.wait(2000);
        CHECK(got.size() == 2, "both frames arrive");
        const int hdr = int(sizeof(STRUCT_MESSAGE_HEADER));
        for (const QByteArray &w : got) {
            const QString text = QString::fromUtf8(w.mid(hdr));
            const CaptureLine c = CaptureDecoder::parseLine(text);
            const int len = quint8(w.at(3)) | (quint8(w.at(4)) << 8);
            CHECK(len == w.size() - hdr && c.crcChecked && c.crcOk, "message_len matches, CRC good");
            // As old as it was when recorded, now: within a second of the
            // recording's own lag between FRAME_NUM and arrival.
            const int sodNow = QDateTime::fromMSecsSinceEpoch(sentAround).time().msecsSinceStartOfDay() / 1000 + 1;
            const CaptureLine orig = CaptureDecoder::parseLine(text.startsWith(QLatin1String("@arp")) ? arp : lsrp);
            const int sodRec = QDateTime::fromMSecsSinceEpoch(recMs).time().msecsSinceStartOfDay() / 1000 + 1;
            const qint64 lagThen = frameNum(orig) - sodRec, lagNow = frameNum(c) - sodNow;
            CHECK(qAbs(lagNow - lagThen) <= 2, QByteArray("FRAME_NUM reads as now (lag then ") + QByteArray::number(lagThen)
                                                   + " s, now " + QByteArray::number(lagNow) + " s)");
        }
    }
}
