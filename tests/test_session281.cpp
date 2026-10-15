#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"
#include "tagbuilderwindow.h"

#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 281 — Tag Builder: export one tag. Abhinav: "single tag export in
//  tag builder"; he chose the tag's <rfid_data> line. Real route:
//  tag_scenarios/Hafizpet/DN_MN904_720route.xml, a route.xml tags_sim wrote:
//  each exported line must be the very line tags_sim wrote for that tag.
// =============================================================================

namespace {
QString fx(const QString &name) { return QStringLiteral(DL_SRC_DIR "/tag_scenarios/Hafizpet/") + name; }

// tags_sim's own <rfid_data .../> lines of the file's first <route_data>, trimmed.
QStringList ownLines(const QString &path)
{
    QStringList out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String("</route_data>"))) break;
        if (l.startsWith(QLatin1String("<rfid_data"))) out << l;
    }
    return out;
}
}  // namespace

TEST_SUITE(session281)
{
    const QString path = fx(QStringLiteral("DN_MN904_720route.xml"));
    const QVector<RfidTag::Route> routes = RfidTag::readFile(path);
    const QStringList own = ownLines(path);
    CHECK(!routes.isEmpty() && routes.first().tags.size() == own.size() && own.size() > 4,
          "fixture: tags_sim's route, one line per tag");
    if (routes.isEmpty() || own.isEmpty()) return;
    const RfidTag::Route r = routes.first();

    int same = 0;
    for (int i = 0; i < r.tags.size(); ++i) {
        QString err;
        const QString line = QString::fromUtf8(RfidExport::tagRow(r, i, &err)).trimmed();
        if (line == own.at(i)) ++same;
        else if (same == i) printf("      tag %d: %s\n      tags_sim: %s\n", i, qPrintable(line), qPrintable(own.at(i)));
    }
    CHECK(same == r.tags.size(), QByteArray("every tag's line is tags_sim's own (") + QByteArray::number(same)
                                     + "/" + QByteArray::number(r.tags.size()) + ")");

    QString err;
    CHECK(RfidExport::tagRow(r, -1, &err).isEmpty() && err.contains(QStringLiteral("select")), "no tag selected: refused");
    RfidTag::Route unset = r;
    unset.dir = RfidTag::DirUnset;
    CHECK(RfidExport::tagRow(unset, 0, &err).isEmpty() && err.contains(QStringLiteral("direction")),
          "direction unset: refused, as route.xml is");

    // ---- the window ------------------------------------------------------------------------------
    QTemporaryDir out;
    TagBuilderWindow w;
    w.setRoute(r);
    w.selectRows({ 2 });
    const QString file = out.filePath(QStringLiteral("873_rfid_data.xml"));
    CHECK(w.exportSelectedTag(file), "Export > Selected tag writes the file");
    QFile f(file);
    CHECK(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).trimmed() == own.at(2),
          "holding the third tag's line, 873");
    w.selectRows({});
    CHECK(!w.exportSelectedTag(out.filePath(QStringLiteral("none.xml"))), "nothing selected: not exported");
}
