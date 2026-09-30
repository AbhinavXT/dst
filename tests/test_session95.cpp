#include "testutil.h"

#include "capturedecoder.h"
#include "framenumberwatch.h"
#include "packetmakerdialog.h"

#include <QDir>
#include <QFile>

// =============================================================================
//  Session 95: FrameNumberWatch is no longer a process-wide singleton.
//  MainWindow owns the one fed by live traffic and hands it to the Packet
//  Makers; anything else (a test, a second console) has its own.
// =============================================================================

TEST_SUITE(session95)
{
    QString lsrp;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && lsrp.isEmpty()) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@lsrp_"))) lsrp = l;
        }
        if (!lsrp.isEmpty()) break;
    }
    CHECK(!lsrp.isEmpty(), "a real @lsrp frame");

    FrameNumberWatch a, b;
    a.observeLine(CaptureDecoder::parseLine(lsrp));
    CHECK(a.latest().valid(), "a watch that saw the frame has its number");
    CHECK(!b.latest().valid(), "another watch saw nothing: no shared state between them");

    bool live = true;
    PacketMakerDialog::seedFrameNumber(nullptr, 17, &live);
    CHECK(!live, "no watch (no live traffic): the seed comes from the clock, and says so");
    const qint64 seeded = PacketMakerDialog::seedFrameNumber(&a, 17, &live);
    CHECK(live && seeded == (a.latest().value & ((1 << 17) - 1)), "a watch: the seed is its latest frame number");
    PacketMakerDialog::seedFrameNumber(&b, 17, &live);
    CHECK(!live, "the empty watch does not borrow the other's number");
}
