#include "testutil.h"
#include "layoutaudit.h"

#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "undolog.h"

#include <QTableWidget>

// =============================================================================
//  Session 200 — RFID Tag Builder: set one field on several tags at once;
//  delete several. Routes built from field values through the schema.
// =============================================================================

namespace {

QByteArray normal(int unique, qint64 loc)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 },
                            { QStringLiteral("stn_nom"), 528 },  { QStringLiteral("placement"), 1 } });
}

QByteArray adjustment(int unique, qint64 l1)
{
    return RfidTag::build({ { QStringLiteral("type"), 12 },      { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc_1"), l1 },
                            { QStringLiteral("abs_loc_2"), 5000 }, { QStringLiteral("dir_corr_1"), 1 },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 } });
}

qint64 field(const RfidTag::Route &r, int row, const char *name)
{
    return RfidTag::values(r.tags.at(row).bytes).value(QLatin1String(name), -1);
}

}  // namespace

TEST_SUITE(session200)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    TagBuilderWindow w;
    w.resize(1100, 680);
    w.show();
    RfidTag::Route r;
    r.dir = RfidTag::DirNominal;
    r.tags = { { QString(), normal(100, 1000) }, { QString(), normal(101, 1300) }, { QString(), adjustment(660, 1600) },
               { QString(), normal(102, 1900) }, { QString(), normal(103, 2200) } };
    w.setRoute(r);

    // ---- selection ----------------------------------------------------------------------------
    w.selectRows({ 3, 0, 2 });
    CHECK(w.selectedRows() == QList<int>({ 0, 2, 3 }), "several rows can be selected; listed in order");

    // ---- the fields in common -----------------------------------------------------------------
    QStringList common;
    for (const RfidTag::Field &f : w.commonFields({ 0, 2, 3 })) common << f.name;
    CHECK(common.contains(QLatin1String("tin_nom")) && common.contains(QLatin1String("comm_nom"))
              && !common.contains(QLatin1String("abs_loc")) && !common.contains(QLatin1String("stn_nom"))
              && !common.contains(QLatin1String("abs_loc_2")),
          QByteArray("Normal and adjustment tags together: only the fields both carry: ") + common.join(QLatin1Char(' ')).toUtf8());

    // ---- set one field on several -------------------------------------------------------------
    const int depth = w.undoLog()->depth();
    CHECK(w.setFieldOn({ 0, 2, 3 }, QStringLiteral("tin_nom"), 86), "tin_nom = 86 on three tags");
    const RfidTag::Route &a = w.route();
    bool crcs = true;
    for (const RfidTag::Tag &t : a.tags) crcs = crcs && RfidTag::summary(t.bytes).crcOk;
    CHECK(field(a, 0, "tin_nom") == 86 && field(a, 2, "tin_nom") == 86 && field(a, 3, "tin_nom") == 86
              && field(a, 1, "tin_nom") == 84 && crcs,
          "the three selected have it, the other does not, and every CRC passes");
    CHECK(field(a, 0, "abs_loc") == 1000 && field(a, 2, "abs_loc_1") == 1600 && field(a, 2, "abs_loc_2") == 5000
              && field(a, 3, "stn_nom") == 528 && field(a, 3, "placement") == 1,
          "every other field as it was");
    CHECK(w.selectedRows() == QList<int>({ 0, 2, 3 }) && w.undoLog()->depth() == depth + 1
              && w.status()->text().contains(QLatin1String("on 3 tags")),
          "the selection stays, one Undo step, and it says what it did");

    CHECK(w.setFieldOn({ 0, 1 }, QStringLiteral("comm_nom"), 1) && field(w.route(), 1, "comm_nom") == 1,
          "a coded field (comm_nom = 1, Not required)");

    // ---- refused, whole -----------------------------------------------------------------------
    const RfidTag::Route kept = w.route();
    CHECK(!w.setFieldOn({ 0, 2, 3 }, QStringLiteral("tin_nom"), 300) && w.status()->kind() == StatusLine::Kind::Fail
              && w.status()->text().contains(QLatin1String("8 bits")) && w.route().tags.at(0).bytes == kept.tags.at(0).bytes,
          QByteArray("a value too wide for the field: nothing set, and why: ") + w.status()->text().toUtf8());
    CHECK(!w.setFieldOn({ 0, 2 }, QStringLiteral("stn_nom"), 1) && w.route().tags.at(0).bytes == kept.tags.at(0).bytes,
          "a field one of the selected tags does not carry: refused");

    // ---- undo ---------------------------------------------------------------------------------
    w.undo();
    w.undo();
    CHECK(w.route().tags.at(0).bytes == r.tags.at(0).bytes && w.route().tags.at(3).bytes == r.tags.at(3).bytes,
          "Undo, twice: back to the route as it was");

    // ---- delete several -----------------------------------------------------------------------
    w.selectRows({ 1, 3 });
    w.deleteTag();
    CHECK(w.route().tags.size() == 3 && RfidTag::nameOf(w.route().tags.at(1).bytes) == QLatin1String("660")
              && w.status()->text().contains(QLatin1String("101, 102")),
          QByteArray("Delete with two rows selected deletes both: ") + w.status()->text().toUtf8());
    w.undo();
    CHECK(w.route().tags.size() == 5, "and Undo brings both back");

    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits a laptop with the new button (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no widget outside a layout: ") + loose.join(QLatin1Char(' ')).toUtf8());
    w.setRoute(RfidTag::Route());
}
