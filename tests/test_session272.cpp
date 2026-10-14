#include "testutil.h"

#include "capturedecoder.h"
#include "dmipanel.h"

#include <QDir>
#include <QFile>

// =============================================================================
//  Session 272 — the DMI context message "SOS – From Station N ( SOS to This
//  Loco )" names the station from the DMI field station_id, not
//  sos_station_id. "SOS to All Locos" is unchanged (sos_station_id).
// =============================================================================

TEST_SUITE(session272)
{
    const QHash<QString, QString> text;
    {
        const QHash<QString, qint64> raw{ { "station_id", 4501 }, { "sos_station_id", 1201 } };
        CHECK(dmiContextMessage(QStringLiteral("SOS - Station (This Loco)"), raw, text)
                  == QString::fromUtf8("SOS – From Station 4501 ( SOS to This Loco )"),
              "SOS to This Loco: the station is station_id");
        CHECK(dmiContextMessage(QStringLiteral("SOS - Station (All Locos)"), raw, text)
                  == QString::fromUtf8("SOS – From Station 1201 ( SOS to All Locos )"),
              "SOS to All Locos: still sos_station_id");
    }

    // Real @dmi frames: the panel state's message carries the frame's station_id.
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    int frames = 0, checked = 0, agree = 0;
    for (const QString &f : dir.entryList({ QStringLiteral("loco_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && frames < 20000) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (!l.startsWith(QLatin1String("@dmi_"))) continue;
            ++frames;
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(CaptureDecoder::parseLine(l), nullptr, 0, &raw);
            if (!raw.contains(QStringLiteral("station_id"))) continue;
            ++checked;
            if (dmiContextMessage(QStringLiteral("SOS - Station (This Loco)"), raw, text)
                    .contains(QString::number(raw.value(QStringLiteral("station_id")))))
                ++agree;
        }
    }
    CHECK(frames > 1000, QByteArray("real @dmi frames in replay/: ") + QByteArray::number(frames));
    CHECK(checked > 1000 && agree == checked,
          QByteArray("real frames: This Loco names the frame's station_id: ") + QByteArray::number(agree) + "/"
              + QByteArray::number(checked));
}
