#include "testutil.h"

#include "pressuregauge.h"
#include "simwidgets.h"
#include "speedgauge.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include <cmath>

// =============================================================================
//  Session 396 — the dials' scale numbers were cut off on a large window:
//  each was drawn in a fixed 32 x 16 box (24 x 16 on the pressure dial) while
//  the font grows with the dial. Now the box is the text's own.
// =============================================================================

TEST_SUITE(session396)
{
    // A dial of 1000 px: the speed dial's label font is radius x 0.085 pt.
    const double radius = 500;
    QFont font;
    font.setPointSizeF(radius * 0.085);
    const QFontMetricsF metrics(font);
    const QString widest = QStringLiteral("120");
    const QRectF box = SimUi::labelBox(font, QPointF(300, 200), widest);
    CHECK(box.width() >= metrics.horizontalAdvance(widest) && box.height() >= metrics.height(),
          QByteArray("the box holds the text: ") + QByteArray::number(box.width()) + " x " + QByteArray::number(box.height())
              + " for " + QByteArray::number(metrics.horizontalAdvance(widest)) + " x " + QByteArray::number(metrics.height()));
    CHECK(box.width() > 32 && box.height() > 16, "bigger than the old fixed 32 x 16 at this size");
    CHECK(qAbs(box.center().x() - 300) < 1e-9 && qAbs(box.center().y() - 200) < 1e-9, "centred on the point");

    // The labels sit at 0.57 of the radius; the ticks start at 0.70. The
    // corners of the widest label, anywhere round the dial, stay inside, in
    // the font the dial fits to them (session 400: Linux's fonts ran over).
    font = SimUi::fitLabelFont(font, radius, 0.57, 0.70, widest);
    CHECK(font.pointSizeF() > radius * 0.085 * 0.8, "and the font is not shrunk far to do it");
    double farthest = 0;
    for (int deg = 0; deg < 360; deg += 5) {
        const double a = deg * 3.14159265358979323846 / 180;
        const QRectF b = SimUi::labelBox(font, QPointF(std::cos(a) * radius * 0.57, -std::sin(a) * radius * 0.57), widest);
        for (const QPointF &corner : { b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight() })
            farthest = qMax(farthest, std::hypot(corner.x(), corner.y()) / radius);
    }
    CHECK(farthest < 0.70, QByteArray("the widest number stays inside the tick ring: ") + QByteArray::number(farthest, 'f', 3)
                               + " of the radius");

    // Both dials paint at a large size.
    SpeedGauge speed(QStringLiteral("Forward"), true);
    speed.setScaleMaximum(140);
    speed.resize(1000, 1000);
    QImage image(1000, 1000, QImage::Format_ARGB32);
    image.fill(Qt::white);
    speed.render(&image);
    CHECK(image.pixelColor(500, 500) != QColor(Qt::white), "the speed dial paints at 1000 px");
    PressureGauge pressure(QStringLiteral("BP"));
    pressure.resize(1000, 1000);
    image.fill(Qt::white);
    pressure.render(&image);
    CHECK(image.pixelColor(500, 500) != QColor(Qt::white), "and the pressure dial");
}
