#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "stationlayoutwindow.h"
#include "tagbuilderwindow.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>

// =============================================================================
//  Session 221 — Station Layout ↔ RFID Tag Builder.
//  The built-in station.xlsx (session 210: tag 981 at 163960 m). A tag is
//  opened in the Tag Builder, changed there, and its Apply button puts it
//  back: edited in place, added as a new tag, or renamed (the lines and
//  signals naming it follow). The tag bytes are the layout's own.
// =============================================================================

namespace {
int indexOf(const StationLayout::Layout &l, const QString &name)
{
    for (int i = 0; i < l.tags.size(); ++i) if (l.tags.at(i).name == name) return i;
    return -1;
}
}  // namespace

TEST_SUITE(session221)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the built-in layout opens");
    const int n = w.station().tags.size();
    const StationLayout::Tag orig = w.station().tags.at(indexOf(w.station(), QStringLiteral("981")));

    // ---- edit: 981 moved 50 m in the Tag Builder, applied back ----
    TagBuilderWindow *tb = w.openInTagBuilder(QStringLiteral("981"));
    CHECK(tb && tb->pageXEdit()->text().compare(orig.pageX, Qt::CaseInsensitive) == 0
              && tb->pageYEdit()->text().compare(orig.pageY, Qt::CaseInsensitive) == 0,
          "the Tag Builder opens on tag 981's own page_x / page_y");
    if (!tb) return;
    CHECK(tb->applyButton()->isVisibleTo(tb) && tb->applyButton()->text().contains(QStringLiteral("981")),
          "it offers \"Apply to tag 981\"");
    CHECK(tb->setField(QStringLiteral("abs_loc"), 164010), "abs_loc set in the builder");
    tb->applyButton()->click();
    const int i981 = indexOf(w.station(), QStringLiteral("981"));
    const StationLayout::TagInfo in = StationLayout::info(w.station().tags.value(i981));
    CHECK(w.station().tags.size() == n && in.ok && in.absLoc == 164010 && in.crcOk,
          "981 now at 164010 m in the layout, CRC-30 good, nothing added");
    CHECK(StationLayout::tagLocations(w.station()).value(QStringLiteral("981")) == 164010, "the drawing's location follows");
    CHECK(w.undo() && w.station().tags.at(i981).pageX == orig.pageX && w.station().tags.at(i981).pageY == orig.pageY,
          "one Undo puts 981 back");

    // ---- add: a new tag from the selected one ----
    w.table(0)->selectRow(i981);
    tb = w.openInTagBuilder(QString());
    CHECK(tb->applyButton()->text().contains(QStringLiteral("Add")), "a new tag: \"Add this tag\"");
    tb->applyButton()->click();
    CHECK(w.station().tags.size() == n, "adding it unchanged is refused: 981 is already there");
    int unused = 1000;
    while (indexOf(w.station(), QString::number(unused)) >= 0) ++unused;
    unused = qMin(unused, 1023);
    CHECK(tb->setField(QStringLiteral("unique"), unused) && tb->setField(QStringLiteral("abs_loc"), 164100), "a new id and place");
    tb->applyButton()->click();
    const int iNew = indexOf(w.station(), QString::number(unused));
    CHECK(w.station().tags.size() == n + 1 && iNew == i981 + 1, "added after the selected tag, named by its bits");
    CHECK(StationLayout::info(w.station().tags.value(iNew)).absLoc == 164100, "at 164100 m");
    w.undo();
    CHECK(w.station().tags.size() == n, "Undo removes it");

    // ---- rename: a tag on a line; the line follows ----
    QString onLine;
    for (const StationLayout::Line &ln : w.station().lines)
        if (!ln.tags.isEmpty() && indexOf(w.station(), ln.tags.first()) >= 0
            && !ln.tags.first().endsWith(QLatin1Char('D'))) { onLine = ln.tags.first(); break; }
    CHECK(!onLine.isEmpty(), "a main tag on a line");
    tb = w.openInTagBuilder(onLine);
    tb->setField(QStringLiteral("unique"), unused);
    tb->applyButton()->click();
    bool lineFollows = false, oldGone = true;
    for (const StationLayout::Line &ln : w.station().lines) {
        lineFollows = lineFollows || ln.tags.contains(QString::number(unused));
        oldGone = oldGone && !ln.tags.contains(onLine);
    }
    CHECK(indexOf(w.station(), onLine) < 0 && indexOf(w.station(), QString::number(unused)) >= 0 && lineFollows && oldGone,
          QByteArray("tag ") + onLine.toUtf8() + " renamed; its line names the new tag");
    w.undo();

    // ---- refused: a rename onto a tag that exists ----
    tb = w.openInTagBuilder(QStringLiteral("981"));
    QString otherMain;
    for (const StationLayout::Tag &t : w.station().tags)
        if (t.name != QLatin1String("981") && !t.name.endsWith(QLatin1Char('D')) && StationLayout::info(t).ok) { otherMain = t.name; break; }
    CHECK(!otherMain.isEmpty() && tb->setField(QStringLiteral("unique"), otherMain.toInt()), "981 given another tag's id");
    const int before = w.station().tags.size();
    tb->applyButton()->click();
    CHECK(w.station().tags.size() == before && indexOf(w.station(), QStringLiteral("981")) >= 0,
          QByteArray("renaming 981 onto tag ") + otherMain.toUtf8() + " is refused");

    // ---- double-click a tag in the drawing ----
    w.resize(1400, 800);
    w.show();
    QApplication::processEvents();
    tb->close();
    QApplication::processEvents();
    const QPointF c = w.canvas()->tagRect(QStringLiteral("981")).center();
    QMouseEvent dbl(QEvent::MouseButtonDblClick, c, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w.canvas(), &dbl);
    CHECK(w.tagBuilder() && w.tagBuilder()->isVisible() && w.tagBuilder()->applyButton()->text().contains(QStringLiteral("981")),
          "double-clicking tag 981 in the drawing opens it in the Tag Builder");
}
