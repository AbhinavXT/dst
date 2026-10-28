#include "testutil.h"

#include "faulttimeline.h"
#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
//  Session 255 — NMS fault log: the fault summary. Each fault from the packet
//  that first lists it to the first later packet from the same subsystem that
//  no longer does. Checked against FaultTimeline::build (session 174), which
//  applies the same rule on its own, on three real captures in replay/.
// =============================================================================

namespace {

QVector<LogEntryPtr> faultsOf(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@nmsflt_"))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

}  // namespace

TEST_SUITE(session255)
{
    int totalSpans = 0, totalOpen = 0, totalClosed = 0;
    for (const char *cap : { "loco_1_1_26062026_162418", "loco_1_1_29062026_134509", "loco_2_1_27062026_140226" }) {
        const QVector<LogEntryPtr> fl = faultsOf(QStringLiteral("../replay/%1.cap").arg(QLatin1String(cap)));
        LogModel model(nullptr, 100000);
        model.appendEntries(fl);
        NmsLog::Table t;
        for (const LogEntryPtr &e : fl) NmsLog::append(t, NmsLog::Kind::Fault, *e);
        const FaultTimeline::Timeline tl = FaultTimeline::build(&model);
        QVector<FaultTimeline::Bar> bars;
        for (const FaultTimeline::Bar &b : tl.bars) if (b.source == QLatin1String("NMS")) bars << b;
        bool same = bars.size() == t.faultSpans.size() && !bars.isEmpty();
        QString first;
        for (int i = 0; same && i < bars.size(); ++i) {
            const NmsLog::Table::FaultSpan &sp = t.faultSpans.at(i);
            const bool open = sp.toRow < 0;
            same = bars.at(i).fromMs == t.epochMs.at(sp.fromRow) && bars.at(i).open == open
                   && (open || bars.at(i).toMs == t.epochMs.at(sp.toRow))
                   && sp.module.contains(bars.at(i).row) && sp.fault.contains(bars.at(i).fault);
            if (!same) first = QStringLiteral(" (span %1: %2 / %3)").arg(i).arg(sp.module, sp.fault);
            if (open) ++totalOpen; else ++totalClosed;
        }
        totalSpans += t.faultSpans.size();
        CHECK(same, qPrintable(QStringLiteral("%1: %2 fault spans, the same as the fault timeline's bars%3")
                                   .arg(QLatin1String(cap)).arg(t.faultSpans.size()).arg(first)));
        // Packets listing each: from its first to its last listing, every packet from that subsystem lists it.
        bool counted = true;
        for (const NmsLog::Table::FaultSpan &sp : t.faultSpans)
            counted = counted && sp.packets >= 1 && sp.lastRow >= sp.fromRow && sp.packets <= sp.lastRow - sp.fromRow + 1
                      && (sp.toRow < 0 || sp.toRow > sp.lastRow);
        CHECK(counted, qPrintable(QStringLiteral("%1: each span's packet count and rows are consistent").arg(QLatin1String(cap))));
    }
    {
        // The duplicate listing in 134509 at 14:20:55: one packet, counted once.
        const QVector<LogEntryPtr> d = faultsOf(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"));
        NmsLog::Table t;
        for (const LogEntryPtr &e : d) NmsLog::append(t, NmsLog::Kind::Fault, *e);
        int row = -1;
        for (int r = 0; r < t.rows.size() && row < 0; ++r)
            if (t.cell(r, t.index.value(QStringLiteral("rtc"))) == QLatin1String("2026-06-29 14:20:55")) row = r;
        int spans = 0;
        for (const NmsLog::Table::FaultSpan &sp : t.faultSpans)
            if (sp.module.startsWith(QLatin1String("17 ")) && sp.fault.startsWith(QLatin1String("100 ")) && sp.codeType == 2
                && sp.fromRow <= row && (sp.toRow < 0 || sp.toRow > row)) ++spans;
        CHECK(row > 0 && spans == 1, "a fault listed twice in one packet: one span");
    }
    CHECK(totalOpen > 0 && totalClosed > 0,
          qPrintable(QStringLiteral("spans that end and spans still listed at the end both seen (%1 / %2)").arg(totalClosed).arg(totalOpen)));

    // The summary lines: the first fault of 162418 is 2 (VCC Mc-2) / 26 (safety error).
    const QVector<LogEntryPtr> fl = faultsOf(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"));
    NmsLog::Table t;
    for (const LogEntryPtr &e : fl) NmsLog::append(t, NmsLog::Kind::Fault, *e);
    const QVector<QStringList> sum = NmsLog::faultSummary(t);
    CHECK(sum.size() == t.faultSpans.size() && NmsLog::faultSummaryHeader().size() == 9, "one summary line per span, 9 columns");
    CHECK(sum.value(0).value(0) == QLatin1String("2 (VCC Mc-2)") && sum.value(0).value(1) == QLatin1String("26 (safety error)")
              && sum.value(0).value(2) == QLatin1String("0X01 (Faulty)") && sum.value(0).value(3).startsWith(QLatin1String("34")),
          qPrintable("first line: module, fault, code type, subsystem: " + sum.value(0).join(" | ")));
    CHECK(sum.value(0).value(5) == t.cell(0, t.index.value(QStringLiteral("rtc"))), "first listed (rtc) is that packet's rtc");
    {
        const NmsLog::Table::FaultSpan &sp = t.faultSpans.at(0);
        const qint64 secs = (t.epochMs.at(sp.toRow) - t.epochMs.at(sp.fromRow)) / 1000;
        CHECK(sp.toRow > 0 && sum.value(0).value(7) == QStringLiteral("0:%1:%2").arg(secs / 60, 2, 10, QLatin1Char('0')).arg(secs % 60, 2, 10, QLatin1Char('0')),
              qPrintable("listed for: no longer listed minus first listed, h:mm:ss: " + sum.value(0).value(7)));
    }
    {
        int open = -1;
        for (int i = 0; i < t.faultSpans.size() && open < 0; ++i) if (t.faultSpans.at(i).toRow < 0) open = i;
        CHECK(open < 0 || (sum.value(open).value(6) == QLatin1String("still listed in the last packet")
                           && sum.value(open).value(7).startsWith(QLatin1String("at least "))),
              "a fault still listed at the end: says so, and 'at least' its time so far");
    }

    // The window: built live in two batches, the same spans; the dialog.
    {
        LogModel model(nullptr, 100000);
        model.appendEntries(fl.mid(0, 7));
        NmsLogWindow w(NmsLog::Kind::Fault, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
        model.appendEntries(fl.mid(7));
        QApplication::processEvents();
        bool same = w.table().faultSpans.size() == t.faultSpans.size();
        for (int i = 0; same && i < t.faultSpans.size(); ++i)
            same = w.table().faultSpans.at(i).fromRow == t.faultSpans.at(i).fromRow
                   && w.table().faultSpans.at(i).toRow == t.faultSpans.at(i).toRow
                   && w.table().faultSpans.at(i).packets == t.faultSpans.at(i).packets;
        CHECK(same, "built live across a batch boundary: the same spans, rows and counts");
        CHECK(w.findChild<QPushButton *>(QStringLiteral("nmsFaultSummary")) != nullptr, "the fault log has Fault summary…");
        QDialog *d = w.faultSummary();
        auto *tw = d->findChild<QTableWidget *>(QStringLiteral("nmsFaultSpans"));
        CHECK(tw && tw->rowCount() == sum.size() && tw->item(0, 1)->text() == QLatin1String("26 (safety error)"),
              "the dialog lists the spans");
        QSet<QString> distinct;
        for (const QStringList &l : sum) distinct.insert(l.mid(0, 4).join(QLatin1Char('|')));
        QLabel *note = nullptr;
        for (QLabel *l : d->findChildren<QLabel *>()) if (l->text().contains(QLatin1String("distinct"))) note = l;
        CHECK(note && note->text().startsWith(QStringLiteral("%1 distinct fault(s) in %2 packet(s), listed %3 time(s)")
                                                  .arg(distinct.size()).arg(fl.size()).arg(sum.size())),
              qPrintable("the note counts distinct faults and listings: " + (note ? note->text() : QString())));
        qint64 jumped = -1;
        QObject::connect(&w, &NmsLogWindow::jumpRequested, [&](const QString &, qint64 ms) { jumped = ms; });
        if (tw) emit tw->cellDoubleClicked(1, 0);
        CHECK(jumped == w.table().epochMs.at(w.table().faultSpans.at(1).fromRow), "double-click: the packet that first listed it");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("s.csv"));
        CHECK(w.saveFaultSummaryCsv(path), "export: saved");
        QFile f(path);
        const QByteArray csv = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        CHECK(csv.startsWith("\xEF\xBB\xBFModule,Fault,Code type") && csv.count("\r\n") == sum.size() + 1,
              "export: a header and one line per fault");
        // Live while open: a new packet that lists a new fault adds a line.
        d->show();
        const int before = tw->rowCount();
        LogModel m2(nullptr, 1000);
        NmsLogWindow w2(NmsLog::Kind::Fault, &m2, QStringLiteral("1_1"), QStringLiteral("L1"));
        QDialog *d2 = w2.faultSummary();
        d2->show();
        m2.appendEntries({ fl.at(0) });
        QApplication::processEvents();
        auto *tw2 = d2->findChild<QTableWidget *>(QStringLiteral("nmsFaultSpans"));
        CHECK(before > 0 && tw2 && tw2->rowCount() == 1, "the open dialog follows new packets");
    }
    // Not the fault log: no button.
    {
        LogModel m(nullptr, 10);
        NmsLogWindow w(NmsLog::Kind::Health, &m, QStringLiteral("1_1"), QStringLiteral("L1"));
        CHECK(w.findChild<QPushButton *>(QStringLiteral("nmsFaultSummary")) == nullptr, "the health log has no Fault summary");
    }
    Q_UNUSED(totalSpans);
}
