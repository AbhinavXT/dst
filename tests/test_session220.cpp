#include "testutil.h"

#include "settings.h"
#include "stationlayoutwindow.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

// =============================================================================
//  Session 220 — Station Layout: zoom that follows the window, the wheel,
//  and grabbing the sheet.
//  Before: zoom 1 was a 600 px minimum inside a scroll area that stretches
//  the sheet to the window, so on a wide window the first few "+" changed
//  nothing seen, and "−" then had to undo them one by one. Now 1 = fit, and
//  every step changes the width seen. The built-in station.xlsx is drawn.
// =============================================================================

namespace {
QPushButton *button(QWidget &w, const QString &text)
{
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == text) return b;
    return nullptr;
}
}  // namespace

TEST_SUITE(session220)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the built-in layout opens");
    w.resize(1600, 800);
    w.setStepsShown(false);   // session 459: the drawing at the width these numbers were set for
    w.show();
    QApplication::processEvents();
    StationLayoutCanvas *c = w.canvas();
    QScrollArea *sa = w.findChild<QScrollArea *>();
    QPushButton *in = button(w, QStringLiteral("+")), *out = button(w, QStringLiteral("−"));
    CHECK(c && sa && in && out, "canvas, scroll area, + and −");
    if (!c || !sa || !in || !out) return;
    const int view = sa->viewport()->width();
    // Over 900 px (1.5 × the old 600 px minimum) so the old code fails the + below; Windows CI gives 994.
    CHECK(view > 900 && c->width() == view, QByteArray("at 1 the sheet fits a wide window (") + QByteArray::number(view) + ")");

    // The reported case: − at the start, then + zooms at once.
    for (int i = 0; i < 6; ++i) out->click();
    QApplication::processEvents();
    CHECK(c->zoom() == 1.0 && c->width() == view, "− at fit stays at fit");
    in->click();
    QApplication::processEvents();
    CHECK(c->width() >= int(view * 1.5) - 1, QByteArray("one + widens the sheet seen, 1.5× (") + QByteArray::number(c->width()) + ")");
    out->click();
    QApplication::processEvents();
    CHECK(c->zoom() == 1.0 && c->width() == view, "one − brings it back to fit");

    // The wheel zooms about the pointer: the place under it stays under it.
    in->click();
    in->click();
    QApplication::processEvents();
    QScrollBar *h = sa->horizontalScrollBar();
    const double at = h->value() + 300.0;              // canvas x, 300 px into the view
    const double f = at / c->width();
    QWheelEvent wheel(QPointF(at, 50), QPointF(c->mapToGlobal(QPoint(int(at), 50))), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(c, &wheel);
    QApplication::processEvents();
    CHECK(c->zoom() > 2.25 && std::abs(f * c->width() - h->value() - 300.0) < 2.0,
          QByteArray("wheel up zooms in, the pointer's place kept (") + QByteArray::number(f * c->width() - h->value()) + ")");

    // Grabbing the sheet off a tag moves it.
    QPointF grab(h->value() + 400.0, 4);
    bool onTag = false;
    for (const StationLayoutCanvas::Hit &hit : c->hits()) onTag = onTag || (!hit.tag.isEmpty() && hit.rect.contains(grab));
    CHECK(!onTag, "the grab point is off every tag");
    const int before = h->value();
    const QPoint g = c->mapToGlobal(grab.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &press);
    const QPointF to = c->mapFromGlobal(g - QPoint(150, 0));
    QMouseEvent move(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(c, &release);
    CHECK(h->value() == before + 150, QByteArray("dragging 150 px left moves the sheet 150 px (")
                                          + QByteArray::number(h->value() - before) + ")");
}
