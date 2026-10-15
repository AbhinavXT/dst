#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "tagbuilderwindow.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 282 — Tag Builder: Export ▸ To shared folder. Abhinav: "send
//  exported file over ethernet to other pc"; he chose a shared folder, for
//  the Tag Builder's exports. Real route: tags_sim's DN_MN904_720route.xml.
// =============================================================================

namespace {
QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST_SUITE(session282)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where, shared, local;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    const QVector<RfidTag::Route> routes =
        RfidTag::readFile(QStringLiteral(DL_SRC_DIR "/tag_scenarios/Hafizpet/DN_MN904_720route.xml"));
    CHECK(!routes.isEmpty(), "fixture: tags_sim's route");
    if (routes.isEmpty()) return;

    TagBuilderWindow w;
    w.setRoute(routes.first());
    using K = TagBuilderWindow::SharedExport;

    CHECK(Settings::tagSharedFolder().isEmpty() && !w.exportToShared(K::RouteXml, false),
          "no shared folder set: nothing written");

    Settings::setTagSharedFolder(shared.path() + QStringLiteral("/gone"));
    CHECK(!w.exportToShared(K::RouteXml, false), "a folder that cannot be reached: refused");

    Settings::setTagSharedFolder(shared.path());
    const QDir dir(shared.path());

    // Each export lands under its usual name, the same bytes as the plain export.
    CHECK(w.exportToShared(K::RouteXml, false) && w.exportRouteXml(local.filePath(QStringLiteral("route.xml"))),
          "route.xml written to the shared folder");
    CHECK(readAll(dir.filePath(QStringLiteral("route.xml"))) == readAll(local.filePath(QStringLiteral("route.xml"))),
          "the same route.xml as Export > route.xml writes");

    const QString cfg = w.sharedFileNames(K::ConfigurationRoute).value(0);
    CHECK(w.exportToShared(K::ConfigurationRoute, false) && QFile::exists(dir.filePath(cfg)),
          QByteArray("the route for Configuration1.xml: ") + cfg.toUtf8());

    w.selectRows({});
    CHECK(!w.exportToShared(K::SelectedTag, false), "no tag selected: refused");
    w.selectRows({ 2 });
    const QString tag = w.sharedFileNames(K::SelectedTag).value(0);
    CHECK(tag == QLatin1String("873_rfid_data.xml") && w.exportToShared(K::SelectedTag, false)
              && readAll(dir.filePath(tag)).contains("rfid_id=\"873\""),
          "the selected tag, 873_rfid_data.xml");

    const QStringList texts = w.sharedFileNames(K::TextFiles);
    bool allThere = w.exportToShared(K::TextFiles, false) && texts.size() == 3;
    for (const QString &t : texts) allThere = allThere && QFile::exists(dir.filePath(t));
    CHECK(allThere, QByteArray("the three text files (") + texts.join(QStringLiteral(", ")).toUtf8() + ")");

    CHECK(w.exportToShared(K::RouteXml, false), "an existing file is replaced when not asked");

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
