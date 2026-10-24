#include "testutil.h"

#include "dmipanel.h"

#include <QImage>
#include <QPainter>

#include <cmath>

// =============================================================================
//  Session 352 — the DMI window against RDSO SIF 0522 v1.2, "Functional Test
//  Cases for LP-OCIP (DMI)". Each check names its test case. The expected
//  strings are the PDF's Expected Output column, as written. What the window
//  cannot show (the DMI's own keys and screens, hardware) is listed in the
//  CHANGELOG, not here.
// =============================================================================

namespace {
const QString d = QStringLiteral("–");   // the PDF's en dash
QString w(const char *utf8) { return QString::fromUtf8(utf8).replace(QStringLiteral("--"), d); }
}  // namespace

TEST_SUITE(session352)
{
    QHash<QString, qint64> raw{
        { QStringLiteral("ack_blockstop_sos_generate_time"), 15 },
        { QStringLiteral("collision_loco_id"), 23456 },
        { QStringLiteral("collision_loco_distance"), 812 },
        { QStringLiteral("override_timeout"), 45 },
        { QStringLiteral("expiry_reverse_distance"), 250 },
        { QStringLiteral("expiry_reverse_mode"), 120 },
        { QStringLiteral("lc.distance"), 900 },
        { QStringLiteral("target_distance"), 1200 },
        { QStringLiteral("to_speed"), 30 },
        { QStringLiteral("tsr_distance"), 700 },
        { QStringLiteral("target_speed"), 45 },
        { QStringLiteral("track_condition_start_distance"), 1500 },
        { QStringLiteral("train_length"), 650 },
        { QStringLiteral("context_pressure"), 512 },
        { QStringLiteral("fsb_applied_time"), 8 },
        { QStringLiteral("eb_applied_time"), 5 },
        { QStringLiteral("sos_station_id"), 1201 },
        { QStringLiteral("station_id"), 4501 },
    };
    QHash<QString, QString> text{ { QStringLiteral("lc.id"), QStringLiteral("17 (gate)") },
                                  { QStringLiteral("train_type"), QStringLiteral("2 (GOODS_TRAIN)") } };

    // ---- KAVACH_DMI_14: region H, the 70 alarm flags (schema name -> wording) ----
    const struct { const char *flag; const char *want; } h[] = {
        { "System Fault, Isolate or Restart KAVACH", "System Fault, Isolate or Restart KAVACH" },                    // 14.1
        { "Ack Block Stop, SOS Generates in XX s", "Ack Loco Block stop, SOS Generates in 15 s" },                   // 14.2
        { "EB Bypassed (EB Cock Closed), No Traction", "Emergency EB Bypassed (EB Cock Closed), No Traction" },      // 14.3
        { "Train Tripped, Select P_Trip", "Train Tripped, Select P_Trp" },                                           // 14.4
        { "Brake Applied, Dead End Detected", "Brake Applied, Dead End Detected" },                                  // 14.5
        { "Standstill Protection, Brake Applied", "Standstill Protection, Brake Applied" },                          // 14.6
        { "Rollback Protection, Brake Applied", "Rollback Protection, Brake Applied" },                              // 14.7
        { "REV Movement Not Allowed, Use REV mode", "REV Movement Not Allowed, Use REV mode" },                      // 14.8
        { "Stand By mode - CAB input not Active", "Stand By mode- CAB input is not Active" },                        // 14.9
        { "Ack SR mode, KAVACH Territory", "Ack SR mode-- KAVACH Territory Exit" },                                  // 14.10
        { "Ack SR mode, Station Radio Comm Fail", "Ack SR mode-- Station Radio Comm Fail with Station" },            // 14.11
        { "Ack SR mode, No Track Profile Info", "Ack SR mode--No Track Profile Info" },                              // 14.12
        { "Ack SR mode, Tags Missing", "Ack SR mode -- Tags missing" },                                              // 14.13
        { "Ack SR mode, Direction Unknown", "Ack SR mode -- Direction unknown" },                                    // 14.14
        { "Ack SR mode, GPS Fail", "Ack SR mode -- GPS Fail" },                                                      // 14.15
        { "Ack LS mode, Station Radio Comm Fail", "Ack LS mode -- Station Radio Comm Fail with Station" },           // 14.16
        { "Ack SR mode, TSR Link Fail", "Ack SR mode -- TSR Link Fail" },                                            // 14.17
        { "Head-On Collision with Loco", "Head On Collision with Loco 23456 in 0812 m" },                            // 14.18
        { "Rear-End Collision with Loco", "Rear End Collision with Loco 23456 in 0812 m" },                          // 14.19
        { "Override Selected, Pass Signal", "Override selected, Pass Signal in 45 s" },                              // 14.20
        { "Reverse Mode Expires", "Reverse Mode Expires in 0250m or 120s" },                                         // 14.21
        { "Manned LC Gate", "Manned LC Gate 17 approaching in 0900m" },                                              // 14.22
        { "Unmanned LC Gate", "Unmanned LC Gate 17 approaching in 0900m" },                                          // 14.23
        { "LS mode - Waiting for Station Radio Comm", "LS mode Waiting for Station Radio Comm with station" },       // 14.24
        { "LS mode - Waiting for Track Profile Info", "LS mode Waiting for Track Profile info" },                    // 14.25
        { "Leading/Non-Leading Input Active", "Both Leading & Non-leading Inputs are Active" },                      // 14.26
        { "Train Length Computation In Progress", "Train Length Computation in Progress" },                          // 14.27
        { "Train Length Computation Success", "Train Length Computation Success (0650m)" },                          // 14.28
        { "Train Length Computation Fail", "Train Length Computation Fail (0650m)" },                                // 14.29
        { "Train Length Computation Aborted", "Train Length Computation Aborted" },                                  // 14.30
        { "Turnout with Speed Limit", "TurnOut in 1200m with speed limit 30Kmph" },                                  // 14.31
        { "TSR with Speed Limit", "TSR in 0700m with speed limit 45Kmph" },                                          // 14.32
        { "PSR with Speed", "PSR in 1200m with speed limit 45Kmph" },                                                // 14.33
        { "End of Authority", "End of Authority in 1200m" },                                                         // 14.34
        { "KAVACH Territory Entry", "KAVACH Territory Entry" },                                                      // 14.35
        { "System Self Test In Progress", "System Self-Test in progress" },                                          // 14.36
        { "System Self Test Success", "System Self-Test Success" },                                                  // 14.37
        { "System Self Test Fail", "System Self-Test Fail" },                                                        // 14.38 (no code in the frame)
        { "Brake Test - Waiting for MR", "Brakes Testing -- Waiting for MR 5.12 kg/cm\xC2\xB2" },                    // 14.39 (one value)
        { "Brake Test - Waiting for BP", "Brakes Testing -- Waiting for BP 5.12 kg/cm\xC2\xB2" },                    // 14.40
        { "Brake Test - NSB Applied (BP)", "Brakes Testing -- NSB Applied, BP-5.12kg/cm\xC2\xB2" },                  // 14.41
        { "Brake Test - FSB Applied (BP)", "Brakes Testing -- FSB Applied, BP-5.12kg/cm\xC2\xB2" },                  // 14.42
        { "Brake Test - EB Applied (BP)", "Brakes Testing -- EB Applied, BP-5.12kg/cm\xC2\xB2" },                    // 14.43
        { "Brake Test - LEB Applied (BC)", "Brakes Testing -- LEB Brake applied, BC 5.12 Kg/cm\xC2\xB2" },           // 14.44
        { "Brakes Testing Success", "Brakes Testing Success" },                                                      // 14.45
        { "Brakes Testing Fail", "Brakes Testing Fail" },                                                            // 14.46 (which brake: not in the frame)
        { "Brakes Test Fail, Press Ack for Retesting", "Brakes Test Fail, Press ACK for Retesting Brakes" },         // 14.47
        { "Select Train Config, Press Config Button", "Select Train Configuration, Press 'CONFIG' Button" },         // 14.48
        { "Select Staff Responsible / Shunt mode", "Select Staff Responsible or Shunt Mode" },                       // 14.49
        { "Approaching Radio Hole", "Approaching Radio Hole in 01500 m" },                                           // 14.50
        { "Ack OS mode", "ACK OS Mode" },                                                                            // 14.51
        { "Train in Fouling Zone, Normalize the REV", "Loco/Train is in FM Zone, Normalize the Reverser" },          // 14.52
        { "SR/SH mode - ETCS Territory Entry", "SR/SH Mode- ETCS--ETCS Territory Entry" },                           // 14.53
        { "FS mode - ETCS Full Supervision mode", "FS mode ETCS- ETCS Full Supervision Mode" },                      // 14.54
        { "Ack SR mode - ETCS Territory Exit", "ACK SR Mode -- ETCS Territory Exit" },                               // 14.55
        { "Ballise Default Telegram Received", "Ballise Default telegram received" },                                // 14.56
        { "Waiting for Traction Command", "Waiting for Traction Command" },                                          // 14.57
        { "Traction Cut-off Command Fail", "Traction cut-off Command fail" },                                        // 14.58
        { "Fouling Mark Entry", "Fouling Mark Entry" },                                                              // 14.59
        { "Fouling Mark Clear", "Fouling Mark Clear" },                                                              // 14.60
        { "Neutral Section Approaching", "Neutral Section approaching in 1500 m" },                                  // 14.61
        { "Braking System Malfunction", "Braking system malfunction" },                                              // 14.62
        { "No Forward Dir in REV mode", "No Forward Dir in REV Mode" },                                              // 14.63
        { "Ack SR mode - SR Authorized Received", "ACK SR Mode - SR Authorization Received" },                       // 14.64
        { "Ack SR mode - Slip/Skid Detected", "ACK SR Mode - Slip/Skid Detected" },                                  // 14.65
        { "Ack SR mode - Foreign Tag Detected", "ACK SR mode- Foreign Tag Detected" },                               // 14.66
        { "Ack SR mode - Odo Error Detected", "Ack for SR mode - Odo Error detected" },                              // 14.67
        { "Brake Applied - Shunting Limit Exceed", "Brake Applied- Shunting limits exceeded" },                      // 14.68
        { "Brake Applied - Station General SOS", "Brake Applied- Station General SoS" },                             // 14.69
        { "Brake Applied - SPAD Detected", "Brake Applied-SPAD detected" },                                          // 14.70
    };
    int i = 1;
    QStringList wrong;
    for (const auto &c : h) {
        const QString got = dmiSystemMessage(QLatin1String(c.flag), raw, text);
        if (got != w(c.want)) wrong << QStringLiteral("14.%1: \"%2\"").arg(i).arg(got);
        ++i;
    }
    CHECK(i == 71, "fixture: 70 alarm flags");
    CHECK(wrong.isEmpty(), QByteArray("14.1-14.70: region H reads as the test cases (") + wrong.join(QLatin1String("; ")).toUtf8() + ")");

    // ---- KAVACH_DMI_15: region I, the 14 context flags --------------------------------------
    const struct { const char *flag; const char *want; } ctx[] = {
        { "SOS - Self Loco Manual", "SOS -- Self Loco (Manual)" },                                                   // 15.1
        { "SOS - Self Loco Stopped in Block Section", "SOS -- Self Loco (Stopped in Block Section)" },               // 15.2
        { "SOS - Self Loco Train Parted", "SOS -- Self Loco (Train Parted)" },                                       // 15.3
        { "SOS - Other Loco Manual", "SOS -- From Loco 23456 (Manual)" },                                            // 15.4
        { "SOS - Other Loco Stopped in Block Section", "SOS -- From Loco 23456 (Stopped in Block Section)" },        // 15.5
        { "SOS - Other Loco Train Parted", "SOS -- From Loco 23456 (Train Parted)" },                                // 15.6
        { "SOS - Station (All Locos)", "SOS -- From Station 1201 (SOS to All Locos)" },                              // 15.7
        { "SOS - Station (This Loco)", "SOS -- From Station 4501 (SOS to this Loco)" },                              // 15.8
        { "Over Speed - Reduce Speed", "Over Speed, Please Reduce Speed" },                                          // 15.9
        { "Brake Applied - Speed Limit Exceeded", "Brake Applied, Speed Limit Exceeded" },                           // 15.10
        { "FSB will be Applied in XX s", "FSB will be applied in 8s" },                                              // 15.11
        { "EB will be Applied in XX s", "EB will be applied in 5s" },                                                // 15.12
        { "BIU Isolated", "BIU Isolated" },                                                                          // 15.13
        { "Train Type Selected", "GOODS TRAIN Train Type selected" },                                                // 15.14
    };
    i = 1;
    wrong.clear();
    for (const auto &c : ctx) {
        const QString got = dmiContextMessage(QLatin1String(c.flag), raw, text);
        if (got != w(c.want)) wrong << QStringLiteral("15.%1: \"%2\"").arg(i).arg(got);
        ++i;
    }
    CHECK(wrong.isEmpty(), QByteArray("15.1-15.14: region I reads as the test cases (") + wrong.join(QLatin1String("; ")).toUtf8() + ")");

    // ---- single regions ----------------------------------------------------------------------
    CHECK(dmiLocationText(9.68) == QLatin1String("LOC: 9.68 km") && dmiLocationText(52.47) == QLatin1String("LOC: 52.47 km")
              && dmiLocationText(135.09) == QLatin1String("LOC: 135.09 km"),
          "7.1/7.3/7.4: B9 \"LOC: 52.47 km\"");
    CHECK(dmiTrainLengthText(650) == QLatin1String("TL 650 m") && dmiTrainLengthText(2047) == QLatin1String("TL 2047 m"),
          "17.2/17.4/17.5: F \"TL 650 m\"");
    CHECK(dmiTrainLengthText(0) == QLatin1String("TL Invalid"), "17.3: train length 0 is shown as invalid");
    CHECK(dmiTargetDistanceText(500) == QLatin1String("500 m") && dmiTargetDistanceText(1000) == QLatin1String("1000 m"),
          "3.11/3.12: A3 \"500 m\", \"1000 m\"");
    CHECK(dmiModeName(11) == QLatin1String("Non-Leading Mode") && dmiModeName(13) == QLatin1String("Isolation Mode"),
          "19.10/19.12: G names the modes");
    CHECK(!dmiSpeedColours(11) && !dmiSpeedColours(13) && dmiSpeedColours(4) && dmiSpeedColours(2),
          "4.33/4.34: no speed colours in Non-Leading or Isolation mode");

    // 4.33 drawn: Non-Leading at 50 km/h under a 100 km/h permit: no light-green ring.
    {
        DmiState s;
        s.valid = true;
        s.speed = 50;
        s.permitted = 100;
        s.hasPermitted = true;
        const auto ringGreen = [](const DmiState &st) {
            DmiView v;
            v.resize(800, 600);
            v.setState(st);
            QImage img(800, 600, QImage::Format_RGB32);
            v.render(&img);
            // B3 outer ring at 20 km/h: dial centre (274.5, 186), radius ~173.
            const double a = (149.0 - 20 * 298.0 / 250.0) * 3.14159265358979323846 / 180.0;
            const QColor px = img.pixelColor(int(274.5 - 173 * std::sin(a)), int(186 - 173 * std::cos(a)));
            return px == QColor(128, 255, 0);
        };
        s.mode = 4;
        CHECK(ringGreen(s), "fixture: Full Supervision draws the light-green permit ring");
        s.mode = 11;
        CHECK(!ringGreen(s), "4.33: Non-Leading draws none");
        s.mode = 13;
        CHECK(!ringGreen(s), "4.34: Isolation draws none");
    }
    CHECK(dmiNoDataText() == QLatin1String("Waiting for communication"), "22.30: no data yet: \"Waiting for communication\"");
    CHECK(dmiStaleText() == QLatin1String("Communication link failure"), "1.11/1.12: data stopped: \"Communication link failure\"");
}
