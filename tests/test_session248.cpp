#include "testutil.h"

#include "logentry.h"
#include "nmslog.h"

#include <QFile>
#include <QTextStream>

// =============================================================================
//  Session 248 — the NMS tables' column order: PC time, rtc, seq, loco / ctrl,
//  frame len, CRC; the NMS header; the packet's events; the frame in hex.
//  Real frames from replay/loco_1_1_26062026_162418.cap (all three types).
// =============================================================================

namespace {

QVector<LogEntryPtr> nmsEntries(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;   // an arbitrary PC clock
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@nms"))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 100;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

QStringList headers(const NmsLog::Table &t)
{
    QStringList h;
    for (int c = 0; c < t.columnCount(); ++c) h << t.header(c);
    return h;
}

}  // namespace

TEST_SUITE(session248)
{
    const QVector<LogEntryPtr> es = nmsEntries(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"));
    CHECK(es.size() > 1000, "fixture: replay/loco_1_1_26062026_162418.cap has NMS packets");

    const QStringList lead = { "PC time", "rtc", "seq", "loco / ctrl", "frame len", "CRC" };
    const QStringList header = { "nms.start_of_frame", "nms.message_type", "nms.message_length", "nms.message_seq",
                                 "nms.onboard_kavach_id", "nms.system_id", "nms.system_version", "nms.date",
                                 "nms.time" };
    const struct { NmsLog::Kind kind; const char *firstEvent; } kinds[] = {
        { NmsLog::Kind::Health, "RADIO_1_HEALTH" },
        { NmsLog::Kind::Fault,  "reporting_subsystem" },
        { NmsLog::Kind::Rssi,   "stationary_kavach_id" },
    };
    for (const auto &k : kinds) {
        NmsLog::Table t;
        for (const LogEntryPtr &e : es) NmsLog::append(t, k.kind, *e);
        const QString what = NmsLog::token(k.kind);
        const QStringList h = headers(t);
        CHECK(t.rows.size() > 10, qPrintable(what + ": rows"));
        CHECK(h.mid(0, lead.size()) == lead,
              qPrintable(what + ": PC time, rtc, seq, loco / ctrl, frame len, CRC first; got " + h.mid(0, 6).join(" | ")));
        CHECK(h.mid(lead.size(), header.size()) == header,
              qPrintable(what + ": then the full NMS header, in frame order"));
        CHECK(h.value(lead.size() + header.size()) == QLatin1String(k.firstEvent),
              qPrintable(what + ": then the events, from " + k.firstEvent));
        CHECK(h.last() == QLatin1String("Frame (hex)"), qPrintable(what + ": the frame in hex last"));
        const QString crc = t.cell(0, lead.indexOf("CRC"));
        CHECK(crc == QLatin1String("PASS") || crc == QLatin1String("FAIL"), qPrintable(what + ": CRC is PASS or FAIL"));
        CHECK(t.cell(0, lead.indexOf("frame len")).endsWith(QLatin1String(" B")), qPrintable(what + ": frame len in bytes"));
    }

    // A column first seen later (a fault packet with more faults) joins the
    // events, never the capture line or header.
    {
        NmsLog::Table t;
        QVector<LogEntryPtr> faults;
        for (const LogEntryPtr &e : es) if (e->text.startsWith(QLatin1String("@nmsflt_"))) faults << e;
        int one = -1, many = -1;
        for (int i = 0; i < faults.size(); ++i) {
            NmsLog::Table probe;
            NmsLog::append(probe, NmsLog::Kind::Fault, *faults.at(i));
            const QString n = probe.cell(0, probe.index.value(QStringLiteral("fault_count")));
            if (n == QLatin1String("1") && one < 0) one = i;
            if (n.toInt() >= 3 && many < 0) many = i;
        }
        CHECK(one >= 0 && many >= 0, "fixture: a one-fault and a three-or-more-fault packet");
        if (one >= 0 && many >= 0) {
            NmsLog::append(t, NmsLog::Kind::Fault, *faults.at(one));
            NmsLog::append(t, NmsLog::Kind::Fault, *faults.at(many));
            const QStringList h = headers(t);
            CHECK(h.mid(0, lead.size()) == lead && h.mid(lead.size(), header.size()) == header,
                  "fault[3] added later: the capture line and header unmoved");
            CHECK(t.index.value(QStringLiteral("fault[3] fault_id")) > t.index.value(QStringLiteral("fault[1] fault_id"))
                      && h.last() == QLatin1String("Frame (hex)"),
                  "fault[3] added later: among the events, before the frame");
        }
    }
}
