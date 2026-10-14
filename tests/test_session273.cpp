#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "nmslog.h"

#include <QFile>
#include <QSet>
#include <QTextStream>

// =============================================================================
//  Session 273 — the first REAL @dllog capture
//  (replay/loco_81_1_14102026_110915.cap, loco 81 port 1). Its LP-OCIP alert
//  packets (log 4 / sub 7) are 49 bytes, message_length 49: popup_loco_id is
//  a U32, not the 8 bits patch 267 read (46 bytes). The periodic packets
//  (3 / 1) were right.
// =============================================================================

namespace {

const char *kReal = "../replay/loco_81_1_14102026_110915.cap";

QVector<LogEntryPtr> dllogOf(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1792130400000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@dllog_"))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 81;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

QHash<QString, QString> rowsOf(const QString &line)
{
    QHash<QString, QString> h;
    for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(line)))
        h.insert(r.field.trimmed(), r.value.trimmed());
    return h;
}

}  // namespace

TEST_SUITE(session273)
{
    const QVector<LogEntryPtr> es = dllogOf(QString::fromLatin1(kReal));
    CHECK(es.size() == 172, QByteArray("real capture: 172 @dllog lines, got ") + QByteArray::number(es.size()));

    int periodic = 0, alert = 0, other = 0, lengthOk = 0, alertOk = 0, periodicOk = 0;
    QSet<QString> eoaDistances;
    for (const LogEntryPtr &e : es) {
        const CaptureLine c = CaptureDecoder::parseLine(e->text);
        const QHash<QString, QString> h = rowsOf(e->text);
        if (c.type == CapType::DlLog && h.value("msg.message_length") == QString::number(c.bytes.size())
                && h.value("frame len") == QString::number(c.bytes.size()) + QLatin1String(" B"))
            ++lengthOk;
        const int lt = c.bytes.size() > 19 ? quint8(c.bytes.at(18)) : -1;
        const int sub = c.bytes.size() > 19 ? quint8(c.bytes.at(19)) : -1;
        if (lt == 3 && sub == 1) {
            ++periodic;
            if (h.value("loco_id") == QLatin1String("1") && h.value("tin") == QLatin1String("52")) ++periodicOk;
        } else if (lt == 4 && sub == 7) {
            ++alert;
            const QString alarm = h.value("alarm_code"), dist = h.value("popup_distance");
            const bool eoa = alarm == QLatin1String("End of Authority") && dist != QLatin1String("0 m");
            const bool none = alarm == QLatin1String("no_alarm") && dist == QLatin1String("0 m");
            if (eoa) eoaDistances.insert(dist);
            if ((eoa || none) && c.bytes.size() == 49 && h.value("popup_time") == QLatin1String("3 s")
                    && h.value("popup_loco_id") == QLatin1String("0")
                    && h.value("popup_train_type") == QLatin1String("1 (LIGHT_ENGINE)")
                    && h.value("popup_pressure") == QLatin1String("0") && h.value("popup_station_id") == QLatin1String("0"))
                ++alertOk;
        } else {
            ++other;
        }
    }
    CHECK(periodic == 87 && alert == 85 && other == 0, "real capture: 87 periodic (3/1), 85 LP-OCIP alert (4/7), nothing else");
    CHECK(lengthOk == es.size(), QByteArray("every real frame is as long as its message_length says: ")
                                     + QByteArray::number(lengthOk) + "/" + QByteArray::number(es.size()));
    CHECK(alertOk == alert, QByteArray("LP-OCIP alert: U32 popup_loco_id lines the fields up (End of Authority at a "
                                       "distance or no_alarm at 0 m, LIGHT_ENGINE): ")
                                + QByteArray::number(alertOk) + "/" + QByteArray::number(alert));
    CHECK(eoaDistances == QSet<QString>({ QStringLiteral("724 m"), QStringLiteral("622 m") }),
          "LP-OCIP alert: End of Authority popups at 724 m and 622 m");
    CHECK(periodicOk == periodic, "periodic: loco_id 1, tin 52 in every real frame");
    {
        const QHash<QString, QString> h = rowsOf(es.value(0)->text);
        CHECK(h.value("log_time") == QLatin1String("14-10-26 11:10:23") && h.value("abs_location") == QLatin1String("160605 m"),
              "first frame: log_time 14-10-26 11:10:23, abs_location 160605 m");
    }

    // The DL log table: one row per real packet.
    NmsLog::Table t;
    for (const LogEntryPtr &e : es) NmsLog::append(t, NmsLog::Kind::DlLog, *e);
    CHECK(t.rows.size() == es.size() && t.index.contains(QStringLiteral("popup_station_id"))
              && t.index.contains(QStringLiteral("loco_id")),
          "DL log table: a row per real packet, with the periodic and popup columns");
}
