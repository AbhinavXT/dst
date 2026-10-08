#include "testutil.h"

#include "capturedecoder.h"
#include "lococonsolewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QTableWidget>

// =============================================================================
//  Session 170 — a received ARP carrying the loco's own ID is rejected: not
//  shown as another loco on the Live Loco Console, counted by the run report.
//  Real frames: replay/loco_2_1_08102026_162008.cap, the first two minutes of
//  Abhinav's 81_2 capture of 2026-10-08. Loco 2 hears its own ARP (44 times;
//  the first, line 13, before its own first ARP on line 17) and loco 1's
//  (19 times, from 16:21:19).
// =============================================================================

namespace {

QStringList lines170()
{
    QStringList out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_08102026_162008.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (l.startsWith(QLatin1Char('@'))) out << l;
    }
    return out;
}

qint64 sourceOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw.value(QStringLiteral("SOURCE_LOCO_ID"), -1);
}

QString valueOf(QTableWidget *t, const QString &field)
{
    for (int r = 0; t && r < t->rowCount(); ++r)
        if (t->item(r, 0) && t->item(r, 0)->text().trimmed() == field) return t->item(r, 1) ? t->item(r, 1)->text() : QString();
    return QString();
}

}  // namespace

TEST_SUITE(session170)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QStringList lines = lines170();
    CHECK(lines.size() > 2000, "fixture: the excerpt");

    // Brute force, from the decoder: received ARPs from loco 2 / loco 1, and
    // which of loco 2's come after its own ID is known (its first ARP/LSRP).
    int ownRecv = 0, ownAfterKnown = 0, otherRecv = 0, firstOther = -1;
    bool known = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (l.startsWith(QLatin1String("@arp_")) || l.startsWith(QLatin1String("@lsrp_"))) {
            known |= sourceOf(l) == 2;
        } else if (l.startsWith(QLatin1String("@arprecv_"))) {
            const qint64 src = sourceOf(l);
            if (src == 2) { ++ownRecv; if (known) ++ownAfterKnown; }
            else if (src == 1) { ++otherRecv; if (firstOther < 0) firstOther = i; }
        }
    }
    CHECK(ownRecv == 44 && ownAfterKnown == 43 && otherRecv == 19,
          QByteArray("decoded with the 8-byte received header: 44 from loco 2 itself (43 once it is known), 19 from loco 1 (")
              + QByteArray::number(ownRecv) + "/" + QByteArray::number(ownAfterKnown) + "/" + QByteArray::number(otherRecv) + ")");

    // ---- run report ---------------------------------------------------------------------
    {
        MessageDispatcher disp;
        for (const QString &l : lines) {
            const QStringList tok = l.split(QLatin1Char(' '));
            disp.ingestLocal(81, 2, l.toLatin1(), QDateTime::fromString(tok.at(1), Qt::ISODate).toMSecsSinceEpoch(), QString());
        }
        disp.drainNow();
        LogModel *model = disp.modelForKey(QStringLiteral("81_2"));
        CHECK(model && model->count() == lines.size(), "fixture: the tab's rows");
        if (model) {
            const RunReport::Summary s = RunReport::summarise(model, QStringLiteral("81_2"), QString());
            CHECK(s.arpRecvFrames == 63, QByteArray("63 received ARPs judged (") + QByteArray::number(s.arpRecvFrames) + ")");
            CHECK(s.arpRecvRejects.value(QStringLiteral("DLConsole  SOURCE_LOCO_ID")) == ownAfterKnown,
                  QByteArray("each own-ID one after the loco is identified is a reject match (")
                      + QByteArray::number(s.arpRecvRejects.value(QStringLiteral("DLConsole  SOURCE_LOCO_ID"))) + ")");
            CHECK(s.arpRecvRejects.size() == 1, "and nothing else: loco 1's ARPs are processed");
            const QString html = RunReport::toHtml(s);
            CHECK(html.contains(QStringLiteral("Received ARP frames matching a reject condition: 43 matches over 63 frames"))
                      && html.contains(QStringLiteral("own ID")) && html.contains(QStringLiteral("would not process")),
                  "the report says so, in the receiver's terms");
        }
    }

    // ---- Live Loco Console -------------------------------------------------------------------
    {
        LocoConsoleWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        auto feed = [&w](const QString &line) {
            LogEntryPtr e(new LogEntry);
            e->text = line;
            e->epochMs = QDateTime::currentMSecsSinceEpoch();
            QMetaObject::invokeMethod(&w, "onEntryAppended", Q_ARG(QString, QStringLiteral("81_2")), Q_ARG(LogEntryPtr, e));
        };
        for (int i = 0; i < firstOther; ++i) feed(lines.at(i));
        QMetaObject::invokeMethod(&w, "onRefreshTick");
        QTableWidget *recv = w.tableForLabel(QString::fromLatin1(CaptureDecoder::typeLabel(CapType::ArpRecv)));
        CHECK(recv != nullptr, "the received-ARP tab");
        int ownBefore = 0;
        for (int i = 0; i < firstOther; ++i) ownBefore += lines.at(i).startsWith(QLatin1String("@arprecv_")) ? 1 : 0;
        const QString before = valueOf(recv, QStringLiteral("rejected"));
        CHECK(before.startsWith(QStringLiteral("%1 received ARP(s) carrying this loco's own ID 2").arg(ownBefore))
                  && before.contains(QStringLiteral("none from another loco yet"))
                  && valueOf(recv, QStringLiteral("SOURCE_LOCO_ID")).isEmpty(),
              QByteArray("before loco 1 is heard: its own ARPs, all of them (the first too, withdrawn once the ID was "
                         "learned), are not shown as another loco: ") + before.toUtf8());

        for (int i = firstOther; i < lines.size(); ++i) feed(lines.at(i));
        QMetaObject::invokeMethod(&w, "onRefreshTick");
        CHECK(valueOf(recv, QStringLiteral("SOURCE_LOCO_ID")).startsWith(QLatin1Char('1')),
              QByteArray("after: the tab is loco 1's ARP (SOURCE_LOCO_ID ") + valueOf(recv, QStringLiteral("SOURCE_LOCO_ID")).toUtf8() + ")");
        CHECK(valueOf(recv, QStringLiteral("rejected")).startsWith(QStringLiteral("44 received ARP(s)")),
              QByteArray("with the own-ID count above it: ") + valueOf(recv, QStringLiteral("rejected")).toUtf8());
    }
}
