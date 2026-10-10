#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <cstdio>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 197 — RFID Tag Builder, phase C: adjustment tags in route.xml,
//  Configuration1.xml, tags_sim's text files.
//
//  Built routes (from field values through the schema) run everywhere. The
//  ground truth is local and git-ignored (tests/fixtures/tags_sim/): tags_sim's
//  six route.xml files that hold adjustment tags (adjust/), and two routes with
//  the text files tags_sim wrote for them (text/). Without them those checks
//  are skipped and the run says so.
// =============================================================================

namespace {

QString fx(const QString &name) { return QStringLiteral(DL_SRC_DIR "/tests/fixtures/tags_sim/") + name; }

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray normal(int unique, qint64 loc, int placement = 0, bool dup = false)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 },
                            { QStringLiteral("placement"), placement }, { QStringLiteral("duplication"), dup ? 1 : 0 } });
}

QByteArray adjust(int unique, qint64 l1, qint64 l2, int dc1, int dc2)
{
    return RfidTag::build({ { QStringLiteral("type"), 12 },      { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc_1"), l1 },
                            { QStringLiteral("abs_loc_2"), l2 }, { QStringLiteral("dir_corr_1"), dc1 },
                            { QStringLiteral("dir_corr_2"), dc2 }, { QStringLiteral("tin_nom"), 84 },
                            { QStringLiteral("tin_rev"), 84 } });
}

RfidTag::Route route(int dir, const QVector<QByteArray> &tags)
{
    RfidTag::Route r;
    r.name = QStringLiteral("r");
    r.dir = dir;
    for (const QByteArray &t : tags) r.tags.append({ QString(), t });
    return r;
}

// "loc>next" for each row, for a compact comparison.
QString locs(const QVector<RfidTag::RouteRow> &rows)
{
    QStringList out;
    for (const RfidTag::RouteRow &r : rows) out << QStringLiteral("%1>%2").arg(r.absLoc).arg(r.nextAbsLoc);
    return out.join(QLatin1Char(' '));
}

// Every <rfid_data>'s attributes, in file order.
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

QStringList lines(const QByteArray &text)
{
    QStringList out;
    for (const QString &l : QString::fromUtf8(text).split(QLatin1Char('\n')))
        if (!l.trimmed().isEmpty()) out << l.trimmed();
    return out;
}

}  // namespace

