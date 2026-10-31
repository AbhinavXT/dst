#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 441 — the track circuits occupied by the locos, from where their
//  ARPs and LSRPs put them. A loco occupies every circuit of its line (the
//  line of the tag it last read) its train covers: ABS_LOCO_LOC back
//  TRAIN_LENGTH against MOVEMENT_DIR. On station_527.xml (real: 31 circuits)
//  with the real ARPs and LSRPs of loco 1 (DN main, tags 10 … 898) and
//  loco 2 (UP, tags 985 … 3, then the loop by 22, 21, 20) in
//  replay/loco_{1,2}_1_27062026_170217.cap, in frame order.
// =============================================================================

namespace {

QStringList linesOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) out << l.trimmed();
    return out;
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

struct In { qint64 fn; int loco; QString line; QHash<QString, qint64> raw; };

QVector<In> heard(const QStringList &one, const QStringList &two)
{
    QVector<In> ins;
    for (int k = 0; k < 2; ++k)
        for (const QString &l : (k ? two : one))
            if (l.startsWith(QLatin1String("@arp_")) || l.startsWith(QLatin1String("@lsrp_"))) {
                const QHash<QString, qint64> r = rawOf(l);
                ins.append({ r.value(QStringLiteral("FRAME_NUM")), k + 1, l, r });
            }
    std::stable_sort(ins.begin(), ins.end(), [](const In &x, const In &y) { return x.fn != y.fn ? x.fn < y.fn : x.loco < y.loco; });
    return ins;
}

} // namespace

