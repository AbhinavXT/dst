#include "testutil.h"

#include "fieldplot.h"
#include "messagedispatcher.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QtTest/QTest>

// =============================================================================
//  Session 157 — Plot field over time: choosing a field from the menu.
//
//  Reported: choosing any field in the window's field menu crashed the
//  console. The suites before this one opened the window on a field by
//  name (plotField / addField) and never went through the menu.
//  Real run: replay/loco_1_1_26062026_162418.cap.
// =============================================================================

namespace {

// The submenu for each packet type, freshly built.
QVector<QMenu *> packetMenus(QMenu *top)
{
    QVector<QMenu *> out;
    for (QAction *a : top->actions())
        if (QMenu *sub = a->menu()) out << sub;
    return out;
}

// What the operator does: the menu pops up, the packet's submenu opens, and
// the field is clicked. The click is delivered by the submenu itself, which
// is what a trigger() from outside never exercised.
void clickField(QMenu *top, int packet, int field)
{
    top->popup(QPoint(10, 10));
    QMenu *sub = packetMenus(top).value(packet);
    if (!sub) { top->hide(); return; }
    sub->popup(QPoint(40, 40));
    QCoreApplication::processEvents();
    const QList<QAction *> acts = sub->actions();
    if (field >= acts.size()) { sub->hide(); top->hide(); return; }
    const QPoint at = sub->actionGeometry(acts.at(field)).center();
    QTest::mouseMove(sub, at);
    QTest::mouseClick(sub, Qt::LeftButton, Qt::NoModifier, at);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

}  // namespace

TEST_SUITE(session157)
{
    MessageDispatcher disp;
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
    CHECK(run.open(QIODevice::ReadOnly), "fixture: the run");
    while (!run.atEnd()) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();

    FieldPlotWindow w(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    const int packets = packetMenus(w.fieldMenu()).size();
    CHECK(packets > 3, QByteArray("the field menu has a submenu per packet (") + QByteArray::number(packets) + ")");

    // The first two fields of every packet, each clicked in the menu.
    int clicked = 0, plotted = 0;
    for (int p = 0; p < packets; ++p) {
        for (int f = 0; f < 2; ++f) {
            QMenu *sub = packetMenus(w.fieldMenu()).value(p);
            if (!sub || f >= sub->actions().size()) continue;
            const QString field = sub->actions().at(f)->text();
            ++clicked;
            clickField(w.fieldMenu(), p, f);
            if (w.currentField() == field) ++plotted;
        }
    }
    CHECK(clicked > 0 && plotted == clicked,
          QByteArray("clicking a field in the menu plots it, without a crash (") + QByteArray::number(plotted)
              + " of " + QByteArray::number(clicked) + ")");

    // And the "Add" menu, which rebuilds itself the same way.
    {
        QMenu *sub = packetMenus(w.addMenu()).value(0);
        const int before = w.seriesCount();
        int f = 0;
        while (sub && f < sub->actions().size() && !sub->actions().at(f)->isEnabled()) ++f;
        clickField(w.addMenu(), 0, f);
        CHECK(w.seriesCount() == before + 1, "clicking a field in Add adds it, without a crash");
    }
}
