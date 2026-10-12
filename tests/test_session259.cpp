#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QSet>
#include <QTemporaryDir>

// =============================================================================
//  Session 259 — the Tag Builder.
//  1. A route for Configuration1.xml: Export ▸ Route for Configuration1.xml…
//     writes just the route's two <route_data> blocks (route + REV), to paste
//     inside a Configuration1.xml's <route>. Real files, in git:
//     tag_scenarios/Hafizpet (its Configuration1.xml and DN_MAIN route).
//  2. The tag form listed duplication, comm_nom, comm_rev, sect_* once per
//     tag type declared in the schema (four duplication rows on a Normal
//     tag). RfidTag::fieldsOf() now keeps only the type's own declaration.
// =============================================================================

namespace {
QString fx259(const QString &name) { return QStringLiteral(DL_SRC_DIR "/tag_scenarios/Hafizpet/") + name; }
QByteArray readAll259(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST_SUITE(session259)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 2. one row per field ----------------------------------------------------------------
    for (int type : { 9, 10, 11, 12 }) {
        const QVector<RfidTag::Field> fields = RfidTag::fieldsOf(type);
        QSet<QString> names;
        int dup = 0;
        for (const RfidTag::Field &f : fields) {
            names.insert(f.name);
            dup += f.name == QLatin1String("duplication") ? 1 : 0;
        }
        CHECK(!fields.isEmpty() && names.size() == fields.size() && dup == 1,
              QByteArray("type ") + QByteArray::number(type) + ": every field once, duplication once ("
                  + QByteArray::number(fields.size()) + " fields, " + QByteArray::number(names.size()) + " names)");
    }
    {
        // Each type's fields in its own wire order: a Junction tag's abs_loc_2
        // before its sections (in the Normal tag's block they come first).
        QStringList t12;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(12)) t12 << f.name;
        CHECK(t12.indexOf(QStringLiteral("abs_loc_2")) >= 0
                  && t12.indexOf(QStringLiteral("abs_loc_2")) < t12.indexOf(QStringLiteral("sect_nom"))
                  && t12.indexOf(QStringLiteral("sect_nom")) < t12.indexOf(QStringLiteral("duplication")),
              QByteArray("type 12 in its wire order: ") + t12.join(QLatin1Char(' ')).toUtf8());
        QStringList t11;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(11)) t11 << f.name;
        CHECK(!t11.contains(QStringLiteral("comm_nom")) && !t11.contains(QStringLiteral("sect_nom")),
              "type 11 (adjacent line) has no comm or section fields");
        // Every field still round-trips: a tag built from the form's fields
        // reads back the same values (the encoder is unchanged).
        QHash<QString, qint64> v;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(9)) v.insert(f.name, f.name == QLatin1String("duplication") ? 1 : 0);
        v.insert(QStringLiteral("unique"), 804);
        v.insert(QStringLiteral("comm_rev"), 1);
        QString err;
        v.insert(QStringLiteral("type"), 9);
        const QByteArray tag = RfidTag::build(v, &err);
        const QHash<QString, qint64> back = RfidTag::values(tag);
        CHECK(!tag.isEmpty() && back.value(QStringLiteral("unique")) == 804 && back.value(QStringLiteral("duplication")) == 1
                  && back.value(QStringLiteral("comm_rev")) == 1,
              QByteArray("a Normal tag built from the form's fields reads back: ") + err.toUtf8());
    }

    // ---- 1. a route for Configuration1.xml ---------------------------------------------------
    const QByteArray conf = readAll259(fx259(QStringLiteral("Configuration1.xml")));
    const RfidTag::Route s2s = RfidTag::readFile(fx259(QStringLiteral("DN_MAIN.tagroute.xml"))).value(0);
    CHECK(!conf.isEmpty() && !s2s.tags.isEmpty() && s2s.dir == RfidTag::DirNominal,
          "fixtures: Hafizpet's Configuration1.xml and DN_MAIN route");
    QString err;
    const QByteArray blocks = RfidExport::configurationRoute(s2s, &err);
    const QString text = QString::fromUtf8(blocks);
    CHECK(text.startsWith(QLatin1String("<route_data ")) && !text.contains(QLatin1String("<?xml"))
              && !text.contains(QLatin1String("<route>")) && text.count(QStringLiteral("<route_data ")) == 2
              && text.count(QStringLiteral("</route_data>")) == 2,
          QByteArray("two <route_data> blocks, no declaration or <route> wrapper: ") + err.toUtf8());
    CHECK(text.contains(QLatin1String("\n    <rfid_data ")) && !text.contains(QLatin1String("\n        <rfid_data")),
          "indented as Configuration1.xml's blocks: route_data at the margin, rfid_data 4 in");
    {
        // Pasted by hand before the real file's </route>: the same routes as
        // Export ▸ Into Configuration1.xml gives, every other route kept.
        QByteArray pasted = conf;
        const int at = pasted.lastIndexOf("</route>");
        CHECK(at > 0, "fixture: the Configuration1.xml has a </route>");
        pasted.insert(at, blocks);
        const QVector<RfidTag::Route> byHand = RfidTag::readXml(pasted);
        const QVector<RfidTag::Route> merged = RfidTag::readXml(RfidExport::mergeIntoConfiguration(conf, s2s));
        bool same = byHand.size() == merged.size() && byHand.size() == RfidTag::readXml(conf).size() + 2;
        for (int i = 0; same && i < merged.size(); ++i) {
            bool found = false;
            for (const RfidTag::Route &h : byHand) {
                if (h.name != merged.at(i).name || h.tags.size() != merged.at(i).tags.size()) continue;
                bool rows = true;
                for (int t = 0; rows && t < h.tags.size(); ++t) rows = h.tags.at(t).bytes == merged.at(i).tags.at(t).bytes;
                found = found || rows;
            }
            same = found;
        }
        CHECK(same, QByteArray("pasted into the real Hafizpet Configuration1.xml: the same ") + QByteArray::number(merged.size())
                        + " routes as Into Configuration1.xml gives (" + QByteArray::number(byHand.size()) + ")");
    }
    {
        RfidTag::Route noDir = s2s;
        noDir.dir = 0;
        CHECK(RfidExport::configurationRoute(noDir, &err).isEmpty() && err.contains(QLatin1String("direction")),
              "refused without a direction (the REV route depends on it)");
    }
    // The window: the export, and a form with one row per field.
    {
        TagBuilderWindow w;
        w.setRoute(s2s);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("route.xml"));
        CHECK(w.exportConfigurationRoute(path) && readAll259(path) == blocks
                  && w.status()->text().contains(QLatin1String("<route_data>")),
              QByteArray("Export ▸ Route for Configuration1.xml writes the blocks: ") + w.status()->text().toUtf8());
        QSet<QString> labels;
        int rows = 0;
        for (QFormLayout *form : w.findChildren<QFormLayout *>()) {
            for (int r = 0; r < form->rowCount(); ++r) {
                QLayoutItem *li = form->itemAt(r, QFormLayout::LabelRole);
                auto *l = li ? qobject_cast<QLabel *>(li->widget()) : nullptr;
                if (!l || !l->text().contains(QLatin1String(" bit"))) continue;
                ++rows;
                labels.insert(l->text());
            }
        }
        CHECK(rows > 0 && labels.size() == rows,
              QByteArray("the tag form: one row per field (") + QByteArray::number(rows) + " rows, "
                  + QByteArray::number(labels.size()) + " distinct)");
    }
}
