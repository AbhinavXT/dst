#include "testutil.h"

#include "benchrecord.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "settings.h"
#include "tagsenddialog.h"

#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 410 — Send a tag…: one RFID tag, now, by hand, from its page_x and
//  page_y, on reader 1 and / or reader 2: LocoTcasSimulator's "SPAD scenario
//  RFID interface" (spadsceneriorfidinterface.cpp) and its buildTagPacket
//  (structures.h). The page values are the simulator's own, from its
//  Configuration1.xml.
// =============================================================================

TEST_SUITE(session410)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    const QVector<RfidTag::Route> routes
        = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty() && !routes.first().tags.isEmpty() && routes.first().tags.first().file.present,
          "the simulator's Configuration1.xml, its rows' page_x / page_y as written");
    if (routes.isEmpty() || routes.first().tags.isEmpty()) return;

    // ---- buildTagPacket -------------------------------------------------------------------------
    bool same = true;
    int rows = 0;
    for (const RfidTag::Tag &t : routes.first().tags) {
        if (!t.file.present) continue;
        ++rows;
        same = same && LocoSim::simulatorTagFrame(1, t.file.pageX, t.file.pageY) == LocoSim::tagFrame(1, t.bytes)
               && LocoSim::simulatorTagFrame(2, t.file.pageX, t.file.pageY) == LocoSim::tagFrame(2, t.bytes);
    }
    CHECK(same && rows > 10, qPrintable(QStringLiteral("every row of the file (%1): the simulator's frame from page_x / page_y "
                                                        "is the route's own tag frame").arg(rows)));
    const RfidTag::Tag &first = routes.first().tags.first();
    CHECK(LocoSim::simulatorTagFrame(1, first.file.pageX.left(15), first.file.pageY).isEmpty(), "31 hex digits: refused");
    CHECK(LocoSim::simulatorTagFrame(1, QString(first.file.pageX).replace(0, 1, QLatin1Char('g')), first.file.pageY).isEmpty(),
          "a digit that is not hex: refused");

    // ---- the window --------------------------------------------------------------------------------
    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    struct Out { int port; QByteArray frame; };
    QVector<Out> out;
    window.setTagSink([&out](int port, const QByteArray &f) { out.append({ port, f }); });
    window.setSendSink([](const QByteArray &) {});
    QString why;
    CHECK(window.sendTagByHand(1, first.file.pageX, first.file.pageY, &why) && out.size() == 1 && out.at(0).port == 0
              && out.at(0).frame == LocoSim::tagFrame(1, first.bytes),
          "reader 1 by hand: the frame on port A, no route or run needed");
    window.setReader2OwnPort(true);
    out.clear();
    CHECK(window.sendTagByHand(2, first.file.pageX, first.file.pageY) && out.size() == 1 && out.at(0).port == 1
              && quint8(out.at(0).frame.at(3)) == 2,
          "reader 2 on its own port: on port B");
    window.setReader2OwnPort(false);
    CHECK(!window.sendTagByHand(1, QString(), first.file.pageY, &why) && why.contains(QLatin1String("empty")), "an empty page_x: refused, said why");
    CHECK(!window.sendTagByHand(1, QStringLiteral("12"), QStringLiteral("34"), &why) && why.contains(QLatin1String("32 hex digits")),
          "too few digits: refused, said why");
    {
        LocoSimWindow noPort(nullptr);
        noPort.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(!noPort.sendTagByHand(1, first.file.pageX, first.file.pageY, &why) && why == QLatin1String("port A is not open"),
              "port A closed: not sent, said why");
    }

    // ---- Send a tag… ---------------------------------------------------------------------------------
    window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    window.setRoute(0);
    TagSendDialog *dialog = window.openTagSend();
    CHECK(dialog && dialog->isVisible() && window.openTagSend() == dialog, "one window, raised again");
    dialog->fillFromRoute(2);
    const RfidTag::Tag &third = routes.first().tags.at(2);
    CHECK(dialog->says(1).startsWith(QStringLiteral("Tag %1").arg(RfidTag::nameOf(third.bytes))) && dialog->says(1).contains(QLatin1String("matches"))
              && dialog->says(2).isEmpty(),
          qPrintable(QStringLiteral("From the route fills the ticked reader 1 only: %1").arg(dialog->says(1))));
    dialog->setReader(2, true);
    dialog->fillFromRoute(2);
    out.clear();
    QTemporaryDir dir;
    BenchRecord::Recorder recorder;
    window.setRecorder(&recorder);
    recorder.start(dir.filePath(QStringLiteral("t.benchrec")), 0);
    CHECK(dialog->send() == 2 && out.size() == 2 && out.at(0).frame == LocoSim::tagFrame(1, third.bytes)
              && out.at(1).frame == LocoSim::tagFrame(2, third.bytes),
          "Send with both ticked: reader 1's frame, then reader 2's");
    recorder.stop(0);
    window.stop();
    window.setRecorder(nullptr);
    BenchRecord::File f;
    BenchRecord::read(dir.filePath(QStringLiteral("t.benchrec")), &f);
    int byHand = 0;
    for (const BenchRecord::Item &i : f.items) byHand += i.kind == QLatin1String("tag") && i.says.endsWith(QLatin1String("by hand")) ? 1 : 0;
    CHECK(byHand == 2, "recorded as tags sent by hand");
    dialog->setPages(1, QStringLiteral("0011"), QStringLiteral("22"));
    CHECK(dialog->says(1).contains(QLatin1String("32 hex digits")), "bad page values: said under them");
    out.clear();
    CHECK(dialog->send() == 1 && out.size() == 1, "Send: reader 2 goes, reader 1 refused (and said)");
    // A tag the route does not hold, its CRC wrong: sent as written.
    QString x = RfidTag::pageX(third.bytes), y = RfidTag::pageY(third.bytes);
    y.replace(0, 1, y.at(0) == QLatin1Char('0') ? QLatin1Char('1') : QLatin1Char('0'));
    dialog->setReader(2, false);
    dialog->setPages(1, x, y);
    CHECK(dialog->says(1).contains(QLatin1String("does not match")), "a changed page_y: the CRC-30 said not to match");
    out.clear();
    CHECK(dialog->send() == 1 && out.size() == 1 && out.at(0).frame == LocoSim::simulatorTagFrame(1, x, y), "and sent as written");

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
