#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "uicolors.h"

#include <QBrush>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 252 — NMS tables: packets missing. @nmshlth / @nmsrssi come about
//  once a second (their message_seq does not count): 3 s or more on the RTC
//  is a gap. @nmsflt's message_seq counts by one: a jump, a repeat or a
//  restart is a gap. Real frames from replay/; the expected gaps are worked
//  out here from the capture lines themselves (the RTC token, bytes 13..14).
// =============================================================================

namespace {

struct Line { LogEntryPtr e; qint64 rtc; int msgSeq; };

QVector<Line> linesOf(const QString &path, const QString &token)
{
    QVector<Line> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1Char('@') + token + QLatin1Char('_'))) continue;
        const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        const qint64 rtc = QDateTime::fromString(tok.value(1), Qt::ISODate).toMSecsSinceEpoch() / 1000;
        const int seq = (tok.value(3 + 13).toInt(nullptr, 16) << 8) | tok.value(3 + 14).toInt(nullptr, 16);
        out << Line{ e, rtc, seq };
    }
    return out;
}

NmsLog::Table tableOf(NmsLog::Kind k, const QVector<Line> &ls)
{
    NmsLog::Table t;
    for (const Line &l : ls) NmsLog::append(t, k, *l.e);
    return t;
}

QVector<int> gapRows(const NmsLog::Table &t)
{
    QVector<int> out;
    for (int r = 0; r < t.gapColumn.size(); ++r) if (!t.gapColumn.at(r).isEmpty()) out << r;
    return out;
}

}  // namespace

TEST_SUITE(session252)
{
    // ---- health and RSSI: the RTC ----
    const QString cap = QStringLiteral("../replay/loco_1_1_29062026_134509.cap");
    for (const char *tok : { "nmshlth", "nmsrssi" }) {
        const QString token = QLatin1String(tok);
        const NmsLog::Kind k = token == QLatin1String("nmshlth") ? NmsLog::Kind::Health : NmsLog::Kind::Rssi;
        const QVector<Line> ls = linesOf(cap, token);
        QVector<int> want;
        bool seqCounts = false;
        for (int i = 1; i < ls.size(); ++i) {
            if (ls.at(i).rtc - ls.at(i - 1).rtc >= 3 || ls.at(i).rtc < ls.at(i - 1).rtc) want << i;
            if (ls.at(i).msgSeq != ls.at(i - 1).msgSeq) seqCounts = true;
        }
        CHECK(ls.size() == 2081 && !seqCounts, qPrintable(token + ": fixture, 2081 packets; message_seq never changes"));
        const NmsLog::Table t = tableOf(k, ls);
        CHECK(!want.isEmpty() && gapRows(t) == want && t.gaps == want.size(),
              qPrintable(QStringLiteral("%1: the gaps are where the RTC steps 3 s or more (%2)").arg(token).arg(want.size())));
        bool allRtc = true, allChanged = true;
        for (int r : gapRows(t)) {
            allRtc = allRtc && t.gapColumn.at(r) == QLatin1String("rtc")
                     && t.gapNote.at(r).contains(QStringLiteral("%1 s since").arg(ls.at(r).rtc - ls.at(r - 1).rtc));
            allChanged = allChanged && t.changed.at(r);
        }
        CHECK(allRtc, qPrintable(token + ": each on the rtc cell, saying how many seconds"));
        CHECK(allChanged, qPrintable(token + ": and kept by Changes only"));
    }

    // ---- fault: message_seq ----
    struct FaultCase { const char *cap; };
    int restarts = 0, jumps = 0;
    for (const char *c : { "loco_1_1_26062026_162418", "loco_1_1_29062026_134509", "loco_2_1_27062026_140226" }) {
        const QVector<Line> ls = linesOf(QStringLiteral("../replay/%1.cap").arg(QLatin1String(c)), QStringLiteral("nmsflt"));
        QVector<int> want;
        for (int i = 1; i < ls.size(); ++i) {
            const int step = (ls.at(i).msgSeq - ls.at(i - 1).msgSeq) & 0xFFFF;
            if (step != 1) want << i;
        }
        const NmsLog::Table t = tableOf(NmsLog::Kind::Fault, ls);
        CHECK(!want.isEmpty() && gapRows(t) == want,
              qPrintable(QStringLiteral("fault %1: a gap wherever message_seq does not count on by one (%2)").arg(c).arg(want.size())));
        for (int r : gapRows(t)) {
            CHECK(t.gapColumn.at(r) == QLatin1String("nms.message_seq"), "fault: on the message_seq cell");
            if (t.gapNote.at(r).contains(QLatin1String("started again"))) ++restarts;
            if (t.gapNote.at(r).contains(QLatin1String("not in the log")) && !t.gapNote.at(r).contains(QLatin1String("started")))
                ++jumps;
        }
    }
    CHECK(restarts >= 3 && jumps >= 2, qPrintable(QStringLiteral("fault: restarts (%1) and jumps (%2) both seen").arg(restarts).arg(jumps)));
    {
        // The note for a jump: 162418 restarts at row 11; 134509 jumps.
        const QVector<Line> ls = linesOf(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"), QStringLiteral("nmsflt"));
        const NmsLog::Table t = tableOf(NmsLog::Kind::Fault, ls);
        CHECK(gapRows(t).value(0) == 10 && t.gapNote.at(10).contains(QStringLiteral("message_seq %1 then %2").arg(ls.at(9).msgSeq).arg(ls.at(10).msgSeq)),
              "fault 162418: row 11 (seq 12 then 2): the count started again");
    }

    // ---- the window: marked, counted, live ----
    {
        const QVector<Line> ls = linesOf(cap, QStringLiteral("nmshlth"));
        const NmsLog::Table all = tableOf(NmsLog::Kind::Health, ls);
        const int firstGap = gapRows(all).value(0);
        LogModel model(nullptr, 100000);
        QVector<LogEntryPtr> head, tail;
        for (int i = 0; i < ls.size(); ++i) (i < firstGap ? head : tail) << ls.at(i).e;
        model.appendEntries(head);
        NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
        CHECK(w.table().gaps == 0, "open before the first gap: none");
        model.appendEntries(tail);   // the gap arrives live, across the batch boundary
        QCoreApplication::processEvents();
        CHECK(w.table().gaps == all.gaps && gapRows(w.table()) == gapRows(all), "live: the same gaps as built at once");
        QAbstractItemModel *m = w.view()->model();
        const QModelIndex cell = m->index(firstGap, w.table().index.value(QStringLiteral("rtc")));
        CHECK(cell.data(Qt::ForegroundRole).value<QBrush>().color() == UiColor::warning()
                  && cell.data(Qt::ToolTipRole).toString().contains(QLatin1String("since the @nmshlth before")),
              "the rtc cell: warning colour, the note in its tooltip");
        const QModelIndex next = m->index(firstGap + 1, w.table().index.value(QStringLiteral("rtc")));
        CHECK(!next.data(Qt::ForegroundRole).isValid(), "the row after it: not marked");
        QLabel *count = nullptr;
        for (QLabel *l : w.findChildren<QLabel *>()) if (l->text().contains(QLatin1String("packets"))) count = l;
        CHECK(count && count->text().endsWith(QStringLiteral("; %1 gaps").arg(all.gaps)),
              qPrintable("the count says how many gaps: " + (count ? count->text() : QString())));
    }
}
