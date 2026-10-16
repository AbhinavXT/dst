#include "testutil.h"

#include "rfidtag.h"
#include "tagbuilderwindow.h"

#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>

// =============================================================================
//  Session 304 — the Tag Builder shows Annexure D's field descriptions.
//
//  The tag editor's fields (label and editor) and the route table's tag-field
//  columns (Type, Abs loc, TIN nom / rev, Placement, CRC-30) carry Annexure
//  D's description for the tag's own type: an LC tag's placement mentions the
//  G-tag, a junction tag's tin_nom is TIN -1. A tooltip a cell already had
//  (a failing CRC's stored and computed values) stays, before it. The first
//  tag is a real one (session 195's, read from a reader).
// =============================================================================

namespace {

QWidget *editor(TagBuilderWindow &w, const QString &name)
{
    return w.findChild<QWidget *>(name);
}

QString labelTip(TagBuilderWindow &w, const QString &name)
{
    QWidget *field = editor(w, name);
    if (!field) return QStringLiteral("<no editor>");
    for (QFormLayout *form : w.findChildren<QFormLayout *>()) {
        if (QWidget *label = form->labelForField(field)) return label->toolTip();
    }
    return QStringLiteral("<no label>");
}

}  // namespace

TEST_SUITE(session304)
{
    TagBuilderWindow w;
    w.setTag(RfidTag::fromPages(QStringLiteral("082a2a027fece219"), QStringLiteral("f65d4fe80d010781")));
    CHECK(RfidTag::summary(w.tag()).type == 9, "a real Normal tag in the editor");

    // ---- the editor ------------------------------------------------------------------------
    QWidget *placement = editor(w, QStringLiteral("placement"));
    CHECK(placement && placement->toolTip().contains(QLatin1String("<b>Tag placement</b>"))
              && placement->toolTip().contains(QLatin1String("D 2. Normal Tag, page 5")),
          "Normal tag: placement's editor carries Annexure D 2");
    CHECK(labelTip(w, QStringLiteral("placement")) == placement->toolTip(), "and so does its label");
    CHECK(editor(w, QStringLiteral("stn_nom")) && editor(w, QStringLiteral("stn_nom"))->toolTip()
              .contains(QLatin1String("Station ID in Nominal direction")),
          "stn_nom: Station ID in Nominal direction");
    QWidget *abs = editor(w, QStringLiteral("abs_loc"));
    CHECK(abs && abs->toolTip().startsWith(QLatin1String("8388607 = not applicable"))
              && abs->toolTip().contains(QLatin1String("Absolute Loc in meters")),
          QByteArray("abs_loc keeps its own note, then the description: ") + (abs ? abs->toolTip().left(120).toUtf8() : QByteArray()));

    w.setType(10);
    placement = editor(w, QStringLiteral("placement"));
    CHECK(placement && placement->toolTip().contains(QLatin1String("G-tag"))
              && placement->toolTip().contains(QLatin1String("D 3. LC Tag")),
          "LC tag: placement is D 3's, with the G-tag");
    CHECK(editor(w, QStringLiteral("gate_id")) && editor(w, QStringLiteral("gate_id"))->toolTip().contains(QLatin1String("<b>Gate ID</b>")),
          "gate_id: Gate ID");
    w.setType(11);
    CHECK(editor(w, QStringLiteral("adj2")) && editor(w, QStringLiteral("adj2"))->toolTip().contains(QLatin1String("Adjacent Line-3 TIN")),
          "Adjacent line tag: adj2 is Adjacent Line-3 TIN");
    w.setType(12);
    CHECK(editor(w, QStringLiteral("tin_nom")) && editor(w, QStringLiteral("tin_nom"))->toolTip().contains(QLatin1String("<b>TIN -1</b>")),
          "Junction tag: tin_nom is TIN -1");
    CHECK(editor(w, QStringLiteral("dir_corr_2")) && editor(w, QStringLiteral("dir_corr_2"))->toolTip()
              .contains(QLatin1String("Direction Reset toward absolute location-2")),
          "dir_corr_2: direction correction 2");

    // ---- the route table ---------------------------------------------------------------------
    RfidTag::Route route;
    route.name = QStringLiteral("docs");
    QHash<QString, qint64> normal{ { QStringLiteral("type"), 9 }, { QStringLiteral("version"), 1 },
                                   { QStringLiteral("unique"), 101 }, { QStringLiteral("abs_loc"), 150000 },
                                   { QStringLiteral("tin_nom"), 84 }, { QStringLiteral("tin_rev"), 84 } };
    route.tags.append({ QString(), RfidTag::build(normal) });
    QHash<QString, qint64> lc{ { QStringLiteral("type"), 10 }, { QStringLiteral("version"), 1 },
                               { QStringLiteral("unique"), 102 }, { QStringLiteral("abs_loc"), 150300 },
                               { QStringLiteral("gate_id"), 19 } };
    route.tags.append({ QString(), RfidTag::build(lc) });
    QByteArray broken = RfidTag::build(normal);
    broken[15] = static_cast<char>(broken.at(15) ^ 0x01);
    route.tags.append({ QString(), broken });
    w.setRoute(route);
    QTableWidget *table = w.routeTable();
    CHECK(table && table->rowCount() == 3, "a route of three tags");
    if (table && table->rowCount() == 3) {
        CHECK(table->item(0, 8)->toolTip().contains(QLatin1String("D 2. Normal Tag"))
                  && table->item(1, 8)->toolTip().contains(QLatin1String("G-tag")),
              "Placement: each row's own tag type");
        CHECK(table->item(0, 3)->toolTip().contains(QLatin1String("The decimal value is 9"))
                  && table->item(1, 3)->toolTip().contains(QLatin1String("The decimal value is 10")),
              "Type: per row");
        CHECK(table->item(0, 4)->toolTip().contains(QLatin1String("Absolute Loc in meters"))
                  && table->item(0, 7)->toolTip().contains(QLatin1String("TIN in Reverse Direction")),
              "Abs loc and TIN nom / rev");
        CHECK(table->item(0, 2)->toolTip().contains(QLatin1String("CRC-30/CDMA")), "CRC-30: D 6");
        CHECK(table->item(2, 2)->text() == QLatin1String("FAIL")
                  && table->item(2, 2)->toolTip().startsWith(QLatin1String("Stored "))
                  && table->item(2, 2)->toolTip().contains(QLatin1String("CRC-30/CDMA")),
              "a failing CRC keeps its stored/computed values first");
        CHECK(table->item(0, 0)->toolTip().isEmpty() && table->item(0, 9)->toolTip().isEmpty(),
              "columns that are not tag fields (#, pages) have none");
    }
    w.setRoute(RfidTag::Route());   // nothing to confirm on close
}
