#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "tinlayout.h"
#include "uicolors.h"

#include <QImage>
#include <QPainter>

// =============================================================================
//  Session 432 — signals drawn as the SIP and the tag-TIN layouts draw them
//  (RDSO Annexure-H p.9, LPI): a mast from the foot tag with an arrow at the
//  track, the heads outwards the way the signal faces, lamps touching: Red
//  (three bars), Yellow (a slash), Green (a bar), a second Yellow; a shunt
//  disc nearest the mast, a small calling-on lamp behind it. The way it faces:
//  the foot tag's placement (1 nominal, 2 reverse). The layout says how many
//  lamps (aspects, calling_on, shunt), else the name: "SH…" a shunt signal.
// =============================================================================

namespace {

QColor pixel(const QImage &im, double x, double y) { return im.pixelColor(int(x), int(y)); }

bool near(const QColor &a, const QColor &b) { return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) < 60; }

} // namespace

TEST_SUITE(session432)
{
    // ---- the look ----
    StationLayout::Signal s;
    s.name = QStringLiteral("S48");
    StationLayout::SignalLook k = StationLayout::lookOf(s);
    CHECK(k.aspects == 3 && !k.shunt && !k.callingOn, "not said: a 3-aspect main signal");
    s.name = QStringLiteral("SH40");
    k = StationLayout::lookOf(s);
    CHECK(k.aspects == 0 && k.shunt, "not said, named SH…: a shunt signal, a disc only");
    s.name = QStringLiteral("S50");
    s.aspects = 4;
    s.callingOn = true;
    k = StationLayout::lookOf(s);
    CHECK(k.aspects == 4 && k.callingOn && !k.shunt, "said: 4 aspects and calling-on (S50 on the LPI sheet)");

    // ---- through every file kind ----
    StationLayout::Layout l;
    QString err;
    QStringList notes;
    CHECK(StationLayout::fromXml(QByteArray(
              "<kavsite format=\"kavsite-xml 1\" name=\"x\"><signals>"
              "<signal sig_foot_tag=\"1\" signal=\"S50\" sig_id=\"3\" StationId=\"527\" aspects=\"4\" calling_on=\"1\"/>"
              "<signal sig_foot_tag=\"3\" signal=\"S5\" sig_id=\"5\" StationId=\"527\" aspects=\"2\" shunt=\"1\"/>"
              "<signal sig_foot_tag=\"4\" signal=\"S7\" sig_id=\"7\" StationId=\"527\" aspects=\"5\"/>"
              "<signal sig_foot_tag=\"5\" signal=\"S9\" sig_id=\"9\" StationId=\"527\"/>"
              "</signals></kavsite>"), &l, &err, &notes),
          "signals with their heads in the XML");
    CHECK(l.signalList.size() == 4 && l.signalList.at(0).aspects == 4 && l.signalList.at(0).callingOn
              && l.signalList.at(1).aspects == 2 && l.signalList.at(1).shunt && l.signalList.at(3).aspects == -1,
          "aspects, calling_on, shunt read; none: not said");
    CHECK(l.signalList.at(2).aspects == -1 && notes == QStringList{ QStringLiteral("signal S7: aspects \"5\" is not 0, 2, 3 or 4: drawn as not said") },
          "aspects 5: named, not guessed");
    const QByteArray xml = StationLayout::toXml(l, QStringLiteral("x"));
    CHECK(xml.contains("signal=\"S50\" sig_id=\"3\" StationId=\"527\" aspects=\"4\" calling_on=\"1\"/>")
              && xml.contains("signal=\"S9\" sig_id=\"9\" StationId=\"527\"/>"),
          "written back only where said (station_527.xml, which says none, stays as it was: session 431)");
    StationLayout::Layout j, x2;
    CHECK(StationLayout::fromJson(StationLayout::toJson(l), &j) && j.signalList.at(0).aspects == 4 && j.signalList.at(0).callingOn
              && j.signalList.at(1).shunt && j.signalList.at(3).aspects == -1,
          "the .json keeps them");
    CHECK(StationLayout::fromSheets(StationLayout::toSheets(l), &x2) && x2.signalList.at(1).aspects == 2 && x2.signalList.at(1).shunt
              && x2.signalList.at(3).aspects == -1,
          "and the sheets (columns aspects, calling_on, shunt)");

    // ---- the symbol, pixel by pixel ----
    const QColor ink(0, 0, 0);
    {
        QImage im(120, 40, QImage::Format_ARGB32);
        im.fill(Qt::white);
        QPainter p(&im);
        TinLayout::SignalHead h;
        h.aspects = 3;
        const QRectF box = TinLayout::drawSignal(p, QPointF(20, 5), h, 1, TinLayout::LampGreen, ink);
        p.end();
        // Lamps r 4.5 from x + 5: Red at 29.5, Yellow 38.5, Green 47.5; centre line y 18.
        CHECK(near(pixel(im, 47.5, 15.5), UiColor::signalLamp(QStringLiteral("green"))), "the lit lamp, third from the mast: green");
        CHECK(near(pixel(im, 38.5 + 2, 18 + 1.5), Qt::white), "Yellow not lit: as on the sheet (white inside)");
        CHECK(box.left() < 20 && box.right() > 51 && box.right() < 54 && box.top() == 5, "nominal: the heads to the right of the mast");
        CHECK(near(pixel(im, 20, 7), ink), "the arrow at the foot, under the track");
    }
    {
        QImage im(120, 40, QImage::Format_ARGB32);
        im.fill(Qt::white);
        QPainter p(&im);
        TinLayout::SignalHead h;
        h.aspects = 2;
        const QRectF box = TinLayout::drawSignal(p, QPointF(100, 5), h, -1, TinLayout::LampRed, ink);
        p.end();
        CHECK(box.right() <= 104.5 && box.left() > 75 && box.left() < 82, "reverse: the heads to the left");
        CHECK(near(pixel(im, 90.5 + 3.3, 18), UiColor::signalLamp(QStringLiteral("red"))), "Red nearest the mast, lit");
    }

    // ---- on the drawing ----
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml");
    StationLayoutCanvas *c = w.canvas();
    w.resize(1200, 800);
    w.show();
    c->grab();
    int drawn = 0;
    QRectF s48, s1640;
    for (const StationLayoutCanvas::Hit &h : c->hits()) {
        if (h.signal < 0) continue;
        ++drawn;
        const QString n = w.station().signalList.at(h.signal).name;
        if (n == QStringLiteral("S48")) s48 = h.symbol;
        if (n == QStringLiteral("S1640")) s1640 = h.symbol;
    }
    CHECK(drawn == 35, "35 signals drawn: the station's; a route's at a foot tag one stands at is that one (14)");
    const QRectF t12 = c->tagRect(QStringLiteral("12")), t981 = c->tagRect(QStringLiteral("981"));
    CHECK(!s48.isNull() && !t12.isNull() && s48.center().x() > t12.center().x() + 8,
          "S48 at tag 12 (placement 1, nominal): its heads to the right, as on the LPI sheet");
    CHECK(!s1640.isNull() && !t981.isNull() && s1640.center().x() < t981.center().x() - 8,
          "S1640 at tag 981 (placement 2, reverse): to the left");
}
