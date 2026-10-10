#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "undolog.h"

#include <cstdio>

#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 199 — RFID Tag Builder: fix the CRCs, of a route and of a whole
//  Configuration1.xml / route.xml.
//
//  Tag 550 is tags_sim's own (output/550_991_route.xml, CRC fails). With the
//  local, git-ignored S2S Configuration1.xml (601 of 1208 tags failing) the
//  whole-file fix is checked on it too.
// =============================================================================

namespace {

quint64 yOf(const QByteArray &tag)
{
    quint64 y = 0;
    for (int i = 0; i < 8; ++i) y |= quint64(quint8(tag.at(8 + i))) << (8 * i);
    return y;
}

const quint64 kDataBitsOfY = (quint64(1) << 34) - 1;

QByteArray normal(int unique, qint64 loc)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 } });
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST_SUITE(session199)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 1. one tag ---------------------------------------------------------------------------
    const QByteArray t550 = RfidTag::fromPages(QStringLiteral("faab2b0250d18999"), QStringLiteral("90acfa340a00fa80"));
    {
        const QByteArray fixed = RfidTag::fixCrc(t550);
        CHECK(!RfidTag::summary(t550).crcOk && RfidTag::summary(fixed).crcOk, "tags_sim's tag 550: its CRC-30 fails; fixed, it passes");
        CHECK(RfidTag::pageX(fixed) == RfidTag::pageX(t550) && (yOf(fixed) & kDataBitsOfY) == (yOf(t550) & kDataBitsOfY)
                  && RfidTag::values(fixed) == RfidTag::values(t550),
              "only the CRC bits changed: page_x, the 34 data bits of page_y, every field the same");

        const QByteArray good = normal(100, 1000);
        QByteArray bad = good;
        bad[15] = char(bad.at(15) ^ 0x40);                 // a CRC bit
        CHECK(!RfidTag::summary(bad).crcOk && RfidTag::fixCrc(bad) == good, "a damaged CRC comes back exactly");
        CHECK(RfidTag::fixCrc(good) == good, "a tag whose CRC passes is left alone");
    }

    // ---- 2. a whole file ----------------------------------------------------------------------
    {
        const QString px550 = QStringLiteral("faab2b0250d18999"), py550 = QStringLiteral("90acfa340a00fa80");
        const QByteArray goodTag = normal(100, 1000);
        const QString config = QStringLiteral(
            "<?xml version='1.0' encoding='utf-8'?>\n<LOCO>\n   <station>\n       <route>\n"
            "<route_data route_name=\"ONE\" dir=\"1\">\n"
            "        <rfid_data rfid_id=\"550\" tag_name=\"main\" page_x=\"%1\" page_y=\"%2\"/>\n"
            "        <rfid_data rfid_id=\"100\"  page_x=\"%3\" page_y=\"%4\" />\n"
            "</route_data>\n"
            "<route_data route_name=\"TWO\" dir=\"2\">\n"
            "        <rfid_data rfid_id=\"550\" page_y=\"%2\" page_x=\"%1\"/>\n"
            "</route_data>\n       </route>\n   </station>\n</LOCO>\n")
            .arg(px550, py550, RfidTag::pageX(goodTag), RfidTag::pageY(goodTag));
        QStringList changes;
        QString err;
        const QByteArray out = RfidExport::fixCrcs(config.toUtf8(), &changes, &err);
        const QString fixedY = RfidTag::pageY(RfidTag::fixCrc(t550));
        CHECK(!out.isEmpty() && changes.size() == 2
                  && changes.at(0) == QStringLiteral("ONE: 550  page_y %1 -> %2").arg(py550, fixedY)
                  && changes.at(1).startsWith(QLatin1String("TWO: 550")),
              QByteArray("both failing rows fixed, each named by route and tag: ") + changes.join(QStringLiteral(" | ")).toUtf8() + err.toUtf8());
        QString expect = config;
        expect.replace(QStringLiteral("page_y=\"%1\"").arg(py550), QStringLiteral("page_y=\"%1\"").arg(fixedY));
        CHECK(QString::fromUtf8(out) == expect, "and nothing else in the file changed, byte for byte (attribute order, spacing, the good row)");
        QStringList again;
        CHECK(RfidExport::fixCrcs(out, &again) == out && again.isEmpty(), "run again: nothing to fix, the same bytes");
        CHECK(RfidExport::fixCrcs("<LOCO/>", nullptr, &err).isEmpty() && err.contains(QLatin1String("no <rfid_data>")),
              "a file with no tag rows is refused");
    }

    // ---- 3. the real S2S Configuration1.xml (local) -------------------------------------------
    const QString s2s = QStringLiteral(DL_SRC_DIR "/tests/fixtures/tags_sim/S2S_Configuration1.xml");
    if (!QFile::exists(s2s)) {
        fprintf(stderr, "  NOTE [session199] tests/fixtures/tags_sim/ is not here (kept local, not in git): "
                        "the fix of the real S2S Configuration1.xml is skipped\n");
    } else {
        const QByteArray conf = readAll(s2s);
        QStringList changes;
        const QByteArray out = RfidExport::fixCrcs(conf, &changes);
        const QVector<RfidTag::Route> a = RfidTag::readXml(conf), b = RfidTag::readXml(out);
        int same = 0, pass = 0, total = 0;
        for (int r = 0; r < a.size() && r < b.size(); ++r)
            for (int i = 0; i < a.at(r).tags.size() && i < b.at(r).tags.size(); ++i) {
                ++total;
                const QByteArray &x = a.at(r).tags.at(i).bytes, &y = b.at(r).tags.at(i).bytes;
                same += RfidTag::pageX(x) == RfidTag::pageX(y) && RfidTag::values(x) == RfidTag::values(y) ? 1 : 0;
                pass += RfidTag::summary(y).crcOk ? 1 : 0;
            }
        CHECK(changes.size() == 601 && total == 1208 && pass == 1208 && same == 1208 && out.size() == conf.size(),
              QByteArray("the S2S Configuration1.xml: 601 CRCs fixed, all 1208 now pass, every page_x and field the same, "
                         "the file the same size: ") + QByteArray::number(changes.size()) + " / " + QByteArray::number(pass));
    }

    // ---- 4. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w;
        w.show();
        RfidTag::Route r;
        r.dir = RfidTag::DirNominal;
        r.tags = { { QString(), normal(100, 1000) }, { QStringLiteral("550"), t550 }, { QString(), normal(101, 1300) } };
        w.setRoute(r);
        CHECK(w.fixRouteCrcs() == 1 && RfidTag::summary(w.route().tags.at(1).bytes).crcOk
                  && w.route().tags.at(0).bytes == r.tags.at(0).bytes && w.status()->text().contains(QLatin1String("550")),
              "Fix CRCs: the one failing tag fixed, the others untouched, and it says which");
        CHECK(w.fixRouteCrcs() == -1, "a second time: nothing to fix");
        CHECK(w.undo() && w.route().tags.at(1).bytes == t550, "Undo puts the old CRC back");

        QTemporaryDir dir;
        const QString in = dir.filePath(QStringLiteral("route.xml"));
        RfidTag::writeFile(in, RfidTag::toRouteXml(w.route()));
        QStringList changes;
        CHECK(w.fixFileCrcs(in, dir.filePath(QStringLiteral("fixed.xml")), &changes) && changes.size() == 2
                  && w.status()->text().contains(QLatin1String("2 tags")),
              QByteArray("Fix the CRCs in a file: tag 550 in the route and in its REV route: ") + w.status()->text().toUtf8());
        w.setRoute(RfidTag::Route());
    }
}
