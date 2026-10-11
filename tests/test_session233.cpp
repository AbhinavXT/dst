#include "testutil.h"

#include "layoutaudit.h"
#include "settings.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

// =============================================================================
//  Session 233 — Station Layout:
//    - a line a point joins but the lines sheet leaves out gets its lane, so
//      the point is drawn (library ajay/22.20.xlsx: lines lists DM only, its
//      18 points join DM to DL1, DL2, DL3);
//    - a signal or point clicked on the drawing selects its row, and a row
//      selected marks it on the drawing, as tags always did;
//    - Measure: a start and an end, the distance along the track.
//  The built-in station.xlsx: 211 tags, 36 signals, 18 points; tag 910 at
//  164610 m, 981 at 163960 m (sessions 210, 226).
// =============================================================================

namespace {

int countHits(StationLayoutCanvas *c, bool points)
{
    int n = 0;
    for (const StationLayoutCanvas::Hit &h : c->hits()) n += (points ? h.point : h.signal) >= 0 ? 1 : 0;
    return n;
}

QPoint centreOf(StationLayoutCanvas *c, int signal, int point)
{
    for (const StationLayoutCanvas::Hit &h : c->hits())
        if ((signal >= 0 && h.signal == signal) || (point >= 0 && h.point == point)) return h.rect.center().toPoint();
    return QPoint(-1, -1);
}

// A point whose middle no tag covers (a tag on a point's diagonal takes the click).
int clearPoint(StationLayoutCanvas *c)
{
    for (const StationLayoutCanvas::Hit &h : c->hits()) {
        if (h.point < 0) continue;
        bool covered = false;
        for (const StationLayoutCanvas::Hit &o : c->hits()) covered = covered || (!o.tag.isEmpty() && o.rect.contains(h.rect.center()));
        if (!covered) return h.point;
    }
    return -1;
}

} // namespace

TEST_SUITE(session233)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- points on lines the lines sheet leaves out ---------------------------------------------
    {
        StationLayoutWindow w;
        CHECK(w.openFile(QStringLiteral(":/station_layouts/ajay/22.20.xlsx")), "22.20.xlsx opens");
        const StationLayout::Layout &l = w.station();
        CHECK(l.lines.size() == 1 && l.points.size() == 18, "its lines sheet has DM only; 18 points");
        w.resize(1200, 700);
        w.show();
        StationLayoutCanvas *c = w.canvas();
        c->grab();
        int drawable = 0;
        for (const StationLayout::Point &p : l.points) drawable += !p.line1.isEmpty() && !p.line2.isEmpty() ? 1 : 0;
        CHECK(countHits(c, true) == drawable && drawable > 0,
              QByteArray("every point with two lines is drawn (") + QByteArray::number(countHits(c, true)) + " of "
                  + QByteArray::number(drawable) + "; before: 0)");
        CHECK(c->laneNames().contains(QStringLiteral("DL1")) && c->laneNames().contains(QStringLiteral("DL2")),
              "DL1 and DL2 get lanes");
        CHECK(StationLayout::checks(l).contains(QStringLiteral("point P30: line DL1 is not in the lines sheet")),
              "Checks says the lines sheet leaves DL1 out");
        CHECK(!w.isModified() && w.station().lines.size() == 1, "drawing them changes nothing in the file");
    }

    // ---- selecting signals and points --------------------------------------------------------
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast() && w.station().signalList.size() == 36 && w.station().points.size() == 18, "the built-in station");
    w.resize(1200, 760);
    w.show();
    StationLayoutCanvas *c = w.canvas();
    c->grab();
    CHECK(countHits(c, false) == 36 && countHits(c, true) == 18, "36 signals and 18 points to click on");

    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, centreOf(c, 5, -1));
    CHECK(w.tabs()->currentIndex() == 1 && w.table(1)->currentRow() == 5 && c->selectedSignal() == 5,
          "a signal clicked: the Signals tab, its row selected, and it is marked");
    const int pt = clearPoint(c);
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, centreOf(c, -1, pt));
    CHECK(pt >= 0 && w.tabs()->currentIndex() == 2 && w.table(2)->currentRow() == pt && c->selectedPoint() == pt,
          "a point clicked: the Points tab, its row selected, and it is marked");
    w.selectSheetRow(2, 7);
    CHECK(c->selectedPoint() == 7, "a point's row selected: it is marked on the drawing");
    w.selectSheetRow(1, 12);
    CHECK(c->selectedSignal() == 12, "a signal's row selected: it is marked");
    CHECK(w.table(1)->item(12, 1)->text() == w.station().signalList.at(12).name, "the row is that signal's");

    // ---- Measure -----------------------------------------------------------------------------
    QPushButton *measure = w.measureButton();
    CHECK(measure && measure->isCheckable(), "a Measure button");
    measure->setChecked(true);
    CHECK(c->isMeasuring(), "Measure on");
    const int tabBefore = w.tabs()->currentIndex();
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, c->tagRect(QStringLiteral("910")).center().toPoint());
    CHECK(c->measureStart().set && c->measureStart().loc == 164610 && c->measureStart().what.startsWith(QStringLiteral("tag 910"))
              && w.tabs()->currentIndex() == tabBefore,
          "the start: tag 910 at 164610 m (and the click selects nothing)");
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, c->tagRect(QStringLiteral("981")).center().toPoint());
    CHECK(c->measureEnd().set && c->measureEnd().loc == 163960 && w.status()->text().endsWith(QStringLiteral(": 650 m")),
          QByteArray("the end: tag 981; 650 m between them (") + w.status()->text().toUtf8() + ")");
    const QPoint sig = centreOf(c, 0, -1);
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, sig);
    CHECK(c->measureStart().what.startsWith(QStringLiteral("signal ")) && !c->measureEnd().set, "a third click starts again, from a signal");
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, QPoint(c->width() / 2, 4));
    CHECK(c->measureEnd().set && c->measureEnd().what.endsWith(QStringLiteral(" km")), "an empty spot: the location under it");
    c->grab();      // the dimension line paints
    QTest::keyClick(c, Qt::Key_Escape);
    CHECK(!measure->isChecked() && !c->isMeasuring() && !c->measureStart().set, "Esc: Measure off, the measurement gone");
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, centreOf(c, 2, -1));
    CHECK(w.table(1)->currentRow() == 2, "clicks select again once Measure is off");

    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no widget outside a layout");
    CHECK(w.minimumSizeHint().width() <= 1000, QByteArray("the bar still leaves room for wider fonts (")
                                                   + QByteArray::number(w.minimumSizeHint().width()) + ")");
}
