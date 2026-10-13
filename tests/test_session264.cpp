#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"

#include <QFile>
#include <QRegularExpression>

// =============================================================================
//  Session 264 — the REV route named the way it runs: DN_804_908 and
//  REV_DN_908_804 (was REV_DN_804_908). Real routes: tag_scenarios/Hafizpet
//  (in git): DN_MAIN and UP_LOOP_1, and its Configuration1.xml.
// =============================================================================

namespace {
QString hfz(const QString &name) { return QStringLiteral(DL_SRC_DIR "/tag_scenarios/Hafizpet/") + name; }
QByteArray readAll264(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST_SUITE(session264)
{
    for (const QString &file : { QStringLiteral("DN_MAIN.tagroute.xml"), QStringLiteral("UP_LOOP_1.tagroute.xml") }) {
        const RfidTag::Route r = RfidTag::readFile(hfz(file)).value(0);
        CHECK(!r.tags.isEmpty() && (r.dir == RfidTag::DirNominal || r.dir == RfidTag::DirReverse),
              QByteArray("fixture: ") + file.toUtf8());
        const QVector<RfidTag::Route> two = RfidTag::readXml(RfidTag::toRouteXml(r));
        const QRegularExpression name(QStringLiteral("^(DN|UP)_(\\d+)_(\\d+)$"));
        const QRegularExpressionMatch m = two.size() == 2 ? name.match(two.at(0).name) : QRegularExpressionMatch();
        CHECK(m.hasMatch() && m.captured(1) == (r.dir == RfidTag::DirNominal ? QLatin1String("DN") : QLatin1String("UP")),
              QByteArray("the route: <DN|UP>_<first>_<last>: ") + (two.isEmpty() ? QByteArray() : two.at(0).name.toUtf8()));
        const QString rev = QStringLiteral("REV_%1_%2_%3").arg(m.captured(1), m.captured(3), m.captured(2));
        CHECK(two.size() == 2 && two.at(1).name == rev,
              QByteArray("its REV route, first and last swapped: ") + (two.size() == 2 ? two.at(1).name.toUtf8() : QByteArray())
                  + " (expected " + rev.toUtf8() + ")");
        // The REV route's first tag is the route's last main tag: the name says what it runs.
        CHECK(two.size() == 2 && RfidTag::nameOf(two.at(1).tags.first().bytes).contains(m.captured(3)),
              "and its first tag is the one its name starts with");
        // The Configuration1.xml route export names it the same.
        CHECK(RfidExport::configurationRoute(r).contains(QStringLiteral("route_name=\"%1\"").arg(rev).toUtf8()),
              "Route for Configuration1.xml: the same name");
    }

    // A Configuration1.xml merged into before 264 holds REV_<route's name>.
    {
        const QByteArray conf = readAll264(hfz(QStringLiteral("Configuration1.xml")));
        const RfidTag::Route r = RfidTag::readFile(hfz(QStringLiteral("DN_MAIN.tagroute.xml"))).value(0);
        QStringList notes;
        QString err;
        QByteArray merged = RfidExport::mergeIntoConfiguration(conf, r, &err, &notes);
        const QVector<RfidTag::Route> once = RfidTag::readXml(merged);
        CHECK(!merged.isEmpty() && once.size() == RfidTag::readXml(conf).size() + 2, QByteArray("fixture: merged once: ") + err.toUtf8());
        const QString name = RfidTag::readXml(RfidTag::toRouteXml(r)).value(0).name;
        const QString rev = RfidTag::readXml(RfidTag::toRouteXml(r)).value(1).name;
        // As an older DLConsole wrote it.
        const QString old = QStringLiteral("REV_") + name;
        QByteArray oldStyle = merged;
        oldStyle.replace(QStringLiteral("route_name=\"%1\"").arg(rev).toUtf8(), QStringLiteral("route_name=\"%1\"").arg(old).toUtf8());
        notes.clear();
        const QByteArray again = RfidExport::mergeIntoConfiguration(oldStyle, r, &err, &notes);
        const QVector<RfidTag::Route> back = RfidTag::readXml(again);
        int revs = 0, olds = 0;
        for (const RfidTag::Route &b : back) {
            revs += b.name == rev ? 1 : 0;
            olds += b.name == old ? 1 : 0;
        }
        CHECK(!again.isEmpty() && back.size() == once.size() && revs == 1 && olds == 0,
              QByteArray("merging into it replaces the old-named REV route, no second one: ") + err.toUtf8());
        CHECK(notes.contains(QStringLiteral("replaced route %1 (now %2)").arg(old, rev)),
              QByteArray("and says so: ") + notes.join(QStringLiteral(", ")).toUtf8());
    }
}
