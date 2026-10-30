#include "testutil.h"

#include "capturedecoder.h"
#include "logentry.h"
#include "messageheader.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationkavach.h"
#include "stationlayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUdpSocket>

// =============================================================================
//  Session 422 — a Stationary KAVACH made of data: the station's signals and
//  routes (a Table of Control's rows) answering each LSRP with the SLRP the
//  station would send: the route, the MA in sections from the aspect of the
//  signal ahead, the profile of those sections cut at the MA end, a new
//  REF_PROF_ID sent in full until the loco acknowledges it.
//  Checked against what station 527 really sent in
//  replay/2026-10-08/loco_1_1_08102026_105200.cap and _103520.cap, with
//  tests/fixtures/station_kavach/section30_527.json (built from those SLRPs).
// =============================================================================

namespace {

QStringList capture(const char *name)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'));
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

QString find(const QStringList &lines, const char *type, qint64 fn)
{
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String(type)) && rawOf(l).value(QStringLiteral("FRAME_NUM")) == fn) return l.trimmed();
    return {};
}

StationKavach::Station station527()
{
    StationLayout::Layout lay;
    StationKavach::Station s;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay);
    StationKavach::fromJson(lay.kavach, &s);
    return s;
}

// The real SLRP in `line`, with our FRAME_NUM and FRAME_OFFSET 0 (we answer
// that very LSRP; the station answered a little later), against ours: the
// bodies as the encoder writes them, MAC and CRC aside.
bool sameAsReal(const Schema::Encoder &enc, const StationKavach::Answer &a, const QString &line, QString *why = nullptr)
{
    Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(line).bytes);
    if (!pp.ok) { if (why) *why = pp.error; return false; }
    pp.header.insert(QStringLiteral("FRAME_NUM"), a.header.value(QStringLiteral("FRAME_NUM")));
    for (Schema::SubEntry &s : pp.subs)
        if (s.type == 0) s.values.insert(QStringLiteral("FRAME_OFFSET"), 0);
    QHash<QString, qint64> ours = a.header;
    ours.insert(QStringLiteral("PKT_LENGTH"), pp.header.value(QStringLiteral("PKT_LENGTH")));
    QString e1, e2;
    const QByteArray real = enc.encodeBody(QStringLiteral("slrp"), pp.header, pp.subs, &e1);
    const QByteArray mine = enc.encodeBody(QStringLiteral("slrp"), ours, a.subs, &e2);
    if (why) *why = QStringLiteral("%1 | %2 | real %3 | ours %4").arg(e1, e2, QString::fromLatin1(real.toHex(' ')),
                                                                       QString::fromLatin1(mine.toHex(' ')));
    return !real.isEmpty() && real == mine;
}

const Schema::SubEntry *subOf(const StationKavach::Answer &a, int type)
{
    for (const Schema::SubEntry &s : a.subs) if (s.type == type) return &s;
    return nullptr;
}

QByteArray receive(QUdpSocket &rx)
{
    QElapsedTimer t; t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
    return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
}

qint64 ma(const StationKavach::Answer &a, const char *field)
{
    const Schema::SubEntry *s = subOf(a, 0);
    return s ? s->values.value(QLatin1String(field), -1) : -1;
}

} // namespace

