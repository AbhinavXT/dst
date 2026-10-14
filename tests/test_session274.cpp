#include "testutil.h"

#include "capturedecoder.h"
#include "filterbar.h"
#include "logmodel.h"
#include "theme.h"
#include "uistyle.h"

#include <QFile>
#include <QLabel>
#include <QSortFilterProxyModel>

// =============================================================================
//  Session 274 — the main tables hide a received ARP carrying the loco's own
//  ID (its own ARP heard back), on by default, switchable.
//  Real frames: replay/2026-10-08/loco_2_1_08102026_162008.cap (session 170):
//  loco 2 hears its own ARP 44 times, the first before its own first ARP, and
//  loco 1's 19 times. The ambiguity check adds a real ARP from loco 1's own
//  log, replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {

QStringList capLines(const char *path)
{
    QStringList out;
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (l.startsWith(QLatin1Char('@'))) out << l;
    }
    return out;
}

// SOURCE_LOCO_ID from the decoder; -1 when absent or the CRC fails.
qint64 sourceOf(const QString &line)
{
    const CaptureLine c = CaptureDecoder::parseLine(line);
    if (!c.valid || (c.crcChecked && !c.crcOk)) return -1;
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(c, nullptr, 0, &raw);
    return raw.value(QStringLiteral("SOURCE_LOCO_ID"), -1);
}

LogEntryPtr entryOf(const QString &line, qint64 ms)
{
    LogEntryPtr e(new LogEntry);
    e->text = line;
    e->epochMs = ms;
    return e;
}

// Received ARPs the proxy shows, by SOURCE_LOCO_ID.
QHash<qint64, int> shownRecv(const QSortFilterProxyModel *p, const LogModel &m)
{
    QHash<qint64, int> out;
    for (int r = 0; r < p->rowCount(); ++r) {
        const LogEntryPtr e = m.entryAt(p->mapToSource(p->index(r, 0)).row());
        if (e && e->text.startsWith(QLatin1String("@arprecv_"))) ++out[sourceOf(e->text)];
    }
    return out;
}

}  // namespace

TEST_SUITE(session274)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QStringList lines = capLines(DL_SRC_DIR "/replay/2026-10-08/loco_2_1_08102026_162008.cap");
    CHECK(lines.size() > 2000, "fixture: the 81_2 excerpt");

    int own = 0, other = 0;
    for (const QString &l : lines) {
        if (!l.startsWith(QLatin1String("@arprecv_"))) continue;
        const qint64 src = sourceOf(l);
        if (src == 2) ++own; else if (src == 1) ++other;
    }
    CHECK(own == 44 && other == 19,
          QByteArray("decoder: 44 received ARPs from loco 2 itself, 19 from loco 1 (")
              + QByteArray::number(own) + "/" + QByteArray::number(other) + ")");

    // ---- row by row, as live traffic arrives ------------------------------------------
    {
        LogModel model;
        FilterBar bar(&model);
        CHECK(!bar.hideOwnArpRecv(), "off unless set: other filter bars are unchanged");
        bar.setHideOwnArpRecv(true);
        qint64 ms = 1000;
        for (const QString &l : lines) model.appendEntry(entryOf(l, ms += 10));
        const QSortFilterProxyModel *p = bar.proxyModel();

        CHECK(bar.ownLocoId() == 2, "the own ID is learned from the tab's ARP / LSRP");
        CHECK(bar.ownArpRecvCount() == 44,
              QByteArray("all 44 counted, the one before the ID was known too (")
                  + QByteArray::number(bar.ownArpRecvCount()) + ")");
        CHECK(p->rowCount() == model.count() - 44, "exactly those rows are hidden");
        const QHash<qint64, int> shown = shownRecv(p, model);
        CHECK(shown.value(2) == 0 && shown.value(1) == 19, "no own one shown; all 19 of loco 1's are");

        auto *count = bar.findChild<QLabel *>();
        bool said = false;
        for (QLabel *l : bar.findChildren<QLabel *>())
            said |= l->text().contains(QStringLiteral("44 own ARP hidden"))
                    && l->toolTip().contains(QStringLiteral("own ID 2"));
        CHECK(count && said, "the count label says how many are hidden, and why");

        bar.setHideOwnArpRecv(false);
        CHECK(p->rowCount() == model.count() && shownRecv(p, model).value(2) == 44,
              "switched off, every received ARP is shown");
        CHECK(bar.ownArpRecvCount() == 44, "and still counted");
        bool quiet = true;
        for (QLabel *l : bar.findChildren<QLabel *>()) quiet &= !l->text().contains(QStringLiteral("own ARP"));
        CHECK(quiet, "the label no longer mentions them");
        bar.setHideOwnArpRecv(true);
        CHECK(p->rowCount() == model.count() - 44, "switched back on, hidden again");

        // A second loco's own ARP on the same tab: which one is "ours" is
        // undecided, so nothing is hidden.
        const QStringList loco1 = capLines(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
        QString arp1;
        for (const QString &l : loco1)
            if (l.startsWith(QLatin1String("@arp_")) && sourceOf(l) == 1) { arp1 = l; break; }
        CHECK(!arp1.isEmpty(), "fixture: a real ARP from loco 1");
        model.appendEntry(entryOf(arp1, ms += 10));
        CHECK(bar.ownLocoId() == -1 && bar.ownArpRecvCount() == 0 && p->rowCount() == model.count(),
              "two locos' ARPs on one tab: nothing hidden");
    }

    // ---- one batch, and trimming --------------------------------------------------------
    {
        LogModel model(nullptr, 1000, 0.10);
        FilterBar bar(&model);
        bar.setHideOwnArpRecv(true);
        QVector<LogEntryPtr> batch;
        qint64 ms = 1000;
        for (const QString &l : lines) batch << entryOf(l, ms += 10);
        model.appendEntries(batch);
        int kept = 0;
        for (int i = 0; i < model.count(); ++i) {
            const LogEntryPtr e = model.entryAt(i);
            if (e->text.startsWith(QLatin1String("@arprecv_")) && sourceOf(e->text) == 2) ++kept;
        }
        CHECK(model.count() <= 1000, "fixture: the tab trimmed to its capacity");
        CHECK(bar.ownArpRecvCount() == kept && bar.proxyModel()->rowCount() == model.count() - kept,
              QByteArray("the count follows the rows the tab still holds (")
                  + QByteArray::number(bar.ownArpRecvCount()) + "/" + QByteArray::number(kept) + ")");
        model.clear();
        CHECK(bar.ownArpRecvCount() == 0 && bar.proxyModel()->rowCount() == 0, "cleared: none counted");
    }
}
