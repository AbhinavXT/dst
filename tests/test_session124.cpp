#include "testutil.h"
#include "layoutaudit.h"

#include "cabpanel.h"
#include "lococonsolewindow.h"
#include "messagedispatcher.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QFile>
#include <QLabel>

// =============================================================================
//  Session 124 — UI revamp, tool windows 3: the Live Loco Console.
//
//  One duration format everywhere ("96d 20h", not "8369761 s"); heartbeat
//  lights that wrap instead of running off the edge; readouts and actions on
//  separate rows so the window fits a laptop; CRC and seq as chips.
//  Real traffic: replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session124)
{
    using UiStyle::durationText;
    CHECK(durationText(-1) == QLatin1String("--"), "no time yet: --");
    CHECK(durationText(850) == QLatin1String("850 ms"), "under a second in ms");
    CHECK(durationText(4200) == QLatin1String("4.2 s"), "seconds with a tenth");
    CHECK(durationText(725000) == QLatin1String("12m 05s"), "minutes and seconds");
    CHECK(durationText(50580000) == QLatin1String("14h 03m"), "hours and minutes");
    CHECK(durationText(8369761000LL) == QLatin1String("96d 20h"), "days and hours: what \"8369761 s\" meant");

    // Heartbeat lights wrap.
    {
        LinkLights lights;
        QVector<Heartbeat> beats;
        for (const char *n : { "arp", "slrp", "lsrp", "nms hlth", "nms rssi", "dmi", "dop1", "dop2", "ccsys", "dlsys" }) {
            Heartbeat b;
            b.name = QString::fromLatin1(n);
            b.state = Heartbeat::State::Silent;
            b.ageMs = 8369761000LL;
            beats << b;
        }
        lights.setBeats(beats);
        CHECK(lights.hasHeightForWidth() && lights.heightForWidth(400) > lights.heightForWidth(3000),
              "the lights wrap onto more lines when narrow");
        CHECK(lights.minimumSizeHint().width() <= 200, "and never set a window's minimum width");
    }

    // The console, fed real traffic.
    MessageDispatcher disp;
    LocoConsoleWindow w(&disp, nullptr);
    w.resize(1100, 720);
    w.show();
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    int n = 0;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && n < 400) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(21, 1, l, 1782558147000LL + n * 137, QString());
            if (++n % 50 == 0) { disp.drainNow(); QCoreApplication::processEvents(); }
        }
    }
    disp.drainNow();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    CHECK(n > 300, "fixture: real capture lines");

    CHECK(w.minimumSizeHint().width() <= 1366, QByteArray("the console fits a 1366-px laptop (minimum ")
                                                   + QByteArray::number(w.minimumSizeHint().width()) + " px)");
    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no visible widget outside every layout");
    QLabel *crc = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>()) if (l->text().contains(QLatin1String("CRC"))) crc = l;
    CHECK(crc && crc->property("dlRole").toString() == QLatin1String("chip"), "CRC is a chip");
    CHECK(crc && (crc->text().contains(QLatin1String("failed")) == (UiStyle::toneOf(crc) == UiStyle::Tone::Fail)),
          QByteArray("its tone and its words agree (\"") + (crc ? crc->text().toUtf8() : QByteArray()) + "\")");
}
