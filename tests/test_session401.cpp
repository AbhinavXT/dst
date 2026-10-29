#include "testutil.h"

#include "rfidtag.h"
#include "routestrip.h"
#include "settings.h"
#include "tagbuilderwindow.h"
#include "tageditdialog.h"
#include "undolog.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QSettings>

#include <cmath>
#include <cstdlib>
#include <memory>

// =============================================================================
//  Session 401 — the Tag Builder: change or add a tag straight on the route
//  drawing. Double-click a tag: a window with its fields, OK replaces it in
//  the route. Right-click the line: New tag here… (the nearest main tag's
//  type and TINs, an unused id, the clicked metre), Edit tag…, Delete tag.
//  Each is one Undo step. The route is LocoTcasSimulator's own
//  Configuration1.xml (the scenario library's copy).
// =============================================================================

namespace {

QString locField401(const QByteArray &tag)
{
    return RfidTag::summary(tag).type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc");
}

}  // namespace

TEST_SUITE(session401)
{
    const QVector<RfidTag::Route> routes
        = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty() && routes.first().tags.size() >= 6, "the simulator's Configuration1.xml: a route of 6 tags or more");
    if (routes.isEmpty() || routes.first().tags.size() < 6) return;
    const RfidTag::Route route = routes.first();
    CHECK(route.dir == RfidTag::DirNominal || route.dir == RfidTag::DirReverse, "with its direction set");

    TagBuilderWindow builder(nullptr);
    builder.setAttribute(Qt::WA_DeleteOnClose, false);
    builder.setRoute(route);
    RouteStrip *strip = builder.strip();

    // ---- the strip: where a click is -------------------------------------------------
    strip->resize(800, 100);
    const qint64 loc0 = RfidTag::summary(route.tags.at(0).bytes).absLoc;
    CHECK(std::fabs(strip->locAt(strip->xAt(double(loc0))) - double(loc0)) < 1e-6, "locAt is xAt the other way round");

    // ---- double-click a tag: change it ---------------------------------------------------
    const int row = 2;
    const QByteArray before = route.tags.at(row).bytes;
    emit strip->rowDoubleClicked(row);
    TagEditDialog *dialog = builder.findChild<TagEditDialog *>();
    CHECK(dialog && dialog->isVisible() && dialog->tag() == before, "a double-click on a tag opens it in a window of its own");
    if (!dialog) return;
    const qint64 moved = RfidTag::summary(before).absLoc + 7;
    CHECK(dialog->setField(locField401(before), moved), "its location can be set there");
    CHECK(RfidTag::summary(dialog->tag()).crcOk && RfidTag::summary(dialog->tag()).absLoc == moved,
          "the window's tag: the new location, its CRC-30 computed");
    CHECK(builder.route().tags.at(row).bytes == before, "nothing changes in the route before OK");
    dialog->accept();
    CHECK(builder.route().tags.at(row).bytes == dialog->tag() && builder.isModified(), "OK: the row is the window's tag");
    CHECK(builder.undo() && builder.route().tags.at(row).bytes == before, "one Undo step puts it back");

    // Cancel leaves the route alone.
    dialog = builder.editRowInDialog(row);
    CHECK(dialog, "Edit tag… opens it too");
    if (!dialog) return;
    dialog->setField(locField401(before), moved);
    dialog->reject();
    CHECK(builder.route().tags.at(row).bytes == before, "Cancel: the route as it was");

    // ---- New tag here… ------------------------------------------------------------------------
    // Half way between two main tags, in the order the loco meets them.
    int a = -1, b = -1;
    for (int i = 0; i < route.tags.size() && b < 0; ++i) {
        const RfidTag::Summary s = RfidTag::summary(route.tags.at(i).bytes);
        if (s.duplicate || s.absLoc == 0) continue;
        if (a < 0) a = i;
        else if (std::llabs(s.absLoc - RfidTag::summary(route.tags.at(a).bytes).absLoc) > 40) b = i;
        else a = i;
    }
    CHECK(a >= 0 && b > a, "two main tags more than 40 m apart");
    if (a < 0 || b <= a) return;
    const qint64 la = RfidTag::summary(route.tags.at(a).bytes).absLoc;
    const qint64 lb = RfidTag::summary(route.tags.at(b).bytes).absLoc;
    const qint64 middle = (la + lb) / 2;
    dialog = builder.newTagInDialog(middle);
    CHECK(dialog, "New tag here… opens a window");
    if (!dialog) return;
    const RfidTag::Summary made = RfidTag::summary(dialog->tag());
    QSet<int> used;
    for (const RfidTag::Tag &t : route.tags) used.insert(RfidTag::summary(t.bytes).unique);
    CHECK(made.absLoc == middle && !made.duplicate && made.crcOk && !used.contains(made.unique),
          "the new tag: the clicked metre, a main tag, an id the route does not use, a good CRC-30");
    const int sizeBefore = builder.route().tags.size();
    dialog->accept();
    CHECK(builder.route().tags.size() == sizeBefore + 1, "OK: added");
    int at = -1;
    for (int i = 0; i < builder.route().tags.size(); ++i)
        if (builder.route().tags.at(i).bytes == dialog->tag()) at = i;
    CHECK(at > a && at <= b, qPrintable(QStringLiteral("in the route between the two tags it lies between (row %1, between %2 and %3)")
                                             .arg(at).arg(a).arg(b)));
    CHECK(builder.undo() && builder.route().tags.size() == sizeBefore, "one Undo step takes it out");

    // ---- the right-click menu -------------------------------------------------------------------
    {
        std::unique_ptr<QMenu> onTag(builder.stripMenu(row, double(RfidTag::summary(before).absLoc)));
        QStringList texts;
        for (QAction *action : onTag->actions())
            if (!action->isSeparator()) texts << action->text();
        CHECK(texts.size() == 3 && texts.at(0).startsWith(QLatin1String("Edit tag")) && texts.at(1).startsWith(QLatin1String("Delete tag"))
                  && texts.at(2).startsWith(QLatin1String("New tag here")),
              qPrintable(QStringLiteral("on a tag: Edit tag…, Delete tag, New tag here… (%1)").arg(texts.join(QLatin1String(" | ")))));
        const int count = builder.route().tags.size();
        onTag->actions().at(1)->trigger();
        CHECK(builder.route().tags.size() == count - 1, "Delete tag deletes it");
        builder.undo();
        std::unique_ptr<QMenu> offTag(builder.stripMenu(-1, double(middle)));
        int real = 0;
        for (QAction *action : offTag->actions()) real += action->isSeparator() ? 0 : 1;
        CHECK(real == 1 && offTag->actions().first()->text().startsWith(QLatin1String("New tag here")),
              "off the tags: only New tag here…");
    }
    CHECK(builder.route().tags.size() == route.tags.size(), "back to the file's tags after the undos");
}
