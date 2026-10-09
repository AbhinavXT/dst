#include "testutil.h"
#include "sosfixture.h"
#include "layoutaudit.h"

#include "soslog.h"
#include "sosrelay.h"
#include "soswindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 187 — SoS, phase D: two logs, what one loco sent and what the other
//  did with it. SYNTHETIC frames (tests/sosgen): loco 2's pilot holds SoS from
//  10:00:09 to 10:01:52 (its own log); loco 1 hears its ARPs every 2 s.
// =============================================================================

using namespace SosFixture;

TEST_SUITE(session187)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m1 = load(d, 1);
    LogModel *m2 = load(d, 2);
    CHECK(m1 && m2, "fixtures: both locos' synthetic SoS logs");
    if (!m1 || !m2) return;
    const SosLog::Timeline t1 = SosLog::extract(m1), t2 = SosLog::extract(m2);

    // ---- loco 2 sent, loco 1 heard and acted ------------------------------------------
    const QVector<SosLog::Relay> r = SosLog::relay(t2, t1, 2);
    CHECK(r.size() == 2, "loco 2 changed its broadcast status twice");
    if (r.size() == 2) {
        CHECK(r[0].sentMs == atSos("10:00:09") && r[0].fromStatus == 0 && r[0].toStatus == 2,
              "10:00:09: 0 -> 2 (SoS), in loco 2's log");
        CHECK(r[0].tracked && r[0].heardMs == atSos("10:00:10"), "heard by loco 1 at 10:00:10, its next ARP");
        CHECK(r[0].actedMs == atSos("10:00:10") && r[0].acted == QLatin1String("Manual SoS from loco 2 started, 810 m"),
              "and acted on at once: the manual SoS started, 810 m");
        CHECK(r[1].sentMs == atSos("10:01:52") && r[1].toStatus == 0 && r[1].heardMs == atSos("10:01:52"),
              "10:01:52: back to 0, heard at once");
        CHECK(r[1].actedMs < 0, "no decision about it: loco 1 had already ended that threat (passed it) at 10:01:46");
    }

    // ---- the other way: loco 1's status changes never reached loco 2's table ------------
    const QVector<SosLog::Relay> back = SosLog::relay(t1, t2, 1);
    CHECK(back.size() == 2 && back[0].toStatus == 4 && back[1].toStatus == 5,
          "loco 1 broadcast 4 (head-on) then 5 (rear-end)");
    if (back.size() == 2)
        CHECK(!back[0].tracked && back[0].heardMs < 0 && !back[1].tracked,
              "loco 2's log had ended by then: not in its table, not heard");

    // ---- the clocks: a receiver 3 s behind hears it "before" it was sent ------------------
    {
        SosLog::Timeline skewed = t1;
        for (SosLog::Snapshot &s : skewed.snaps) s.epochMs -= 3000;
        for (SosLog::Event &e : skewed.events) e.epochMs -= 3000;
        const QVector<SosLog::Relay> rs = SosLog::relay(t2, skewed, 2);
        CHECK(!rs.isEmpty() && rs.first().heardMs >= 0 && rs.first().heardMs < rs.first().sentMs,
              "with loco 1's clock 3 s behind, heard before sent: reported as such, not hidden");
    }

    // ---- the window's Two logs tab ----------------------------------------------------------
    SosWindow w(&d);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 680);
    w.setSource(QStringLiteral("1_1"));
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    SosRelayPanel *rp = w.relay();
    CHECK(rp && w.lowerTabs() && w.lowerTabs()->count() == 2 && w.lowerTabs()->tabText(1) == QLatin1String("Two logs"),
          "Decisions | Two logs");
    if (!rp) return;
    w.lowerTabs()->setCurrentIndex(1);
    CHECK(rp->picker()->count() == 1 && rp->senderKey() == QLatin1String("2_1"),
          "the other log with @sos is offered (this one is not)");
    QTableWidget *t = rp->table();
    CHECK(t->rowCount() == 2, "one row per status change");
    if (t->rowCount() == 2) {
        CHECK(t->item(0, 0)->text() == QLatin1String("10:00:09")
                  && t->item(0, 1)->text() == QStringLiteral("0 (No Emergency) → 2 (SoS)")
                  && t->item(0, 2)->text() == QLatin1String("10:00:10") && t->item(0, 3)->text() == QLatin1String("+1.0 s")
                  && t->item(0, 6)->text() == QLatin1String("Manual SoS from loco 2 started, 810 m"),
              "row 1: sent 10:00:09, heard 10:00:10 (+1.0 s), the decision");
        CHECK(t->item(1, 4)->text() == QLatin1String("no decision about it"), "row 2: no decision, said so");
    }
    CHECK(rp->summary()->text().startsWith(QStringLiteral("Loco 2: 2 status changes · 2 heard here · 1 acted on.")),
          QByteArray("the summary: ") + rp->summary()->text().toUtf8());

    QSignalSpy jumps(&w, &SosWindow::jumpRequested);
    emit t->cellDoubleClicked(0, 0);
    CHECK(jumps.size() == 1 && jumps.first().at(0).toString() == QLatin1String("2_1")
              && jumps.first().at(1).toLongLong() == atSos("10:00:09"),
          "double-click the sent time: that moment in loco 2's log");
    emit t->cellDoubleClicked(0, 2);
    CHECK(jumps.size() == 2 && jumps.last().at(0).toString() == QLatin1String("1_1")
              && w.timeline().snaps.at(w.currentSnapshot()).epochMs == atSos("10:00:10"),
          "double-click the heard time: this log, and the window moves there");

    // Reading loco 2's log, loco 1 is the other one.
    w.setSource(QStringLiteral("2_1"));
    CHECK(rp->senderKey() == QLatin1String("1_1") && rp->table()->rowCount() == 2
              && rp->table()->item(0, 2)->text() == QLatin1String("loco 1 not in this loco's SoS table"),
          "from loco 2's side: loco 1's changes, not in its table then");

    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
