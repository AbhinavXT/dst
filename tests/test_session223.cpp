#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "stationlayoutwindow.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMouseEvent>

// =============================================================================
//  Session 223 — Station Layout: several tags at once. Shift + drag boxes
//  tags in the drawing, Ctrl + click toggles one; the selection is the Tags
//  table's. Shift by N m, set a field, delete — each one Undo step, refused
//  whole. The built-in station.xlsx.
// =============================================================================

namespace {
StationLayout::TagInfo infoOf(const StationLayout::Layout &l, const QString &name)
{
    for (const StationLayout::Tag &t : l.tags) if (t.name == name) return StationLayout::info(t);
    return StationLayout::TagInfo();
}
}  // namespace

TEST_SUITE(session223)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the built-in layout opens");
    w.resize(1400, 800);
    w.setStepsShown(false);   // session 459: the drawing at the width these numbers were set for
    w.show();
    QApplication::processEvents();
    StationLayoutCanvas *c = w.canvas();

    // Three neighbouring main tags of one line.
    const QHash<QString, qint64> loc = StationLayout::tagLocations(w.station());
    QStringList three;
    for (const StationLayout::Line &ln : w.station().lines) {
        three.clear();
        for (const QString &n : ln.tags)
            if (loc.contains(n) && !n.endsWith(QLatin1Char('D')) && three.size() < 3) three << n;
        if (three.size() == 3) break;
    }
    CHECK(three.size() == 3, three.join(QLatin1Char(',')).toUtf8());
    if (three.size() != 3) return;

    // ---- Shift + drag round them ----
    QRectF box = c->tagRect(three.at(0)) | c->tagRect(three.at(1)) | c->tagRect(three.at(2));
    box.adjust(-2, -2, 2, 2);
    // The box as the drawing makes it, in whole pixels.
    const QStringList inBox = c->tagsIn(QRect(box.topLeft().toPoint(), box.bottomRight().toPoint()).normalized());
    for (const QString &n : three) CHECK(inBox.contains(n), "the box holds " + n.toUtf8());
    QMouseEvent press(QEvent::MouseButtonPress, box.topLeft(), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent move(QEvent::MouseMove, box.bottomRight(), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, box.bottomRight(), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QApplication::sendEvent(c, &press);
    QApplication::sendEvent(c, &move);
    QApplication::sendEvent(c, &release);
    QStringList sel = w.selectedTags();
    CHECK(sel.size() == inBox.size() && sel.contains(three.at(0)) && sel.contains(three.at(2)),
          QByteArray("Shift + drag selects them in the Tags table (") + sel.join(QLatin1Char(',')).toUtf8() + ")");

    // ---- Ctrl + click takes one out ----
    const QPointF mid = c->tagRect(three.at(1)).center();
    QMouseEvent ctrl(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(c, &ctrl);
    QMouseEvent ctrlUp(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(c, &ctrlUp);
    CHECK(!w.selectedTags().contains(three.at(1)) && w.selectedTags().contains(three.at(0)), "Ctrl + click removes one");
    w.selectTags(three);
    CHECK(w.selectedTags().size() == 3, "selectTags: the three");

    // ---- the menu ----
    QMenu *m = w.contextMenuFor(QString(), QString(), 0, false);
    QStringList texts;
    for (QAction *a : m->actions()) texts << a->text();
    delete m;
    const QString all = texts.join(QLatin1Char('|'));
    CHECK(all.contains(QStringLiteral("Shift the 3 selected tags")) && all.contains(QStringLiteral("Set a field on the 3 selected tags"))
              && all.contains(QStringLiteral("Delete the 3 selected tags")), all.toUtf8());

    // ---- shift ----
    CHECK(w.shiftTags(three, 25), "shift by 25 m");
    const QHash<QString, qint64> after = StationLayout::tagLocations(w.station());
    bool moved = true, crc = true;
    for (const QString &n : three) {
        moved = moved && after.value(n) == loc.value(n) + 25;
        crc = crc && infoOf(w.station(), n).crcOk;
    }
    CHECK(moved && crc, "each 25 m on, CRC-30 good");
    CHECK(!w.shiftTags(three, -10000000) && StationLayout::tagLocations(w.station()).value(three.at(0)) == loc.value(three.at(0)) + 25,
          "a shift one tag cannot take is refused whole");
    CHECK(w.undo() && StationLayout::tagLocations(w.station()).value(three.at(2)) == loc.value(three.at(2)), "one Undo");

    // ---- set a field ----
    const int tin = infoOf(w.station(), three.at(0)).tinNom == 7 ? 8 : 7;
    CHECK(w.setFieldOnTags(three, QStringLiteral("tin_nom"), tin), "tin_nom set on all three");
    bool tinSet = true;
    for (const StationLayout::Tag &t : w.station().tags)
        if (three.contains(t.name)) tinSet = tinSet && StationLayout::info(t).tinNom == tin && StationLayout::info(t).crcOk;
    CHECK(tinSet, "each has the TIN, CRC-30 good");
    CHECK(!w.setFieldOnTags(three, QStringLiteral("unique"), 900), "setting the id on several is refused");
    CHECK(!w.setFieldOnTags(three, QStringLiteral("no_such_field"), 1), "a field they lack is refused");
    w.undo();

    // ---- delete ----
    const int n = w.station().tags.size();
    CHECK(w.deleteTags(three) && w.station().tags.size() == n - 3, "three deleted");
    bool gone = true;
    for (const StationLayout::Line &ln : w.station().lines) for (const QString &t : three) gone = gone && !ln.tags.contains(t);
    CHECK(gone, "and out of their line");
    CHECK(w.undo() && w.station().tags.size() == n, "Undo brings them back");
}
