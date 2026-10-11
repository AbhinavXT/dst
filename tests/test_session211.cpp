#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QAction>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>

// =============================================================================
//  Session 211 — the built-in station layout library: every station file of
//  the old Python tool (config/station/*.xlsx, scripts/station_layout_library.py),
//  under Open ▸ Library in Station Layout and the Track diagram's menu.
// =============================================================================

TEST_SUITE(session211)
{
    const QStringList lib = StationLayout::library();
    CHECK(lib.size() == 47, QByteArray("47 built-in station files (") + QByteArray::number(lib.size()) + ")");
    CHECK(lib.contains(StationLayout::defaultFile()), "the default is one of them");
    QSet<QString> groups;
    for (const QString &p : lib) groups.insert(StationLayout::libraryGroup(p));
    CHECK(groups == QSet<QString>({ QString(), QStringLiteral("Originals"), QStringLiteral("ajay"), QStringLiteral("sOs_adj") }),
          "grouped as the zip's folders");
    CHECK(StationLayout::libraryGroup(lib.first()).isEmpty(), "the top level first");

    // Every file reads as a layout with tags; names a Windows checkout accepts.
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9._+-]+$"));
    QStringList bad;
    int tags = 0;
    for (const QString &p : lib) {
        StationLayout::Layout l;
        QString err;
        if (!StationLayout::load(p, &l, &err) || l.tags.isEmpty()) bad << p + QStringLiteral(": ") + err;
        tags += l.tags.size();
        if (!safe.match(p.section(QLatin1Char('/'), -1)).hasMatch()) bad << p + QStringLiteral(": name");
    }
    CHECK(bad.isEmpty(), QByteArray("every file reads, with a safe name (") + bad.join(QStringLiteral("; ")).toUtf8() + ")");
    CHECK(tags > 47 * 50, "real layouts, not empty shells");

    // The menu: one entry per file, groups as submenus, picking hands back the path.
    QWidget host;
    QString picked;
    QMenu *m = stationLibraryMenu(&host, [&picked](const QString &p) { picked = p; });
    int entries = 0, submenus = 0;
    QAction *ajayFirst = nullptr;
    for (QAction *a : m->actions()) {
        if (a->menu()) {
            ++submenus;
            entries += a->menu()->actions().size();
            if (a->menu()->title() == QLatin1String("ajay")) ajayFirst = a->menu()->actions().value(0);
        } else ++entries;
    }
    CHECK(entries == 47 && submenus == 3, "47 entries, three group submenus");
    CHECK(ajayFirst != nullptr, "an ajay submenu");
    if (ajayFirst) {
        ajayFirst->trigger();
        CHECK(picked == ajayFirst->data().toString() && StationLayout::libraryGroup(picked) == QLatin1String("ajay"),
              "picking an entry hands back its path");
    }

    // Opened in the window: not modified, Save asks for a path of its own.
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(":/station_layouts/station_lingampalli.xlsx")) && !w.isModified()
          && w.station().tags.size() == 175, "Lingampalli opens from the library (175 tags)");
}
