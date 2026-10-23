#include "testutil.h"

#include "cabio.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QSet>
#include <QSettings>

// =============================================================================
//  Session 327 — Cab I/O Simulator: which value each output acts on, and the
//  old cab's controller ids.
//
//  Abhinav: "For old output, 1 is relay energised. But for new outputs for
//  buzzer, brakes, sifa valve 0 means buzzer rang, brake applied or sifa
//  valve opened." And the old cab's inputs answer as the same ids, 5-12.
//
//  The real output word is the bench capture's (session 325): 0x03E1 to
//  output cards 13 and 14.
// =============================================================================

namespace {

using namespace CabIo;

const OutputBit *bitNamed(Cab cab, const char *name)
{
    for (const OutputBit &o : outputBits(cab))
        if (o.name == QLatin1String(name)) return &o;
    return nullptr;
}

}  // namespace

TEST_SUITE(session327)
{
    // ---- polarity -----------------------------------------------------------------------------
    {
        QSet<QString> low;
        for (const OutputBit &o : outputBits(Cab::Sifa))
            if (o.activeLow) low.insert(o.name);
        CHECK(low == QSet<QString>({ QStringLiteral("dmi1_buzzer"), QStringLiteral("dmi2_buzzer"),
                                     QStringLiteral("light_engine_brake"), QStringLiteral("full_service_brake"),
                                     QStringLiteral("normal_brake"), QStringLiteral("emergency_brake"),
                                     QStringLiteral("sifa_valve") }),
              "SIFA: exactly the buzzers, the four brakes and the SIFA valve act on 0");
        bool anyOldLow = false;
        for (const OutputBit &o : outputBits(Cab::Old)) anyOldLow |= o.activeLow;
        CHECK(!anyOldLow, "old cab: every output acts on 1 (relay energised)");

        const quint16 real = 0x03E1;     // bits 0, 5-9 set
        const OutputBit *eb = bitNamed(Cab::Sifa, "emergency_brake");
        const OutputBit *bz = bitNamed(Cab::Sifa, "dmi1_buzzer");
        const OutputBit *sv = bitNamed(Cab::Sifa, "sifa_valve");
        const OutputBit *red = bitNamed(Cab::Sifa, "dmi_sos_red");
        const OutputBit *h1 = bitNamed(Cab::Sifa, "horn1");
        CHECK(eb && bz && sv && red && h1, "fixture: the SIFA outputs by name");
        if (eb && bz && sv && red && h1) {
            CHECK(outputActive(*eb, real) && eb->activeWord == QLatin1String("Applied"),
                  "0x03E1: emergency_brake bit 13 is 0 -> Applied");
            CHECK(outputActive(*bz, real) && bz->activeWord == QLatin1String("Sounding"),
                  "dmi1_buzzer bit 2 is 0 -> Sounding");
            CHECK(outputActive(*sv, real) && sv->activeWord == QLatin1String("Open"),
                  "sifa_valve bit 14 is 0 -> Open");
            CHECK(outputActive(*red, real) && !outputActive(*h1, real),
                  "dmi_sos_red bit 0 is 1 -> Energised; horn1 bit 4 is 0 -> De-energised");
            CHECK(!outputActive(*eb, quint16(real | (1u << 13))), "emergency_brake bit 13 set -> Released");
        }
        const OutputBit *ok = bitNamed(Cab::Old, "kavach_ok");
        const OutputBit *oldEb = bitNamed(Cab::Old, "emergency_brake");
        CHECK(ok && oldEb && outputActive(*ok, 0x0001) && !outputActive(*oldEb, 0x0001)
                  && outputActive(*oldEb, 0x0080),
              "old: kavach_ok on bit 0 = 1; emergency_brake applied only with bit 7 = 1");
    }

    // ---- the old cab answers as 5-12 ------------------------------------------------------------
    {
        CHECK(oldCabInputs().ids == QVector<quint8>({ 5, 6, 7, 8, 9, 10, 11, 12 }),
              "STRUCT_CAB_INPUT: the default ids are 5-12");
#ifdef DL_HAVE_SERIAL
        {
            QSettings s(Settings::iniPath(), QSettings::IniFormat);
            s.remove(QStringLiteral("cabio/ids_old"));      // a saved choice would win
        }
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setCab(Cab::Old);
        w.setWireFormat(WireFormat());
        w.setRespond(CabIoSimWindow::Respond::Answer);
        CHECK(w.bankCount() == 1 && w.bankIds(0) == QVector<quint8>({ 5, 6, 7, 8, 9, 10, 11, 12 }),
              "the window's old cab bank answers as all eight");
        for (quint8 id = 5; id <= 12; ++id) w.feedBytes(inputRequest(id, id, WireFormat()));
        CHECK(sent.size() == 8, "one reply to each of 5-12");
        w.setCab(Cab::Sifa);
        w.close();
#endif
    }
}
