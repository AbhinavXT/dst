#include "testutil.h"
#include "layoutaudit.h"

#include "rfidtag.h"
#include "settings.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "taglibrary.h"
#include "theme.h"
#include "uistyle.h"

#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>

// =============================================================================
//  Session 201 — the tag scenario library: tags_sim's KAV_CONFIG routes built
//  into DLConsole (:/tag_scenarios, tag_scenarios/ via tag_scenarios.qrc,
//  made by scripts/tags_sim_library.py), a library folder beside it, and the
//  last route reopened.
// =============================================================================

namespace {

QTreeWidgetItem *find(QTreeWidgetItem *it, const QString &text)
{
    if (it->text(0) == text && !it->isHidden()) return it;
    for (int i = 0; i < it->childCount(); ++i)
        if (QTreeWidgetItem *f = find(it->child(i), text)) return f;
    return nullptr;
}

QTreeWidgetItem *find(QTreeWidget *t, const QString &text)
{
    for (int i = 0; i < t->topLevelItemCount(); ++i)
        if (QTreeWidgetItem *f = find(t->topLevelItem(i), text)) return f;
    return nullptr;
}

}  // namespace

TEST_SUITE(session201)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QString keepLibrary = Settings::tagLibraryPath();
    const QString keepLast = Settings::tagBuilderLastFile();
    const int keepRoute = Settings::tagBuilderLastRoute();

    // ---- 1. what is built in ------------------------------------------------------------------
    const QVector<TagLibrary::Entry> built = TagLibrary::scan(TagLibrary::builtInRoot());
    int fromXlsx = 0, withSignals = 0, configRoutes = 0, tags = 0;
    const TagLibrary::Entry *dnMain = nullptr;
    for (const TagLibrary::Entry &e : built) {
        fromXlsx += e.fileName.endsWith(QLatin1String(".tagroute.xml")) ? 1 : 0;
        withSignals += e.signalCount > 0 ? 1 : 0;
        configRoutes += e.fileName == QLatin1String("Configuration1.xml") && e.folder.isEmpty() ? 1 : 0;
        tags += e.tags;
        if (e.folder == QLatin1String("Hafizpet") && e.fileName == QLatin1String("DN_MAIN.tagroute.xml")) dnMain = &e;
    }
    CHECK(fromXlsx == 163 && configRoutes == 145 && built.size() > 400 && tags > 10000,
          QByteArray("built in: tags_sim's 163 xlsx routes, KAV_CONFIG's Configuration1.xml (145 routes) and the rest: ")
              + QByteArray::number(built.size()) + " routes, " + QByteArray::number(tags) + " tags");
    CHECK(dnMain && dnMain->tags == 42 && dnMain->signalCount == 9 && dnMain->dir == RfidTag::DirNominal,
          "Hafizpet DN_MAIN: its 42 tags, its 9 signals, and nominal (its locations rise, as tags_sim read it)");
    CHECK(withSignals > 100, QByteArray("the xlsx routes keep their signals: ") + QByteArray::number(withSignals));

    // ---- 2. the dialog ------------------------------------------------------------------------
    QTemporaryDir folder;
    QDir(folder.path()).mkpath(QStringLiteral("Ours"));
    RfidTag::Route own;
    own.name = QStringLiteral("OUR_TEST");
    own.dir = RfidTag::DirNominal;
    own.tags = { { QString(), RfidTag::build({ { QStringLiteral("type"), 9 }, { QStringLiteral("unique"), 100 },
                                               { QStringLiteral("abs_loc"), 1000 } }) } };
    RfidTag::writeFile(folder.filePath(QStringLiteral("Ours/our_test.tagroute.xml")), RfidTag::toTagRouteXml(own));
    Settings::setTagLibraryPath(folder.path());
    {
        TagLibraryDialog dlg;
        dlg.show();
        CHECK(dlg.tree()->topLevelItemCount() == 2 && dlg.tree()->topLevelItem(0)->text(0).startsWith(QLatin1String("Built in"))
                  && dlg.tree()->topLevelItem(1)->text(0).contains(QLatin1String("(1 routes)")),
              QByteArray("two places: built in, and the folder with its one route: ")
                  + dlg.tree()->topLevelItem(1)->text(0).toUtf8());
        CHECK(dlg.visibleRoutes() == built.size() + 1, "every route listed");
        CHECK(find(dlg.tree(), QStringLiteral("Hafizpet")) && find(dlg.tree(), QStringLiteral("Configuration1.xml  (145 routes)")),
              "folders as in KAV_CONFIG; a Configuration1.xml is a node holding its routes");

        dlg.setFilter(QStringLiteral("hafizpet"));
        const int hafizpet = dlg.visibleRoutes();
        int inHafizpet = 0;
        for (const TagLibrary::Entry &e : built) inHafizpet += e.folder.startsWith(QLatin1String("Hafizpet")) ? 1 : 0;
        CHECK(hafizpet == inHafizpet && hafizpet < built.size() && find(dlg.tree(), QStringLiteral("DN_MAIN.tagroute.xml")),
              QByteArray("filter by folder: Hafizpet's routes, its Configuration1.xml's included (") + QByteArray::number(hafizpet) + ")");
        CHECK(!dlg.choose(find(dlg.tree(), QStringLiteral("Hafizpet"))), "a folder is not a route to open");
        CHECK(dlg.choose(find(dlg.tree(), QStringLiteral("DN_MAIN.tagroute.xml")))
                  && dlg.chosen().file == QLatin1String(":/tag_scenarios/Hafizpet/DN_MAIN.tagroute.xml") && dlg.chosen().route == 0,
              "choosing a route gives its file and index");
        dlg.setFilter(QStringLiteral("our_test"));
        CHECK(dlg.visibleRoutes() == 1 && dlg.choose(find(dlg.tree(), QStringLiteral("our_test.tagroute.xml")))
                  && dlg.chosen().file.startsWith(folder.path()),
              "a route in the library folder is found too");
        dlg.setFilter(QString());
        CHECK(dlg.visibleRoutes() == built.size() + 1, "clearing the filter shows everything again");
    }

    // ---- 3. open, and reopen next time --------------------------------------------------------
    {
        TagBuilderWindow w;
        w.show();
        CHECK(w.loadFile(QStringLiteral(":/tag_scenarios/Hafizpet/DN_MAIN.tagroute.xml"), 0)
                  && w.route().tags.size() == 42 && w.route().signalList.size() == 9 && w.route().dir == RfidTag::DirNominal,
              "a built-in route opens: tags, signals, direction");
        CHECK(Settings::tagBuilderLastFile() == QLatin1String(":/tag_scenarios/Hafizpet/DN_MAIN.tagroute.xml")
                  && Settings::tagBuilderLastRoute() == 0,
              "and is remembered");
        w.setRoute(RfidTag::Route());
    }
    {
        TagBuilderWindow w;
        w.show();
        CHECK(w.route().tags.isEmpty(), "a window starts empty");
        CHECK(w.reopenLast() && w.route().tags.size() == 42 && w.route().name == QLatin1String("DN_MAIN"),
              "reopenLast: the route last opened is back");
        const QString conf = QStringLiteral(":/tag_scenarios/Configuration1.xml");
        CHECK(w.loadFile(conf, 7) && Settings::tagBuilderLastRoute() == 7, "a route inside a Configuration1.xml: its index remembered");
        Settings::setTagBuilderLast(folder.filePath(QStringLiteral("gone.xml")), 0);
        TagBuilderWindow w2;
        CHECK(!w2.reopenLast() && w2.route().tags.isEmpty(), "a remembered file that is gone: nothing opened, no error");
        CHECK(w.minimumSizeHint().width() <= 1100 && LayoutAudit::orphans(&w).isEmpty(),
              QByteArray("the Library button fits (minimum width ") + QByteArray::number(w.minimumSizeHint().width()) + ")");
        w.setRoute(RfidTag::Route());
    }

    Settings::setTagLibraryPath(keepLibrary);
    Settings::setTagBuilderLast(keepLast, keepRoute);
}
