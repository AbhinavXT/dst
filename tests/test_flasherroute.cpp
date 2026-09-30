#include "testutil.h"

#include "flasherqueuepage.h"
#include "flasherwindow.h"

#include <QApplication>
#include <QDir>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>

// =============================================================================
//  Flasher delivery-route diagram (session 80): reported with the VCC tile
//  drawn over the Input/Output/Analog row. Its row gap was (height - three
//  tiles) / 2, so a short window, which squeezed the left column, made it
//  negative. The gap is now fixed, the diagram refuses to be squeezed below
//  what it needs, and the left column scrolls instead.
//
//  Set DL_SHOTS=<dir> to also write screenshots of the page, tall and short.
// =============================================================================

TEST_SUITE(flasherroute)
{
    QTemporaryDir temp;
    FlasherWindow window(nullptr, temp.path());
    FlasherQueuePage *page = window.queuePage();
    auto *route = page->findChild<FlasherRouteDiagram *>();
    CHECK(route != nullptr, "the route diagram is on the queue page");
    if (!route) return;

    CHECK(route->minimumSizeHint().height() == route->sizeHint().height(),
          "the diagram may not be squeezed below the height it needs");
    const int line = route->fontMetrics().height();
    CHECK(route->sizeHint().height() >= (line * 2 + 10) * 3 + 2 * 14,
          "which is three tile rows and two connector gaps");

    auto *scroll = page->findChild<QScrollArea *>(QStringLiteral("flasherLeftScroll"));
    CHECK(scroll != nullptr, "the left column is in a scroll area");

    for (int h : { 900, 560, 420 }) {
        window.resize(1280, h);
        window.show();
        QTest::qWait(30);
        CHECK(route->height() >= route->sizeHint().height(),
              QByteArray("at a window height of ") + QByteArray::number(h)
                  + " the diagram still gets its full height (the column scrolls)");
        const QByteArray dir = qgetenv("DL_SHOTS");
        if (!dir.isEmpty()) {
            window.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(
                QStringLiteral("flasher_%1.png").arg(h)));
        }
    }

    // Larger text: the diagram asks for more height with it.
    const int before = route->sizeHint().height();
    QFont big = route->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    route->setFont(big);
    CHECK(route->sizeHint().height() > before && route->minimumHeight() >= route->sizeHint().height(),
          "a bigger font raises the height it insists on");
}