TEST_SUITE(session441)
{
    StationLayout::Layout l527;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527), "station_527.xml");
    const StationInterlock::Track track = StationInterlock::trackOf(l527);
    CHECK(track.circuits.size() == 31, "the track: its 31 circuits");
    CHECK(track.lineOfTag.value(906) == QStringLiteral("DM") && track.lineOfTag.value(985) == QStringLiteral("UM")
              && track.lineOfTag.value(20) == QStringLiteral("UL1") && !track.lineOfTag.contains(59),
          "the line of each tag: 906 DM, 985 UM, 20 UL1 (loop 1); 59 on no line");

    // ---- the real ARPs and LSRPs, both locos, in frame order ----
    const QStringList one = linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    const QStringList two = linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap"));
    const QVector<In> ins = heard(one, two);
    CHECK(ins.size() > 100, "the captures' ARPs and LSRPs");
    QHash<int, StationKavach::Loco> locos;
    QHash<int, QSet<QString>> ever;   // loco -> every circuit it occupied
    QSet<QString> notes;
    bool at906 = false, at906Right = false, ownLineOnly = true;
    for (const In &in : ins) {
        StationKavach::note(&locos[in.loco], in.raw);
        const StationInterlock::Occupancy occ = StationInterlock::occupancy(track, locos.values());
        for (auto it = occ.by.cbegin(); it != occ.by.cend(); ++it) {
            ever[it.value()].insert(it.key());
            for (const StationLayout::Circuit &c : track.circuits)
                if (c.name == it.key()) ownLineOnly = ownLineOnly && c.line == (it.value() == 1 ? QStringLiteral("DM") : c.line);
        }
        for (const QString &n : occ.notes) notes.insert(n);
        if (in.loco == 1 && !at906 && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 906 && in.raw.value(QStringLiteral("ABS_LOCO_LOC")) == 162066) {
            at906 = true;   // 162066 m, 250 m long: 161816 to 162066 on DM
            at906Right = occ.by.value(QStringLiteral("43T")) == 1 && occ.by.value(QStringLiteral("43AT")) == 1
                         && !occ.by.contains(QStringLiteral("43BT")) && !occ.by.contains(QStringLiteral("DMT"));
        }
    }
    CHECK(at906 && at906Right, "loco 1 at 162066 m past tag 906, 250 m long: 43T and 43AT occupied by it, 43BT and DMT not");
    const QSet<QString> l1 = ever.value(1), l2 = ever.value(2);
    CHECK(l1.contains(QStringLiteral("DMT")) && l1.contains(QStringLiteral("43BT")) && l1.contains(QStringLiteral("43CT")) && ownLineOnly,
          "loco 1 (DN) occupied DMT, 43T … 43CT as it ran: DM circuits only");
    CHECK(l2.contains(QStringLiteral("1640CT")) && l2.contains(QStringLiteral("1T")) && l2.contains(QStringLiteral("CL1T"))
              && !l2.contains(QStringLiteral("DMT")) && !l2.contains(QStringLiteral("UMT")),
          "loco 2 (UP, Reverse) occupied 1640CT … 1T, then CL1T on loop 1: its train lies ahead of it in km");
    bool said59 = false;
    for (const QString &n : notes) said59 = said59 || n == QStringLiteral("loco 2: tag 59 is on no line of the layout: it occupies nothing");
    CHECK(said59, "loco 2 at tag 59 (on no line of station_527.xml): said, nothing occupied");
    StationKavach::Loco nowhere;
    nowhere.id = 7;
    CHECK(StationInterlock::occupancy(track, { nowhere }).by.isEmpty()
              && StationInterlock::occupancy(track, { nowhere }).notes.value(0).startsWith(QStringLiteral("loco 7 has not said where it is")),
          "a loco not yet placed (ABS_LOCO_LOC 0): nothing, said");

    // ---- the answer: a circuit another loco occupies is said; the loco's own is not ----
    PacketBuilder b;
    StationLayout::Layout fixture;
    StationKavach::Station s;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture)
              && StationKavach::fromJson(fixture.kavach, &s),
          "station 527's KAVACH data (the session 422 fixture)");
    int s43 = -1;
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].entry == QLatin1String("S43")) s43 = i;
    CHECK(s43 >= 0, "route S43-A900");
    if (s43 < 0) return;
    s.routes[s43].trackCircuits = QStringList{ QStringLiteral("43T"), QStringLiteral("43AT") };
    QHash<QString, qint64> lsrp;
    for (const In &in : ins)
        if (in.loco == 1 && in.line.startsWith(QLatin1String("@lsrp_")) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 906) { lsrp = in.raw; break; }
    CHECK(!lsrp.isEmpty(), "loco 1's LSRP at tag 906");
    StationKavach::State st = StationKavach::State::initial(s);
    st.occupiedBy.insert(QStringLiteral("43T"), 1);
    StationKavach::Loco self;
    const StationKavach::Answer mine = StationKavach::answer(s, st, &self, lsrp, b.encoder());
    CHECK(mine.ok && mine.checks.filter(QStringLiteral("43T")).isEmpty(), "loco 1 answered: the circuit it occupies itself is not said");
    QHash<QString, qint64> other = lsrp;
    other.insert(QStringLiteral("SOURCE_LOCO_ID"), 2);
    StationKavach::Loco two2;
    const StationKavach::Answer theirs = StationKavach::answer(s, st, &two2, other, b.encoder());
    CHECK(theirs.ok && theirs.checks.contains(QStringLiteral("S43-A900 DN Auto: track circuit 43T is occupied by loco 1")),
          "a loco behind it on the route: 43T is occupied by loco 1, said");
    st.occupied.insert(QStringLiteral("43AT"));
    CHECK(st.isOccupied(QStringLiteral("43AT")) && st.isOccupied(QStringLiteral("43T")) && !st.isOccupied(QStringLiteral("43BT")),
          "occupied by hand or by a loco: both are occupied");

    // ---- Packet Maker: each ARP / LSRP heard moves its loco and the circuits ----
    QTemporaryDir tmp;
    StationLayout::Layout lay = l527;
    lay.kavach = StationKavach::toJson(s);
    const QString layPath = tmp.filePath(QStringLiteral("station_527.xml"));
    StationLayout::Layout back;
    CHECK(StationLayout::save(layPath, lay) && StationLayout::load(layPath, &back) && StationLayout::circuits(back).size() == 31
              && !back.kavach.isEmpty(),
          "station_527.xml with station 527's KAVACH data, saved and read back: circuits and KAVACH data");
    QString aapLine;
    for (const QString &x : two) if (x.startsWith(QLatin1String("@aap_"))) { aapLine = x; break; }
    PacketPreset preset;
    preset.captype = QStringLiteral("aap");
    preset.header = rawOf(aapLine);
    const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
    CHECK(!aapLine.isEmpty() && preset.save(presetPath), "the AAP preset from loco 2's capture");

    SessionKeyStore keys;
    PacketMakerDialog dlg(nullptr, nullptr, &keys);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
    QCheckBox *arm = nullptr;
    for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
        if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
    auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
    auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
    auto *stationBtn = dlg.findChild<QPushButton *>(QStringLiteral("pmStationPanel"));
    CHECK(arm && answer && from && stationBtn, "Packet Maker: arm, answer, its source, Station…");
    if (!(arm && answer && from && stationBtn)) return;
    from->setCurrentIndex(1);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(layPath);
    answer->setChecked(true);
    stationBtn->click();
    arm->setChecked(true);
    auto entry = [](const QString &line) {
        auto e = LogEntryPtr::create();
        e->text = line;
        e->epochMs = CaptureDecoder::parseLine(line).rtc.toMSecsSinceEpoch();
        e->header.source_id = 21;
        return e;
    };
    QString arp906;
    for (const In &in : ins)
        if (in.loco == 1 && in.line.startsWith(QLatin1String("@arp_")) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 906) { arp906 = in.line; break; }
    CHECK(!arp906.isEmpty(), "loco 1's ARP at tag 906");
    dlg.observe(entry(arp906));   // not answered (no keys in the log): where it is, still noted
    const qint64 at = rawOf(arp906).value(QStringLiteral("ABS_LOCO_LOC"));
    CHECK(dlg.stationLocos().value(1).absLoc == at && dlg.stationLocos().value(1).lastTag == 906,
          "the ARP moves loco 1 (no keys: not answered, still heard)");
    CHECK(dlg.stationState()->occupiedBy.value(QStringLiteral("43T")) == 1, "43T occupied by loco 1 from its ARP");
    auto *occTable = dlg.stationPanel() ? dlg.stationPanel()->findChild<QTableWidget *>(QStringLiteral("skOccupied")) : nullptr;
    bool shown = false;
    for (int i = 0; occTable && i < occTable->rowCount(); ++i)
        shown = shown || (occTable->item(i, 0) && occTable->item(i, 0)->text() == QLatin1String("43T") && occTable->item(i, 2)
                          && occTable->item(i, 2)->text() == QLatin1String("loco 1"));
    CHECK(shown, "Station… ▸ Occupied: 43T, by loco 1");
    QString lsrp898;
    for (const In &in : ins)
        if (in.loco == 1 && in.line.startsWith(QLatin1String("@lsrp_")) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 898) { lsrp898 = in.line; break; }
    dlg.observe(entry(lsrp898));
    CHECK(!dlg.stationState()->occupiedBy.contains(QStringLiteral("43T")) && dlg.stationState()->occupiedBy.value(QStringLiteral("43CT")) == 1,
          "its LSRP at tag 898: 43T clear again, 43CT occupied");
}
