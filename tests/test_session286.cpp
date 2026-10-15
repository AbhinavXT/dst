#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "statusline.h"
#include "tagbuilderwindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 286 — Tag Builder: Export ▸ Everything, in a route folder. One
//  folder named after the route with every export and a README.txt. Real
//  route: tag_scenarios' DN_MN904_720route.xml (tracked; tags_sim is not).
// =============================================================================

namespace {
QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST_SUITE(session286)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where, out, local;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    const QVector<RfidTag::Route> routes =
        RfidTag::readFile(QStringLiteral(DL_SRC_DIR "/tag_scenarios/Hafizpet/DN_MN904_720route.xml"));
    CHECK(!routes.isEmpty(), "fixture: the Hafizpet DN route");
    if (routes.isEmpty()) return;

    TagBuilderWindow w;
    using K = TagBuilderWindow::SharedExport;

    // The folder name: the route's name, less what Windows forbids.
    RfidTag::Route odd = routes.first();
    odd.name = QStringLiteral("DN:MN/904?. ");
    w.setRoute(odd);
    CHECK(w.bundleFolderName() == QLatin1String("DN_MN_904_"), "forbidden characters and a trailing dot/space go");
    odd.name.clear();
    w.setRoute(odd);
    CHECK(w.bundleFolderName() == QLatin1String("route"), "no name: \"route\"");

    w.setRoute(routes.first());
    const QString name = w.bundleFolderName();
    CHECK(w.sharedFileNames(K::Everything) == QStringList{ name }, "the shared-folder name is the folder's");

    CHECK(!w.exportEverything(out.path() + QStringLiteral("/gone"), false)
              && w.status()->kind() == StatusLine::Kind::Fail,
          "a parent folder that cannot be reached: refused");

    CHECK(w.exportEverything(out.path(), false), "exported");
    const QDir dir(QDir(out.path()).filePath(name));
    QStringList files = dir.entryList(QDir::Files);
    CHECK(files.size() == 7, QByteArray("seven files: ") + files.join(QStringLiteral(", ")).toUtf8());
    CHECK(files.contains(name + QStringLiteral(".tagroute.xml")) && files.contains(QStringLiteral("route.xml"))
              && files.contains(QStringLiteral("README.txt")),
          "the route, route.xml and the README");
    CHECK(w.status()->kind() == StatusLine::Kind::Ok, "status: ok, nothing skipped");

    // Each file is the same bytes as its own export.
    CHECK(w.exportRouteXml(local.filePath(QStringLiteral("route.xml")))
              && readAll(dir.filePath(QStringLiteral("route.xml"))) == readAll(local.filePath(QStringLiteral("route.xml"))),
          "route.xml: the same as Export > route.xml");
    CHECK(readAll(dir.filePath(name + QStringLiteral(".tagroute.xml"))) == RfidTag::toTagRouteXml(w.route()),
          ".tagroute.xml: the route as Save writes it");
    CHECK(w.exportTextFiles(local.path()), "the text files, exported on their own");
    bool same = true;
    for (const QString &t : w.sharedFileNames(K::TextFiles))
        same = same && !readAll(dir.filePath(t)).isEmpty() && readAll(dir.filePath(t)) == readAll(local.filePath(t));
    CHECK(same, "the three text files: the same as Export > Text files");

    const QString readme = QString::fromUtf8(readAll(dir.filePath(QStringLiteral("README.txt"))));
    CHECK(readme.contains(QStringLiteral("Direction: Nominal (dir 1, DN)")), "README: the direction");
    CHECK(readme.contains(QStringLiteral("Tags:      %1").arg(w.route().tags.size())), "README: the tag count");
    int badCrc = 0;
    for (const RfidTag::Tag &t : w.route().tags) badCrc += RfidTag::summary(t.bytes).crcOk ? 0 : 1;
    CHECK(badCrc == 0 ? readme.contains(QStringLiteral("CRC-30: every tag's CRC matches"))
                      : readme.contains(QStringLiteral("CRC-30: %1 tag(s) fail their CRC").arg(badCrc)),
          QByteArray("README: the CRCs (") + QByteArray::number(badCrc) + " failing)");
    CHECK(readme.contains(QStringLiteral("  route.xml")) && !readme.contains(QStringLiteral("Not written")),
          "README: route.xml listed, nothing left out");

    // bundleReadme names what was skipped and the time given.
    const QString r = w.bundleReadme({ QStringLiteral("a.xml") }, { QStringLiteral("route.xml: why") },
                                     QDateTime(QDate(2026, 10, 15), QTime(9, 30, 5)));
    CHECK(r.contains(QStringLiteral("Written 2026-10-15 09:30:05")) && r.contains(QStringLiteral("Not written:\n  route.xml: why")),
          "README: the time, and what was not written");

    // No direction: route.xml cannot be made; left out, said so.
    RfidTag::Route unset = routes.first();
    unset.dir = RfidTag::DirUnset;
    unset.name = QStringLiteral("unset");
    w.setRoute(unset);
    CHECK(w.exportEverything(out.path(), false), "no direction: still exported");
    const QDir dir2(QDir(out.path()).filePath(QStringLiteral("unset")));
    CHECK(!dir2.exists(QStringLiteral("route.xml")) && dir2.exists(QStringLiteral("unset.tagroute.xml")),
          "route.xml left out, the route written");
    CHECK(w.status()->kind() == StatusLine::Kind::Warn && w.status()->message().contains(QStringLiteral("route.xml")),
          "status: warns, naming route.xml");
    CHECK(QString::fromUtf8(readAll(dir2.filePath(QStringLiteral("README.txt")))).contains(QStringLiteral("Not written:")),
          "README: says what was left out");

    // An existing folder is replaced when not asked.
    w.setRoute(routes.first());
    CHECK(w.exportEverything(out.path(), false), "an existing folder's files replaced when not asked");

    // To shared folder ▸ Everything.
    QTemporaryDir shared;
    Settings::setTagSharedFolder(shared.path());
    CHECK(w.exportToShared(K::Everything, false) && QDir(shared.path()).exists(name + QStringLiteral("/README.txt")),
          "To shared folder > Everything");
    Settings::setTagSharedFolder(QString());

    w.setRoute(RfidTag::Route());
    CHECK(!w.exportEverything(out.path(), false), "no tags: refused");

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
