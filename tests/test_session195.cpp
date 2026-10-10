#include "testutil.h"
#include "layoutaudit.h"

#include "capturedecoder.h"
#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <cstdio>

#include <QComboBox>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 195 — RFID Tag Builder, phase A: the tag codec, route files, the
//  window. REAL tags only: tests/fixtures/tags_sim/ holds route files from the
//  tags_sim tool's KAV_CONFIG (Hafizpet, S2S) and its output folder, and two
//  of its .xlsx routes converted by scripts/tags_sim_import.py.
// =============================================================================

namespace {

QString fx(const char *name) { return QStringLiteral(DL_SRC_DIR "/tests/fixtures/tags_sim/") + QLatin1String(name); }

// Every <rfid_data> of a file, its attributes by name, in file order.
QVector<QHash<QString, QString>> rows(const QByteArray &xml)
{
    QVector<QHash<QString, QString>> out;
    QDomDocument doc;
    doc.setContent(xml);
    const QDomNodeList l = doc.elementsByTagName(QStringLiteral("rfid_data"));
    for (int i = 0; i < l.size(); ++i) {
        const QDomNamedNodeMap a = l.at(i).attributes();
        QHash<QString, QString> h;
        for (int j = 0; j < a.size(); ++j) h.insert(a.item(j).nodeName(), a.item(j).nodeValue());
        out << h;
    }
    return out;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST_SUITE(session195)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    (void)kavachSchema();     // registers the CRCs

    // ---- 1. page_x / page_y and the schema's fields -------------------------------------------
    {
        // Tag 904 of Hafizpet DN_MAIN, as tags_sim wrote it.
        const QByteArray t = RfidTag::fromPages(QStringLiteral("082a2a027fece219"), QStringLiteral("f65d4fe80d010781"));
        CHECK(t.size() == 16 && RfidTag::pageX(t) == QLatin1String("082a2a027fece219")
                  && RfidTag::pageY(t) == QLatin1String("f65d4fe80d010781"),
              "page_x / page_y -> 16 bytes -> the same page_x / page_y");
        CHECK(quint8(t.at(0)) == 0x19 && quint8(t.at(7)) == 0x08 && quint8(t.at(8)) == 0x81,
              "page_x is tag bytes 0-7 little-endian (the type nibble is the low digit), page_y bytes 8-15");
        const QHash<QString, qint64> v = RfidTag::values(t);
        CHECK(v.value(QStringLiteral("type")) == 9 && v.value(QStringLiteral("unique")) == 904
                  && v.value(QStringLiteral("abs_loc")) == 163820 && v.value(QStringLiteral("tin_rev")) == 84
                  && !v.contains(QStringLiteral("reader_id")),
              "the schema reads it as Normal tag 904 at 163820 m, TIN 84 (the route.xml's own values)");
        const RfidInfo hand = CaptureDecoder::decodeRfid(QByteArray(1, char(1)) + t);
        CHECK(hand.unique == 904 && hand.absLoc == 163820 && hand.crcOk, "the hand decoder agrees, and the CRC-30 passes");
        CHECK(RfidTag::build(v) == t, "the fields build the same 16 bytes, CRC included");

        QHash<QString, qint64> moved = v;
        moved.insert(QStringLiteral("abs_loc"), 163900);
        const QByteArray m = RfidTag::build(moved);
        const RfidTag::Summary ms = RfidTag::summary(m);
        CHECK(ms.absLoc == 163900 && ms.crcOk && ms.crcStored != hand.crcStored,
              "moved to 163900 m: a new CRC-30 that passes");
        QString err;
        moved.insert(QStringLiteral("abs_loc"), 8388608);
        CHECK(RfidTag::build(moved, &err).isEmpty() && err.contains(QLatin1String("23 bits")),
              QByteArray("a location one bit too wide is refused, not cut short: ") + err.toUtf8());
        CHECK(RfidTag::fromPages(QStringLiteral("xyz"), QStringLiteral("1"), &err).isEmpty() && !err.isEmpty(),
              "page values that are not hex are refused");
        CHECK(RfidTag::nameOf(RfidTag::fromPages(QStringLiteral("082a2a027ff0e219"), QStringLiteral("240bee948d010781")))
                  == QLatin1String("904D"),
              "its duplicate is named 904D, as tags_sim names it");
    }

    // ---- 2. every field of every type, from the schema ----------------------------------------
    {
        QStringList names9, names10, names11, names12;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(9)) names9 << f.name;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(10)) names10 << f.name;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(11)) names11 << f.name;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(12)) names12 << f.name;
        CHECK(names9.contains(QLatin1String("stn_nom")) && names9.contains(QLatin1String("placement"))
                  && !names9.contains(QLatin1String("gate_id")) && !names9.contains(QLatin1String("type")),
              QByteArray("Normal: ") + names9.join(QLatin1Char(' ')).toUtf8());
        CHECK(names10.contains(QLatin1String("dist_to_gate")) && names10.contains(QLatin1String("whistle_type")),
              QByteArray("LC gate: ") + names10.join(QLatin1Char(' ')).toUtf8());
        CHECK(names11.contains(QLatin1String("adj4")) && !names11.contains(QLatin1String("comm_nom")),
              QByteArray("Adjacent line (no comm flags): ") + names11.join(QLatin1Char(' ')).toUtf8());
        CHECK(names12.contains(QLatin1String("abs_loc_1")) && names12.contains(QLatin1String("abs_loc_2"))
                  && names12.contains(QLatin1String("dir_corr_2")) && !names12.contains(QLatin1String("abs_loc")),
              QByteArray("Adjustment: ") + names12.join(QLatin1Char(' ')).toUtf8());

        // An LC gate tag built from scratch reads back field for field.
        QHash<QString, qint64> lc{ { QStringLiteral("type"), 10 },        { QStringLiteral("version"), 1 },
                                   { QStringLiteral("unique"), 672 },     { QStringLiteral("abs_loc"), 151000 },
                                   { QStringLiteral("tin_nom"), 86 },     { QStringLiteral("tin_rev"), 85 },
                                   { QStringLiteral("gate_id"), 41 },     { QStringLiteral("gate_alpha"), 2 },
                                   { QStringLiteral("dist_to_gate"), 600 }, { QStringLiteral("auto_whistle"), 1 } };
        const QByteArray t = RfidTag::build(lc);
        const RfidInfo r = CaptureDecoder::decodeRfid(QByteArray(1, char(1)) + t);
        CHECK(r.type == 10 && r.unique == 672 && r.gateId == 41 && r.gateAlpha == 2 && r.distGate == 600
                  && r.autoWhistle == 1 && r.crcOk,
              "an LC gate tag built from its fields: the hand decoder reads them back, CRC passes");
    }

    // The real KAV_CONFIG files are local only (git-ignored): without them
    // sections 3 and 4 are skipped, and the run says so.
    const bool haveFx = QFile::exists(fx("DN_MN904_720route.xml")) && QFile::exists(fx("S2S_Configuration1.xml"));
    if (!haveFx)
        fprintf(stderr, "  NOTE [session195] tests/fixtures/tags_sim/ is not here (kept local, not in git): "
                        "the checks against real KAV_CONFIG routes are skipped\n");

    // ---- 3. every real tag in the fixtures ----------------------------------------------------
    if (haveFx) {
        int total = 0, same = 0, rebuiltOk = 0, failing = 0, fieldsKept = 0;
        for (const char *f : { "DN_MN904_720route.xml", "UP_MN721_981route.xml", "old_flat_550_991_route.xml",
                               "S2S_DN_Single_NOM_20.8_794_N_R_plus1000m148_2route.xml", "S2S_Configuration1.xml" }) {
            for (const RfidTag::Route &route : RfidTag::readFile(fx(f))) {
                for (const RfidTag::Tag &tag : route.tags) {
                    ++total;
                    const QHash<QString, qint64> v = RfidTag::values(tag.bytes);
                    const QByteArray b = RfidTag::build(v);
                    const bool ok = RfidTag::summary(tag.bytes).crcOk;
                    failing += ok ? 0 : 1;
                    if (b == tag.bytes) ++same;
                    if (RfidTag::summary(b).crcOk) ++rebuiltOk;
                    if (RfidTag::values(b) == v) ++fieldsKept;
                }
            }
        }
        CHECK(total > 1400 && fieldsKept == total && rebuiltOk == total,
              QByteArray("every real tag (") + QByteArray::number(total) + ") rebuilds with the same fields and a CRC that passes");
        CHECK(same == total - failing && failing > 0,
              QByteArray("byte for byte, except the ") + QByteArray::number(failing)
                  + " whose stored CRC fails (those get the right one)");
    }

    // ---- 4. route files -----------------------------------------------------------------------
    {
        QString err;
        QStringList notes;
        CHECK(RfidTag::readXml("<nonsense/>", &err).isEmpty() && !err.isEmpty(), "a file that is not a route says so");
        const QVector<RfidTag::Route> partial = RfidTag::readXml(
            "<route_data route_name=\"x\" dir=\"1\"><rfid_data rfid_id=\"1\" page_x=\"zz\" page_y=\"1\"/>"
            "<rfid_data rfid_id=\"904\" page_x=\"082a2a027fece219\" page_y=\"f65d4fe80d010781\"/></route_data>",
            &err, &notes);
        CHECK(partial.value(0).tags.size() == 1 && partial.at(0).dir == 1 && notes.size() == 1
                  && notes.at(0).contains(QLatin1String("hex")),
              "a route_data block is read; a row with bad page values is skipped and named");
    }
    if (haveFx) {
        QString err;
        QStringList notes;
        const QVector<RfidTag::Route> dn = RfidTag::readFile(fx("DN_MN904_720route.xml"), &err, &notes);
        CHECK(dn.size() == 2 && dn.at(0).name == QLatin1String("DN_904_720") && dn.at(0).dir == 1
                  && dn.at(1).name == QLatin1String("REV_DN_904_720") && dn.at(1).dir == 2 && dn.at(0).tags.size() == 48
                  && notes.isEmpty(),
              "a tags_sim route.xml: the route and its REV route, 48 tags, names and directions");

        const QVector<RfidTag::Route> flat = RfidTag::readFile(fx("old_flat_550_991_route.xml"));
        int bad = 0;
        for (const RfidTag::Tag &t : flat.value(0).tags) bad += RfidTag::summary(t.bytes).crcOk ? 0 : 1;
        CHECK(flat.size() == 1 && flat.at(0).name == QLatin1String("old_flat_550_991_route") && flat.at(0).dir == 0
                  && flat.at(0).tags.size() == 36,
              "the older flat route.xml: one route, named after the file, direction not set");
        CHECK(flat.at(0).tags.value(0).name == QLatin1String("550") && !RfidTag::summary(flat.at(0).tags.at(0).bytes).crcOk
                  && bad > 0,
              QByteArray("tag 550 of tags_sim's own output fails its CRC-30 (") + QByteArray::number(bad) + " in that file)");

        const QVector<RfidTag::Route> conf = RfidTag::readFile(fx("S2S_Configuration1.xml"));
        int confTags = 0;
        for (const RfidTag::Route &r : conf) confTags += r.tags.size();
        CHECK(conf.size() == 20 && confTags == 1208, QByteArray("a Configuration1.xml: ") + QByteArray::number(conf.size())
                                                         + " routes, all 1208 tags");

        const QVector<RfidTag::Route> conv = RfidTag::readFile(fx("DN_MAIN.tagroute.xml"));
        CHECK(conv.size() == 1 && conv.at(0).name == QLatin1String("DN_MAIN") && conv.at(0).tags.size() == 42
                  && conv.at(0).signalList.size() == 9 && conv.at(0).signalList.at(0).footTag == QLatin1String("904")
                  && conv.at(0).signalList.at(0).name == QLatin1String("S1637_H"),
              "a tags_sim .xlsx converted by scripts/tags_sim_import.py: 42 tags and its 9 signals");

        RfidTag::Route r = conv.at(0);
        r.dir = RfidTag::DirNominal;
        const QVector<RfidTag::Route> back = RfidTag::readXml(RfidTag::toTagRouteXml(r));
        CHECK(back.size() == 1 && back.at(0).name == r.name && back.at(0).dir == 1 && back.at(0).tags.size() == 42
                  && back.at(0).tags.at(5).bytes == r.tags.at(5).bytes && back.at(0).signalList.size() == 9
                  && back.at(0).signalList.at(8).sigId == r.signalList.at(8).sigId,
              "a DLConsole route file saves and reads back the same");
        CHECK(RfidTag::toTagRouteXml(r) == RfidTag::toTagRouteXml(back.at(0)), "and writes the same bytes again");

        // The route.xml export, against the one tags_sim wrote for the same tags.
        RfidTag::Route fromFile = dn.at(0);
        fromFile.dir = RfidTag::DirUnset;
        CHECK(RfidTag::toRouteXml(fromFile, &err).isEmpty() && err.contains(QLatin1String("direction")),
              "route.xml is refused while the direction is not set");
        fromFile.dir = RfidTag::DirNominal;
        const QByteArray ours = RfidTag::toRouteXml(fromFile);
        const QVector<QHash<QString, QString>> mine = rows(ours), theirs = rows(readAll(fx("DN_MN904_720route.xml")));
        CHECK(mine.size() == 96 && mine == theirs,
              QByteArray("the exported route.xml has tags_sim's rows, attribute for attribute, both directions (")
                  + QByteArray::number(mine.size()) + " rows)");
        const QVector<RfidTag::Route> exported = RfidTag::readXml(ours);
        CHECK(exported.size() == 2 && exported.at(0).name == QLatin1String("DN_904_720")
                  && exported.at(1).name == QLatin1String("REV_DN_904_720") && exported.at(1).dir == 2,
              "named as tags_sim names them");

        const QVector<RfidTag::Route> up = RfidTag::readFile(fx("UP_MN721_981route.xml"));
        const QVector<QHash<QString, QString>> upMine = rows(RfidTag::toRouteXml(up.at(0)));
        CHECK(!upMine.isEmpty() && upMine == rows(readAll(fx("UP_MN721_981route.xml"))),
              "and an UP route the same way");
    }

    // ---- 5. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w;
        w.resize(1100, 640);
        w.show();
        CHECK(RfidTag::summary(w.tag()).type == 9 && RfidTag::summary(w.tag()).crcOk, "opens on a Normal tag with a valid CRC");

        w.setPages(QStringLiteral("082a2a027fece219"), QStringLiteral("f65d4fe80d010781"));
        auto *abs = w.findChild<QSpinBox *>(QStringLiteral("abs_loc"));
        CHECK(abs && abs->value() == 163820 && RfidTag::nameOf(w.tag()) == QLatin1String("904"),
              "pasting page values fills the fields");
        CHECK(w.setField(QStringLiteral("abs_loc"), 170000), "a field can be set");
        CHECK(RfidTag::summary(w.tag()).absLoc == 170000 && RfidTag::summary(w.tag()).crcOk
                  && w.pageXEdit()->text() == RfidTag::pageX(w.tag()) && w.crcLabel()->text().contains(QLatin1String("matches")),
              "editing a field rebuilds page_x / page_y with a CRC that matches");
        auto *place = w.findChild<QComboBox *>(QStringLiteral("placement"));
        CHECK(place && place->count() == 16 && place->itemText(1) == QLatin1String("1 (Signal foot (Nominal))")
                  && place->itemText(12) == QLatin1String("12 (reserved)"),
              QByteArray("a coded field is a choice of every value, by name: ") + (place ? place->itemText(1).toUtf8() + " / " + place->itemText(12).toUtf8() + " / " + QByteArray::number(place->count()) : QByteArray()));

        w.setPages(QStringLiteral("faab2b0250d18999"), QStringLiteral("90acfa340a00fa80"));   // tag 550, CRC fails
        CHECK(w.crcLabel()->text().contains(QLatin1String("would not process")), "a tag whose CRC fails says what a loco would do");

        w.setType(12);
        CHECK(w.findChild<QSpinBox *>(QStringLiteral("abs_loc_2")) && RfidTag::summary(w.tag()).type == 12
                  && RfidTag::summary(w.tag()).crcOk && RfidTag::summary(w.tag()).unique == 550,
              "changing the type keeps the shared fields and shows the new type's");

        // A route of its own: 24 Normal tags 300 m apart, each with its
        // duplicate 4 m on, then tag 550 as tags_sim wrote it (CRC fails).
        QTemporaryDir dir;
        RfidTag::Route made;
        made.name = QStringLiteral("made");
        for (int i = 0; i < 24; ++i) {
            QHash<QString, qint64> v{ { QStringLiteral("type"), 9 },   { QStringLiteral("version"), 1 },
                                      { QStringLiteral("unique"), 100 + i }, { QStringLiteral("abs_loc"), 150000 + 300 * i },
                                      { QStringLiteral("tin_nom"), 84 }, { QStringLiteral("tin_rev"), 84 } };
            made.tags.append({ QString(), RfidTag::build(v) });
            v.insert(QStringLiteral("duplication"), 1);
            v.insert(QStringLiteral("abs_loc"), 150004 + 300 * i);
            made.tags.append({ QString(), RfidTag::build(v) });
        }
        const QString madePath = dir.filePath(QStringLiteral("made.tagroute.xml"));
        RfidTag::writeFile(madePath, RfidTag::toTagRouteXml(made));
        CHECK(w.loadFile(madePath) && w.routeTable()->rowCount() == 48
                  && w.routeTable()->item(0, 1)->text() == QLatin1String("100")
                  && RfidTag::nameOf(w.tag()) == QLatin1String("100") && w.status()->kind() == StatusLine::Kind::Ok,
              "opening a route lists its tags and edits the first");
        w.selectRow(2);
        w.setField(QStringLiteral("abs_loc"), 150611);
        w.replaceTag();
        CHECK(RfidTag::summary(w.route().tags.at(2).bytes).absLoc == 150611 && w.isModified()
                  && w.windowTitle().endsWith(QLatin1String("*")),
              "Replace puts the edited tag in its row; the route is marked changed");
        w.addTag();
        CHECK(w.route().tags.size() == 49 && w.route().tags.at(3).bytes == w.route().tags.at(2).bytes,
              "Add puts the editor's tag after the selected row");
        w.moveTag(-1);
        w.deleteTag();
        CHECK(w.route().tags.size() == 48, "Up and Delete");
        CHECK(!w.exportRouteXml(dir.filePath(QStringLiteral("x.xml"))) && w.status()->kind() == StatusLine::Kind::Fail,
              "export refused with no direction set, and says why");
        made = w.route();
        made.dir = RfidTag::DirNominal;
        w.setRoute(made);
        const QString saved = dir.filePath(QStringLiteral("r.tagroute.xml"));
        CHECK(w.saveFile(saved) && !w.isModified() && RfidTag::readFile(saved).value(0).tags.size() == 48,
              "Save writes the route; it is no longer marked changed");
        CHECK(w.exportRouteXml(dir.filePath(QStringLiteral("route.xml")))
                  && rows(readAll(dir.filePath(QStringLiteral("route.xml")))).size() == 96,
              "Export writes tags_sim's route.xml");

        made.tags.append({ QStringLiteral("550"), RfidTag::fromPages(QStringLiteral("faab2b0250d18999"), QStringLiteral("90acfa340a00fa80")) });
        RfidTag::writeFile(madePath, RfidTag::toTagRouteXml(made));
        CHECK(w.loadFile(madePath) && w.status()->kind() == StatusLine::Kind::Warn
                  && w.status()->text().contains(QLatin1String("1 tag fails its CRC-30 (a loco would not process it)")),
              QByteArray("opening a route with a failing tag warns: ") + w.status()->text().toUtf8());
        CHECK(w.routeTable()->item(48, 2)->text() == QLatin1String("FAIL") && w.routeTable()->item(47, 2)->text() == QLatin1String("pass"),
              "and marks it in the table");

        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no widget outside a layout: ") + loose.join(QLatin1Char(' ')).toUtf8());
        w.setRoute(RfidTag::Route());   // nothing to confirm on close
    }
}