TEST_SUITE(session197)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const int N = RfidTag::DirNominal, R = RfidTag::DirReverse;

    // ---- 1. adjustment tags in route.xml ------------------------------------------------------
    {
        // Nominal, N->N: the tags after the adjustment tag read 5300 and 5600 in
        // the numbering it corrects to; route.xml writes them on from 1600.
        const RfidTag::Route nn = route(N, { normal(100, 1000), normal(101, 1300), adjust(660, 1600, 5000, 1, 0),
                                             normal(102, 5300), normal(103, 5600) });
        const QVector<RfidTag::RouteRow> a = RfidTag::routeRows(nn.tags, N);
        CHECK(locs(a) == QLatin1String("1000>1300 1300>1600 1600>1900 1900>2200 2200>2200") && a.at(3).ownLoc == 5300,
              QByteArray("N->N: written as one continuous line: ") + locs(a).toUtf8());

        // N->R: the numbering after it runs the other way.
        const RfidTag::Route nr = route(N, { normal(100, 1000), normal(101, 1300), adjust(660, 1600, 5000, 2, 0),
                                             normal(102, 4700), normal(103, 4400) });
        CHECK(locs(RfidTag::routeRows(nr.tags, N)) == QLatin1String("1000>1300 1300>1600 1600>1900 1900>2200 2200>2200"),
              QByteArray("N->R: mirrored back onto the same line: ") + locs(RfidTag::routeRows(nr.tags, N)).toUtf8());

        // R->N in nominal has no rule: the tag stays put, the tags after it are
        // not corrected, and the row says why.
        const QVector<RfidTag::RouteRow> none = RfidTag::routeRows(
            route(N, { normal(100, 1000), adjust(660, 1600, 5000, 3, 0), normal(102, 5300) }).tags, N);
        CHECK(locs(none) == QLatin1String("1000>1600 1600>5300 5300>5300") && none.at(1).adjustNote.contains(QLatin1String("dir_corr_1 = 3")),
              QByteArray("no rule for this direction: own locations, and a note (tags_sim wrote -1): ") + locs(none).toUtf8());

        // Reverse, R->R (dir_corr_2 = 4): from location-2 back to location-1.
        const QVector<RfidTag::RouteRow> rr = RfidTag::routeRows(
            route(R, { normal(103, 2000), normal(102, 1700), adjust(660, 5000, 1400, 0, 4), normal(101, 4700) }).tags, R);
        CHECK(locs(rr) == QLatin1String("2000>1700 1700>5000 1400>1100 1100>1100"),
              QByteArray("reverse R->R: ") + locs(rr).toUtf8());

        // A duplicate keeps its own location (tags_sim adjusts main tags only).
        const QVector<RfidTag::RouteRow> d = RfidTag::routeRows(
            route(N, { normal(100, 1000), adjust(660, 1600, 5000, 1, 0), normal(102, 5300), normal(102, 5304, 0, true) }).tags, N);
        CHECK(locs(d) == QLatin1String("1000>1600 1600>1900 1900>5304 5304>5304"),
              QByteArray("a duplicate after it keeps its own location: ") + locs(d).toUtf8());

        // route.xml: the REV route is the same rows backwards.
        const QVector<QHash<QString, QString>> x = rows(RfidTag::toRouteXml(nn));
        CHECK(x.size() == 10 && x.at(3).value(QStringLiteral("abs_loc")) == QLatin1String("1900")
                  && x.at(5).value(QStringLiteral("rfid_id")) == QLatin1String("103")
                  && x.at(5).value(QStringLiteral("abs_loc")) == QLatin1String("2200")
                  && x.at(5).value(QStringLiteral("next_rfid_abs_loc")) == QLatin1String("1900")
                  && x.at(9).value(QStringLiteral("next_rfid_abs_loc")) == QLatin1String("1000"),
              "route.xml: adjusted rows, and the REV route the same locations backwards");
    }

    // ---- 2. tags_sim's text files -------------------------------------------------------------
    {
        RfidTag::Route r = route(N, { normal(100, 1000, 1), normal(100, 1004, 1, true), normal(101, 1300), normal(102, 1600, 1) });
        r.signalList = { { QStringLiteral("100"), QStringLiteral("S1"), QStringLiteral("4") },
                         { QStringLiteral("102"), QStringLiteral("S2"), QStringLiteral("5") } };
        const RfidExport::TextFiles t = RfidExport::textFiles(r);
        CHECK(t.stem == QLatin1String("100_102_"), "named <first>_<last>_");
        CHECK(t.rfid == QStringList({ QStringLiteral("~100,84,2,1000,4,1,1,0,5^^"), QStringLiteral("~101,84,1,1300,4,1,1,0,5^^"),
                                      QStringLiteral("~102,84,2,1600,5,1,1,0,#^^") }),
              QByteArray("rfid.txt: main tags only, entry and exit signals: ") + t.rfid.join(QLatin1Char(' ')).toUtf8());
        CHECK(t.sigId == QStringList({ QStringLiteral("~S1,4,1000,100,#^^"), QStringLiteral("~S2,5,1600,102,#^^") }),
              QByteArray("sigID.txt: ") + t.sigId.join(QLatin1Char(' ')).toUtf8());
        CHECK(t.tagLinkInfo == QStringList({ QStringLiteral("4,5 ,#,0,#,0,#,0,#,0,#, 3, 0,100,0,300,101,0,300,102,0"),
                                             QStringLiteral("sum dist:600 b/w signals with tags: 100, 101, 102") }),
              QByteArray("tag_link_info.txt: ") + t.tagLinkInfo.join(QStringLiteral(" | ")).toUtf8());
    }

    // ---- 3. into a Configuration1.xml ---------------------------------------------------------
    {
        const QByteArray config =
            "<?xml version='1.0' encoding='utf-8'?>\n<LOCO>\n    <station>\n"
            "       <station_data station_name=\"X\" station_id=\"1\" />\n"
            "       <route>\n"
            "<route_data route_name=\"KEEP\" dir=\"1\">\n"
            "  <rfid_data rfid_id=\"7\" page_x=\"082a2a027fece219\" page_y=\"f65d4fe80d010781\"/>\n"
            "</route_data>\n"
            "<route_data route_name=\"DN_100_103\" dir=\"1\">\n"
            "  <rfid_data rfid_id=\"9\" page_x=\"082a2a027fece219\" page_y=\"f65d4fe80d010781\"/>\n"
            "</route_data>\n"
            "       </route>\n   </station>\n</LOCO>\n";
        const RfidTag::Route r = route(N, { normal(100, 1000), normal(101, 1300), normal(102, 1600), normal(103, 1900) });
        QString err;
        QStringList notes;
        const QByteArray merged = RfidExport::mergeIntoConfiguration(config, r, &err, &notes);
        const QVector<RfidTag::Route> back = RfidTag::readXml(merged);
        CHECK(!merged.isEmpty() && back.size() == 3 && back.at(0).name == QLatin1String("KEEP")
                  && back.at(1).name == QLatin1String("DN_100_103") && back.at(1).tags.size() == 4
                  && back.at(2).name == QLatin1String("REV_DN_100_103"),
              QByteArray("the route's block replaced in place, its REV route added after the last: ") + err.toUtf8());
        CHECK(notes == QStringList({ QStringLiteral("replaced route DN_100_103"), QStringLiteral("added route REV_DN_100_103") }),
              QByteArray("and it says which: ") + notes.join(QStringLiteral(", ")).toUtf8());
        const int cut = config.indexOf("<route_data route_name=\"DN_100_103\"");
        const int tail = config.size() - config.indexOf("\n       </route>");
        CHECK(merged.left(cut) == config.left(cut) && merged.right(tail) == config.right(tail),
              "everything before and after the routes it touched is byte for byte as it was");
        CHECK(RfidExport::mergeIntoConfiguration(merged, r) == merged, "merging the same route again changes nothing");
        CHECK(RfidExport::mergeIntoConfiguration("<LOCO/>", r, &err).isEmpty() && !err.isEmpty(),
              "a file with no routes is refused");
        RfidTag::Route noDir = r;
        noDir.dir = RfidTag::DirUnset;
        CHECK(RfidExport::mergeIntoConfiguration(config, noDir, &err).isEmpty() && err.contains(QLatin1String("direction")),
              "and so is a route with no direction");
    }

    // ---- 4. against tags_sim's own files (local) ----------------------------------------------
    const bool haveFx = QFile::exists(fx(QStringLiteral("adjust/3.40804_908route.xml")))
                        && QFile::exists(fx(QStringLiteral("text/550_991_rfid.txt")));
    if (!haveFx) {
        fprintf(stderr, "  NOTE [session197] tests/fixtures/tags_sim/adjust and text/ are not here (kept local, not in "
                        "git): the checks against tags_sim's own route.xml and text files are skipped\n");
    } else {
        int files = 0, same = 0, total = 0;
        for (const QString &f : QDir(fx(QStringLiteral("adjust"))).entryList({ QStringLiteral("*.xml") })) {
            const QByteArray theirs = readAll(fx(QStringLiteral("adjust/") + f));
            RfidTag::Route r = RfidTag::readXml(theirs).value(0);
            ++files;
            const QVector<QHash<QString, QString>> a = rows(RfidTag::toRouteXml(r)), b = rows(theirs);
            total += b.size();
            for (int i = 0; i < qMin(a.size(), b.size()); ++i) same += a.at(i) == b.at(i) ? 1 : 0;
        }
        CHECK(files == 6 && same == total && total > 300,
              QByteArray("tags_sim's six route.xml files with adjustment tags, rebuilt from their tags: ")
                  + QByteArray::number(same) + " of " + QByteArray::number(total) + " rows the same, attribute for attribute");

        const RfidExport::TextFiles dn = RfidExport::textFiles(RfidTag::readFile(fx(QStringLiteral("text/SingleDN20.9.tagroute.xml"))).value(0));
        CHECK(dn.stem == QLatin1String("550_991_") && dn.rfid == lines(readAll(fx(QStringLiteral("text/550_991_rfid.txt"))))
                  && dn.sigId == lines(readAll(fx(QStringLiteral("text/550_991_sigID.txt"))))
                  && dn.tagLinkInfo == lines(readAll(fx(QStringLiteral("text/550_991_tag_link_info.txt")))),
              "SingleDN20.9: all three text files line for line as tags_sim wrote them");
        const RfidExport::TextFiles up = RfidExport::textFiles(RfidTag::readFile(fx(QStringLiteral("text/UpSingle20.9.tagroute.xml"))).value(0));
        const QStringList upTli = lines(readAll(fx(QStringLiteral("text/991_175_tag_link_info.txt"))));
        CHECK(up.stem == QLatin1String("991_175_") && up.rfid == lines(readAll(fx(QStringLiteral("text/991_175_rfid.txt"))))
                  && up.sigId == lines(readAll(fx(QStringLiteral("text/991_175_sigID.txt"))))
                  && up.tagLinkInfo.mid(0, 5) == upTli.mid(0, 5) && up.tagLinkInfo.size() == 12 && upTli.size() == 10,
              "UpSingle20.9: rfid and sigID line for line; tag_link_info's five stretches the same, and one more "
              "(the file on disk predates tags_sim's last version)");

        // Into the real S2S Configuration1.xml: everything outside the two blocks untouched.
        const QByteArray conf = readAll(fx(QStringLiteral("S2S_Configuration1.xml")));
        RfidTag::Route s2s = RfidTag::readXml(readAll(fx(QStringLiteral("adjust/S2S_DN_Single_NOM_20.8_794_N_R_plus1000m148_2route.xml")))).value(0);
        QStringList notes;
        QString err;
        const QByteArray merged = RfidExport::mergeIntoConfiguration(conf, s2s, &err, &notes);
        CHECK(!merged.isEmpty() && RfidTag::readXml(merged).size() == RfidTag::readXml(conf).size() + 2
                  && merged.startsWith(conf.left(conf.lastIndexOf("</route_data>") + 13))
                  && merged.endsWith(conf.mid(conf.lastIndexOf("</route_data>") + 13)),
              QByteArray("the real S2S Configuration1.xml: two routes added after its last, the rest as it was: ")
                  + notes.join(QStringLiteral(", ")).toUtf8() + err.toUtf8());
    }

    // ---- 5. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w;
        w.show();
        RfidTag::Route r = route(N, { normal(100, 1000, 1), normal(101, 1300), adjust(660, 1600, 5000, 1, 0),
                                      normal(102, 5300, 1), normal(103, 5600) });
        r.signalList = { { QStringLiteral("100"), QStringLiteral("S1"), QStringLiteral("4") },
                         { QStringLiteral("102"), QStringLiteral("S2"), QStringLiteral("5") } };
        w.setRoute(r);
        CHECK(w.routeTable()->item(3, 5)->text() == QLatin1String("1900") && w.routeTable()->item(1, 5)->text().isEmpty(),
              "the route.xml loc column: where route.xml puts a tag after the adjustment tag, blank elsewhere");

        QTemporaryDir dir;
        CHECK(w.exportTextFiles(dir.path()) && QFile::exists(dir.filePath(QStringLiteral("100_103_rfid.txt")))
                  && lines(readAll(dir.filePath(QStringLiteral("100_103_sigID.txt")))).size() == 2,
              "Export ▸ Text files writes the three files");
        const QString conf = dir.filePath(QStringLiteral("Configuration1.xml"));
        QFile f(conf);
        f.open(QIODevice::WriteOnly);
        f.write("<LOCO><route><route_data route_name=\"A\" dir=\"1\"></route_data></route></LOCO>");
        f.close();
        CHECK(w.exportIntoConfiguration(conf, conf) && RfidTag::readFile(conf).size() == 3
                  && w.status()->text().contains(QLatin1String("added route DN_100_103")),
              QByteArray("Export ▸ Into Configuration1.xml: ") + w.status()->text().toUtf8());
        w.setRoute(RfidTag::Route());
    }
}