TEST_SUITE(session422)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    const StationKavach::Station st = station527();
    CHECK(st.stationId == 527 && st.signalList.size() == 4 && st.routes.size() == 3, "the station 527 fixture loads: 4 signals, 3 routes");
    CHECK(StationKavach::checks(st).isEmpty(), "the fixture has nothing to report");
    CHECK(st.routes[0].length() == 534 && st.routes[1].length() == 1116 && st.routes[2].length() == 840,
          "route lengths 534, 1116 and 840 m, from the linking distances");

    // ---- run 105200: every signal Yellow, S48-S43 under OS authority at 100 km/h ----
    {
        const QStringList lines = capture("loco_1_1_08102026_105200.cap");
        const StationKavach::State state = StationKavach::State::initial(st);
        StationKavach::Loco loco;
        QString why;

        StationKavach::Answer a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 40331)), enc);
        CHECK(a.ok && a.fullProfile && a.header.value("REF_PROF_ID") == 1, "loco at tag 16, no profile yet: profile 1 in full");
        CHECK(ma(a, "MA_W_R_T_SIG") == 1650 && ma(a, "APPR_SIG_DIST") == 122 && a.header.value("DIST_PKT_START") == -412,
              "S43 Yellow: 2 sections, MA 1650 m; ref 16, 122 m to S43, the start signal 412 m back");
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 40332), &why), "the full SLRP is station 527's, field for field");
        if (!sameAsReal(enc, a, find(lines, "@slrp_", 40332))) std::printf("        %s\n", qPrintable(why));

        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 40333)), enc);
        CHECK(a.ok && !a.fullProfile && a.subs.size() == 1, "the loco acknowledges profile 1: the MA alone");
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 40338), &why), "the MA-only SLRP is the station's");

        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 40349)), enc);
        CHECK(a.ok && a.header.value("REF_PROF_ID") == 2 && a.fullProfile && a.note.contains("S43-A900"),
              "tag 18 read: past S43, onto S43-A900; profile 2 in full");
        CHECK(ma(a, "TRAIN_SECTION_TYPE") == 2 && ma(a, "AUTHORITY_TYPE") == 2 && ma(a, "MA_W_R_T_SIG") == 1956,
              "Autoblock, FS, A900 Yellow: MA 1956 m to A904");
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 40357), &why), "profile 2 is the station's, field for field");
        if (!sameAsReal(enc, a, find(lines, "@slrp_", 40357))) std::printf("        %s\n", qPrintable(why));
        {
            StationKavach::Station other = st;
            other.routes[1].lc[0].insert(QStringLiteral("id"), 192);
            StationKavach::Loco l2;
            l2.profId = 1;
            l2.profKey = "another profile";
            const StationKavach::Answer x = StationKavach::answer(other, state, &l2, rawOf(find(lines, "@lsrp_", 40349)), enc);
            CHECK(x.ok && x.header.value("REF_PROF_ID") == 2 && !sameAsReal(enc, x, find(lines, "@slrp_", 40357)),
                  "the comparison bites: LC 192 for 191 is not the station's frame");
        }

        for (qint64 fn : { 40359, 40371, 40376 }) StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", fn)), enc);
        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 40380)), enc);
        CHECK(a.ok && !a.fullProfile && a.header.value("LAST_REF_RFID") == 906 && a.header.value("DIST_PKT_START") == -200
                  && ma(a, "APPR_SIG_DIST") == 916, "tag 906: ref 906, DIST_PKT_START -200, APPR_SIG_DIST 916");
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 40380), &why), "the SLRP answering LSRP 40380 is the station's");
    }

    // ---- run 103520: A900 Red, then Yellow; S48-S43 under FS ---------------------
    {
        const QStringList lines = capture("loco_1_1_08102026_103520.cap");
        StationKavach::State state = StationKavach::State::initial(st);
        state.aspect.insert(QStringLiteral("A900"), 1);
        state.authority.insert(QStringLiteral("S48-S43 DN Main"), 2);
        StationKavach::Loco loco;
        auto maMatches = [&](const StationKavach::Answer &a, qint64 realFn) {
            Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(find(lines, "@slrp_", realFn)).bytes);
            const Schema::SubEntry *m = subOf(a, 0);
            if (!pp.ok || pp.subs.isEmpty() || !m) return false;
            QHash<QString, qint64> real = pp.subs.first().values;
            real.insert(QStringLiteral("FRAME_OFFSET"), 0);
            return real == m->values && pp.header.value("LAST_REF_RFID") == a.header.value("LAST_REF_RFID")
                   && pp.header.value("DIST_PKT_START") == a.header.value("DIST_PKT_START");
        };
        StationKavach::Answer a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 38121)), enc);
        CHECK(a.ok && ma(a, "CUR_SIG_ASPECT") == 2 && ma(a, "NEXT_SIG_ASPECT") == 1 && ma(a, "MA_W_R_T_SIG") == 1650,
              "tag 12: S43 Yellow, A900 Red ahead, MA 1650 m");
        CHECK(maMatches(a, 38120), "its MA and ref are the station's (SLRP 38120)");
        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 38133)), enc);
        CHECK(a.ok && a.header.value("DIST_PKT_START") == -275 && ma(a, "APPR_SIG_DIST") == 259 && maMatches(a, 38133),
              "tag 14: DIST_PKT_START -275, APPR_SIG_DIST 259, as SLRP 38133");
        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 38155)), enc);
        CHECK(a.ok && ma(a, "CUR_SIG_ASPECT") == 1 && ma(a, "NEXT_SIG_ASPECT") == 0 && ma(a, "MA_W_R_T_SIG") == 1116
                  && maMatches(a, 38156), "tag 18, A900 Red: 1 section, MA 1116 m to A900, as SLRP 38156");
        state.aspect.insert(QStringLiteral("A900"), 2);
        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 38209)), enc);
        QString why;
        CHECK(a.ok && a.fullProfile && a.header.value("REF_PROF_ID") == 3 && ma(a, "MA_W_R_T_SIG") == 1956,
              "A900 cleared to Yellow: a new profile (3) in full, MA 1956 m");
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 38209), &why), "profile 3 is the station's SLRP 38209, field for field");
        a = StationKavach::answer(st, state, &loco, rawOf(find(lines, "@lsrp_", 38211)), enc);
        CHECK(a.ok && !a.fullProfile, "acknowledged: the MA alone");
    }

    // ---- the live state: said, not changed -----------------------------------------
    {
        StationKavach::Station s = st;
        s.routes[1].pointsNormal << QStringLiteral("P11");
        s.routes[1].tins << QStringLiteral("N-84");
        StationKavach::State state = StationKavach::State::initial(s);
        state.reverse.insert(QStringLiteral("P11"));
        state.occupied.insert(QStringLiteral("N-84"));
        state.aspect.insert(QStringLiteral("A900"), 11);
        state.aspect.insert(QStringLiteral("A904"), 1);
        StationKavach::Loco loco;
        QHash<QString, qint64> lsrp{ { "FRAME_NUM", 100 }, { "SOURCE_LOCO_ID", 1 }, { "LAST_RFID_TAG", 18 }, { "MOVEMENT_DIR", 1 } };
        const StationKavach::Answer a = StationKavach::answer(s, state, &loco, lsrp, enc);
        CHECK(a.ok, "answered all the same");
        CHECK(a.checks.join('\n').contains("P11 lies Reverse") && a.checks.join('\n').contains("TIN N-84 is occupied"),
              "the point lying wrong and the occupied TIN are said");
        CHECK(a.checks.join('\n').contains("Green with A904 Red ahead"), "Green with Red ahead is said");
        CHECK(a.checks.join('\n').contains("no route on from A904") && ma(a, "MA_W_R_T_SIG") == 1956,
              "Green wants 4 sections; 2 are known: MA to A904, and said");

        state = StationKavach::State::initial(s);
        state.aspect.insert(QStringLiteral("A900"), 15);
        const StationKavach::Answer c = StationKavach::answer(s, state, &loco, lsrp, enc);
        CHECK(c.ok && ma(c, "AUTHORITY_TYPE") == 1 && ma(c, "MA_W_R_T_SIG") == 1116, "calling-on: 1 section, on-sight authority");
    }

    // ---- refusals ---------------------------------------------------------------------
    {
        StationKavach::Loco loco;
        QHash<QString, qint64> lsrp{ { "FRAME_NUM", 100 }, { "SOURCE_LOCO_ID", 1 }, { "LAST_RFID_TAG", 555 }, { "MOVEMENT_DIR", 1 } };
        StationKavach::Answer a = StationKavach::answer(st, StationKavach::State::initial(st), &loco, lsrp, enc);
        CHECK(!a.ok && a.why.contains("555") && loco.profId == 0, "a tag on no route: refused, nothing remembered");

        StationKavach::Station s = st;
        s.routes[1].tags[2].dist = 2100;   // 11 bits
        s.routes[1].ssp[0].insert(QStringLiteral("d"), 1116 - 656 + 2100);
        s.routes[1].gradient[0].insert(QStringLiteral("d"), 1116 - 656 + 2100);
        lsrp.insert(QStringLiteral("LAST_RFID_TAG"), 18);
        a = StationKavach::answer(s, StationKavach::State::initial(s), &loco, lsrp, enc);
        CHECK(!a.ok && a.why.contains("dist") && a.why.contains("11 bits"), "a linking distance too wide: refused by name");

        s = st;
        s.routes[1].ssp[0].insert(QStringLiteral("d"), 1000);
        a = StationKavach::answer(s, StationKavach::State::initial(s), &loco, lsrp, enc);
        CHECK(!a.ok && a.why.contains("SSP covers 1000 m of its 1116"), "an SSP short of its route: refused");
        CHECK(StationKavach::checks(s).join('\n').contains("SSP covers 1000"), "and reported by the file's checks");
    }

    // ---- TSR: on, off -------------------------------------------------------------------
    {
        StationKavach::Station s = st;
        s.routes[1].tsr << StationKavach::Row{ { "id", 7 }, { "dist", 300 }, { "len", 2000 }, { "u", 6 }, { "whistle", 0 } };
        StationKavach::Loco loco;
        const QHash<QString, qint64> lsrp{ { "FRAME_NUM", 100 }, { "SOURCE_LOCO_ID", 1 }, { "LAST_RFID_TAG", 18 }, { "MOVEMENT_DIR", 1 } };
        StationKavach::State state = StationKavach::State::initial(s);
        StationKavach::Answer a = StationKavach::answer(s, state, &loco, lsrp, enc);
        const Schema::SubEntry *t = subOf(a, 7);
        CHECK(a.ok && t && t->values.value("TSR_STATUS") == 2 && t->repeats.value("tsr").size() == 1
                  && t->repeats.value("tsr").first().value("len") == 1956 - 300,
              "a TSR on: TSR_STATUS 2, its length cut at the MA end");
        state.tsrOff.insert(QStringLiteral("S43-A900 DN Auto/7"));
        a = StationKavach::answer(s, state, &loco, lsrp, enc);
        t = subOf(a, 7);
        CHECK(a.ok && a.fullProfile && a.header.value("REF_PROF_ID") == 2 && t && t->values.value("TSR_STATUS") == 0
                  && t->repeats.value("tsr").isEmpty(), "switched off: a new profile, TSR_STATUS 0, no rows");
    }

    // ---- the file -------------------------------------------------------------------------
    {
        StationLayout::Layout lay;
        CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay)
                  && !lay.kavach.isEmpty(), "the layout carries the kavach block");
        StationKavach::Station back;
        CHECK(StationKavach::fromJson(StationKavach::toJson(st), &back) && StationKavach::toJson(back) == StationKavach::toJson(st),
              "the block round-trips");
        StationLayout::Layout lj;
        CHECK(StationLayout::fromJson(StationLayout::toJson(lay), &lj) && lj.kavach == lay.kavach
                  && StationLayout::toJson(lay).contains("\"version\": 2"), "own file: kept, version 2");
        StationLayout::Layout plain;
        CHECK(StationLayout::toJson(plain).contains("\"version\": 1"), "without it, still version 1");
        StationLayout::Layout lx;
        CHECK(StationLayout::fromSheets(StationLayout::toSheets(lay), &lx) && lx.kavach == lay.kavach && lx.otherSheets.isEmpty(),
              ".xlsx: kept in its kavach sheet, read back as the block");
        CHECK(StationLayout::diff(plain, lay).other.contains(QStringLiteral("KAVACH data added")), "the diff names it");
    }

    // ---- the Packet Maker answers as the station, MAC and all ------------------------------
    {
        const QStringList lines = capture("loco_1_1_08102026_105200.cap");
        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
        SessionKeyStore keys;
        for (const QString &l : lines)
            if (l.startsWith(QLatin1String("@auth_keys"))) keys.observeLine(CaptureDecoder::parseLine(l.trimmed()));
        QString aapLine, arpLine;
        for (const QString &l : lines) {
            if (l.startsWith(QLatin1String("@aap_"))) { aapLine = l.trimmed(); break; }
            if (l.startsWith(QLatin1String("@arp_"))) arpLine = l.trimmed();
        }
        QTemporaryDir tmp;
        PacketPreset preset;
        preset.captype = QStringLiteral("aap");
        preset.header = rawOf(aapLine);
        const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
        CHECK(!arpLine.isEmpty() && preset.save(presetPath), "the ARP is found, the AAP preset written");

        PacketMakerDialog dlg(nullptr, nullptr, &keys);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
        if (QSpinBox *port = dlg.findChild<QSpinBox *>(QStringLiteral("pmDestPort"))) port->setValue(rx.localPort());
        QCheckBox *arm = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
        auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
        auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
        auto *log = dlg.findChild<QTableWidget *>(QStringLiteral("pmSendLog"));
        CHECK(arm && answer && from && log, "the arm switch, the answer box, its source and the log are found");
        if (!(arm && answer && from && log)) return;

        from->setCurrentIndex(1);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(QStringLiteral(":/station_layouts/Section_30.xlsx"));
        answer->setChecked(true);
        CHECK(!answer->isChecked(), "a layout without KAVACH data: answering as the station does not start");
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))
            ->setText(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"));
        answer->setChecked(true);
        CHECK(answer->isChecked(), "with it, it starts; no SLRP in the form is needed");

        auto entry = [](const QString &line) {
            auto e = LogEntryPtr::create();
            e->text = line;
            e->epochMs = CaptureDecoder::parseLine(line).rtc.toMSecsSinceEpoch();
            e->header.source_id = 21;
            return e;
        };
        dlg.setStationRandomForTest(quint16(preset.header.value("STN_RND_NUM_RS")));
        arm->setChecked(true);
        dlg.observe(entry(arpLine));
        CHECK(receive(rx).mid(MessageHeader::SIZE) == CaptureDecoder::parseLine(aapLine).bytes, "the AAP is the station's");

        QByteArray last;
        int answered = 0;
        for (const QString &l : lines) {
            if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
            const qint64 fn = rawOf(l).value(QStringLiteral("FRAME_NUM"));
            if (fn < 40331 || fn > 40380) continue;
            dlg.observe(entry(l.trimmed()));
            last = receive(rx);
            if (!last.isEmpty()) ++answered;
        }
        CHECK(answered >= 20, "every LSRP from 40331 to 40380 is answered");
        CHECK(last.mid(MessageHeader::SIZE) == CaptureDecoder::parseLine(find(lines, "@slrp_", 40380)).bytes,
              "the answer to LSRP 40380 is station 527's SLRP, byte for byte, MAC included");
        CHECK(log->item(log->rowCount() - 1, 7)->text().contains(QStringLiteral("route S43-A900")),
              "the log says which route and why");

        auto *btn = dlg.findChild<QPushButton *>(QStringLiteral("pmStationPanel"));
        CHECK(btn && btn->isEnabled(), "Station… is there while answering as the station");
        if (!btn) return;
        btn->click();
        auto *panel = dlg.findChild<QDialog *>(QStringLiteral("stationKavachPanel"));
        auto *sigs = panel ? panel->findChild<QTableWidget *>(QStringLiteral("skSignals")) : nullptr;
        CHECK(sigs && sigs->rowCount() == 4 && sigs->item(2, 0)->text() == QLatin1String("A900"),
              "the panel lists the 4 signals");
        if (!sigs) return;
        auto *a900 = qobject_cast<QComboBox *>(sigs->cellWidget(2, 2));
        CHECK(a900 && a900->currentData().toInt() == 2, "A900 shows Yellow, as the file starts it");
        if (!a900) return;
        a900->setCurrentIndex(a900->findData(1));
        CHECK(dlg.stationState()->aspect.value(QStringLiteral("A900")) == 1, "set to Red in the panel: the station's state");
        dlg.observe(entry(find(lines, "@lsrp_", 40380)));
        const QByteArray red = receive(rx);
        Schema::ParsedPacket pp = b.encoder().parseBody(QStringLiteral("slrp"), red.mid(MessageHeader::SIZE));
        CHECK(pp.ok && pp.subs.size() == 6 && pp.subs.first().values.value("MA_W_R_T_SIG") == 1116
                  && pp.header.value("REF_PROF_ID") == 3,
              "A900 put to Red: MA 1116 m, a new profile 3 sent in full");
        auto *answers = panel->findChild<QPlainTextEdit *>(QStringLiteral("skAnswers"));
        CHECK(answers && answers->toPlainText().contains(QStringLiteral("Loco 1: LSRP 40380 answered"))
                  && answers->toPlainText().contains(QStringLiteral("A900 Red → 1 section, MA 1116 m")),
              "the panel says what was sent and why");
        answer->setChecked(false);
        CHECK(!btn->isEnabled() && !panel->isVisible(), "answering stopped: Station… is off, the panel hidden");
    }
}
