#include "testutil.h"

#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "tagscenario.h"
#include "undolog.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 235 — Tag Builder ▸ Scenario: SIF section 14's cases on a main /
//  duplicate pair, one at a time or the whole set as files named by clause.
//  The route is the built-in tags_sim library's 3.40 (dir 1): tags 2 / 2D
//  and 4D / 4 are two pairs.
// =============================================================================

namespace {

int rowOf(const RfidTag::Route &r, const QString &name)
{
    for (int i = 0; i < r.tags.size(); ++i)
        if (r.tags.at(i).name == name) return i;
    return -1;
}

qint64 locOf(const RfidTag::Route &r, const QString &name)
{
    const int i = rowOf(r, name);
    return i < 0 ? -1 : RfidTag::summary(r.tags.at(i).bytes).absLoc;
}

} // namespace

TEST_SUITE(session235)
{
    using namespace TagScenario;
    QString err;
    const QVector<RfidTag::Route> routes = RfidTag::readFile(QStringLiteral(":/tag_scenarios/3.40.tagroute.xml"), &err);
    CHECK(routes.size() == 1 && routes.first().tags.size() == 28, QByteArray("route 3.40 reads (") + err.toUtf8() + ")");
    if (routes.isEmpty()) return;
    const RfidTag::Route base = routes.first();
    const qint64 m2 = locOf(base, QStringLiteral("2")), d2 = locOf(base, QStringLiteral("2D"));
    const qint64 m4 = locOf(base, QStringLiteral("4")), d4 = locOf(base, QStringLiteral("4D"));
    CHECK(m2 > 0 && d2 > 0 && m4 > 0 && d4 > 0, "two pairs: 2 / 2D, 4 / 4D");

    // ---- one change at a time ------------------------------------------------------------------
    Result r = apply(base, DuplicateMissing, QStringLiteral("2"));
    CHECK(r.ok && r.route.tags.size() == 27 && rowOf(r.route, QStringLiteral("2D")) < 0 && rowOf(r.route, QStringLiteral("2")) >= 0
              && r.change == QLatin1String("2D missing"),
          "duplicate missing: 2D gone, 2 kept");
    r = apply(base, MainMissing, QStringLiteral("2D"));
    CHECK(r.ok && rowOf(r.route, QStringLiteral("2")) < 0 && rowOf(r.route, QStringLiteral("2D")) >= 0,
          "main missing (asked on the duplicate's row): 2 gone");
    r = apply(base, BothMissing, QStringLiteral("2"));
    CHECK(r.ok && r.route.tags.size() == 26 && rowOf(r.route, QStringLiteral("2")) < 0 && rowOf(r.route, QStringLiteral("2D")) < 0,
          "both missing");
    r = apply(base, Interchanged, QStringLiteral("2"));
    CHECK(r.ok && rowOf(r.route, QStringLiteral("2D")) == rowOf(base, QStringLiteral("2"))
              && rowOf(r.route, QStringLiteral("2")) == rowOf(base, QStringLiteral("2D"))
              && locOf(r.route, QStringLiteral("2D")) == d2,
          "interchanged: the rows swap, the locations stay");
    r = apply(base, SameLocation, QStringLiteral("2"));
    const int i2d = rowOf(r.route, QStringLiteral("2D"));
    CHECK(r.ok && locOf(r.route, QStringLiteral("2D")) == m2 && RfidTag::summary(r.route.tags.at(i2d).bytes).crcOk
              && RfidTag::nameOf(r.route.tags.at(i2d).bytes) == QLatin1String("2D"),
          "same location: 2D says 2's location, still 2D, CRC-30 recomputed");
    const int side2 = d2 > m2 ? 1 : -1;
    CHECK(apply(base, GapUnder4, QStringLiteral("2")).ok && locOf(apply(base, GapUnder4, QStringLiteral("2")).route, QStringLiteral("2D")) == m2 + 3 * side2
              && locOf(apply(base, GapOver4, QStringLiteral("2")).route, QStringLiteral("2D")) == m2 + 5 * side2,
          "under 4 m: 3 m from the main on the duplicate's side; over 4 m: 5 m");
    CHECK(base.tags.size() == 28 && locOf(base, QStringLiteral("2D")) == d2, "the route given is never changed");
    r = apply(base, DuplicateMissing, QStringLiteral("804"));
    CHECK(!r.ok && r.why.contains(QStringLiteral("no duplicate")), "a tag with no duplicate: refused, said why");
    r = jumble(base, { QStringLiteral("2"), QStringLiteral("2D"), QStringLiteral("4D") });
    CHECK(r.ok && r.route.tags.at(rowOf(base, QStringLiteral("2"))).name == QLatin1String("4D")
              && r.route.tags.at(rowOf(base, QStringLiteral("4D"))).name == QLatin1String("2"),
          "jumble: the three rows read in reverse order");

    // ---- the section 14 set --------------------------------------------------------------------
    const QVector<Planned> setA = section14(base, QStringLiteral("2"));
    const QVector<Planned> setB = section14(base, QStringLiteral("4"));
    const QString firstA = d2 > m2 ? QStringLiteral("14.1") : QStringLiteral("14.11");
    const QString firstB = d4 > m4 ? QStringLiteral("14.1") : QStringLiteral("14.11");
    CHECK(setA.size() == 8 && setA.first().clause == firstA && setB.first().clause == firstB && firstA != firstB,
          QByteArray("the duplicate's side picks the set: 2 / 2D ") + firstA.toUtf8() + ", 4 / 4D " + firstB.toUtf8());
    QStringList clauses;
    for (const Planned &p : setA) clauses << p.clause;
    const int b = firstA == QLatin1String("14.1") ? 0 : 10;
    QStringList expect;
    for (int n : { 1, 2, 3, 4, 5, 6, 7, 9 }) expect << QStringLiteral("14.%1").arg(b + n);
    CHECK(clauses == expect, "the clauses, without the actual-position ones (14.8, 14.10)");

    // ---- the Tag Builder ----------------------------------------------------------------------
    TagBuilderWindow w;
    CHECK(w.loadFile(QStringLiteral(":/tag_scenarios/3.40.tagroute.xml")), "the builder opens 3.40");
    CHECK(w.applyScenario(DuplicateMissing, QStringLiteral("2")) && w.route().tags.size() == 27 && w.isModified(),
          "Scenario ▸ Duplicate tag missing changes the route");
    CHECK(w.undo() && w.route().tags.size() == 28 && rowOf(w.route(), QStringLiteral("2D")) >= 0, "one Undo puts it back");
    w.selectRows({ rowOf(w.route(), QStringLiteral("2")), rowOf(w.route(), QStringLiteral("2D")) });
    CHECK(w.jumbleSelected() && w.route().tags.at(rowOf(base, QStringLiteral("2"))).name == QLatin1String("2D"), "jumble the selected rows");
    w.undo();

    QTemporaryDir tmp;
    const bool modifiedBefore = w.isModified();
    const int undoBefore = w.undoLog()->depth();
    const QStringList files = w.makeSection14(tmp.path(), QStringLiteral("2"));
    CHECK(files.size() == 16 && QFile::exists(QDir(tmp.path()).filePath(firstA + QStringLiteral(".tagroute.xml")))
              && QFile::exists(QDir(tmp.path()).filePath(firstA + QStringLiteral("_route.xml"))),
          QByteArray("8 routes, each also as tags_sim's route.xml (") + QByteArray::number(files.size()) + " files)");
    const QString missing = QDir(tmp.path()).filePath(QStringLiteral("14.%1.tagroute.xml").arg(b + 2));
    const QVector<RfidTag::Route> back = RfidTag::readFile(missing, &err);
    CHECK(back.size() == 1 && back.first().tags.size() == 27 && rowOf(back.first(), QStringLiteral("2D")) < 0
              && back.first().name.contains(QStringLiteral("duplicate tag missing")) && back.first().dir == base.dir,
          "its 14.x2 file: 2D missing, named for it, same direction");
    CHECK(w.route().tags.size() == 28 && w.isModified() == modifiedBefore && w.undoLog()->depth() == undoBefore,
          "making the set leaves the open route alone");
    QString why;
    CHECK(w.makeSection14(tmp.path(), QStringLiteral("804"), &why).isEmpty() && why.contains(QStringLiteral("duplicate")),
          "804 has no duplicate: no set, said why");
}
