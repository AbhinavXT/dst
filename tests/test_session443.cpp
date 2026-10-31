#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 443 — the automatic signals work themselves: a signal every route
//  from which is an Autoblock section with track circuits shows Red while
//  the section ahead is occupied, else one step up from the signal at its
//  end (Red -> Yellow -> Double Yellow -> Green).
//  Station 527's KAVACH data (the session 422 fixture) given the circuits
//  of station_527.xml that lie on its routes (S43-A900: 43T … 43CT;
//  A900-A904: 1629T … 1629BT) and the June route S50-S48 over DMT; the real
//  ARPs and LSRPs of loco 1 (DN) and loco 2 (UP) in
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

int routeNamed(const StationKavach::Station &s, const QString &name)
{
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].name == name) return i;
    return -1;
}

// Station 527's KAVACH data with station_527.xml's circuits on its routes, S50-S48 over DMT,
// and two routes from S48 (main: 19 Normal; loop: 19 and P18 Reverse) for the locking.
StationKavach::Station station527()
{
    StationLayout::Layout fixture;
    StationKavach::Station s;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture);
    StationKavach::fromJson(fixture.kavach, &s);
    for (StationKavach::Route &r : s.routes) {
        if (r.entry == QLatin1String("S43")) r.trackCircuits = QStringList{ "43T", "43AT", "43BT", "43CT" };
        if (r.entry == QLatin1String("A900")) r.trackCircuits = QStringList{ "1629T", "1629AT", "1629BT" };
    }
    StationKavach::Signal s50;
    s50.name = QStringLiteral("S50");
    s50.footTag = 2;
    s50.aspect = 1;
    s.signalList.prepend(s50);
    StationKavach::Route r;
    r.name = QStringLiteral("S50-S48 DN");
    r.entry = QStringLiteral("S50");
    r.exit = QStringLiteral("S48");
    r.tags << StationKavach::RouteTag{ 0, 2, 0 } << StationKavach::RouteTag{ 1650, 12, 0 };
    r.trackCircuits = QStringList{ QStringLiteral("DMT") };
    s.routes << r;
    return s;
}

} // namespace

TEST_SUITE(session443)
{
    StationLayout::Layout l527;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527), "station_527.xml");
    const StationInterlock::Track track = StationInterlock::trackOf(l527);
    const StationKavach::Station s = station527();

    // ---- from the signal furthest ahead back ----
    StationKavach::State st = StationKavach::State::initial(s);
    st.interlocking = true;
    st.aspect.insert(QStringLiteral("A904"), 1);
    const QStringList first = StationInterlock::work(s, &st);
    CHECK(first == QStringList{ QStringLiteral("S43 Double Yellow (automatic: A900 Yellow ahead)") },
          QByteArray("A904 Red: A900 stays Yellow, S43 Double Yellow (") + first.join(" | ").toUtf8() + ")");
    CHECK(st.aspectOf(s, QStringLiteral("A900")) == 2 && st.aspectOf(s, QStringLiteral("S43")) == 10
              && st.aspectOf(s, QStringLiteral("S48")) == 2 && st.aspectOf(s, QStringLiteral("S50")) == 1,
          "S48 (a station route) and S50 untouched");
    CHECK(StationInterlock::work(s, &st).isEmpty(), "worked again, nothing changes");
    st.aspect.insert(QStringLiteral("A904"), 11);
    StationInterlock::work(s, &st);
    CHECK(st.aspectOf(s, QStringLiteral("A900")) == 11 && st.aspectOf(s, QStringLiteral("S43")) == 11,
          "A904 Green: A900 and S43 Green");

    // ---- the real run: loco 1 into 43T ----
    const QVector<In> ins = heard(linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap")),
                                  linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap")));
    QHash<int, StationKavach::Loco> locos;
    qint64 redAt = -1;
    QString said;
    for (const In &in : ins) {
        StationKavach::note(&locos[in.loco], in.raw);
        st.occupiedBy = StationInterlock::occupancy(track, locos.values()).by;
        for (const QString &e : StationInterlock::work(s, &st))
            if (e.startsWith(QStringLiteral("S43 Red")) && redAt < 0) { redAt = in.raw.value(QStringLiteral("ABS_LOCO_LOC")); said = e; }
    }
    CHECK(redAt >= 161900 && redAt < 161960 && said == QStringLiteral("S43 Red (automatic: 43T by loco 1)"),
          QByteArray("loco 1's front into 43T: S43 Red (") + said.toUtf8() + " at " + QByteArray::number(redAt) + ")");
    CHECK(st.aspectOf(s, QStringLiteral("S43")) == 1 && st.aspectOf(s, QStringLiteral("A900")) == 11,
          "at the end loco 1 is in 43BT / 43CT: S43 Red, A900 still Green (its section clear)");

    // ---- the section ahead of A900 occupied; then clear again ----
    st.occupied.insert(QStringLiteral("1629AT"));
    const QStringList occ = StationInterlock::work(s, &st);
    CHECK(occ.contains(QStringLiteral("A900 Red (automatic: 1629AT)")) && st.aspectOf(s, QStringLiteral("A900")) == 1,
          "1629AT occupied by hand: A900 Red");
    st.occupied.clear();
    st.occupiedBy.clear();
    StationInterlock::work(s, &st);
    CHECK(st.aspectOf(s, QStringLiteral("A900")) == 11 && st.aspectOf(s, QStringLiteral("S43")) == 11, "all clear: Green again");

    // ---- a route set from it, or interlocking off: the operator's ----
    const int s43 = routeNamed(s, QStringLiteral("S43-A900 DN Auto"));
    CHECK(StationInterlock::setRoute(s, &st, s43).isEmpty(), "a route set from S43");
    st.aspect.insert(QStringLiteral("S43"), 2);
    StationInterlock::work(s, &st);
    CHECK(st.aspectOf(s, QStringLiteral("S43")) == 2, "S43 with a route set: its aspect the operator's (Yellow kept)");
    st.interlocking = false;
    st.aspect.insert(QStringLiteral("A904"), 1);
    CHECK(StationInterlock::work(s, &st).isEmpty() && st.aspectOf(s, QStringLiteral("A900")) == 11, "interlocking off: A900 not worked");
}
