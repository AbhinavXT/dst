#include "testutil.h"

#include "fieldcatalog.h"
#include "fieldindex.h"
#include "logentry.h"
#include "logquery.h"
#include "testassertions.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

// =============================================================================
//  Session 305 — SIF 0533 v4.22 Section 1 (Standby mode) as test cases.
//
//  - field:NAME~"a phrase, with commas" matches: the quotes used to stay in
//    the value, so a DMI alarm flag could never be matched by its name.
//  - fieldmap.json: packet-scoped names for the DMI, ARP, DIP1 non-leading
//    and DIP2 isolation / EM cock inputs.
//  - testcases.json: the Section 1 cases, run over the real 2026-10-08
//    captures. Where no real frame exists (no cab off, no EM cock, no
//    Shunt/Reverse/NL/IS in replay/), a real DIP frame has the documented
//    pins changed, and the test says so.
// =============================================================================

namespace {

const char *k101300 = DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap";
const char *k103520 = DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_103520.cap";
const char *k105200 = DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap";
const char *k162008 = DL_SRC_DIR "/replay/2026-10-08/loco_2_1_08102026_162008.cap";
const char *k81     = DL_SRC_DIR "/replay/loco_81_1_14102026_110915.cap";

QStringList lines305(const char *path)
{
    QFile f(QString::fromUtf8(path));
    QStringList out;
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) out << QString::fromLatin1(f.readLine().trimmed());
    return out;
}

qint64 stamp305(const QString &line)
{
    QDateTime t = QDateTime::fromString(line.section(QLatin1Char(' '), 1, 1), Qt::ISODate);
    t.setTimeSpec(Qt::UTC);
    return t.toMSecsSinceEpoch();
}

LogEntryPtr entry305(const QString &line, qint64 ms = -1)
{
    auto e = LogEntryPtr::create();
    e->text = line;
    e->epochMs = ms < 0 ? stamp305(line) : ms;
    return e;
}

// Line n (1-based) of a capture, as the file has it.
QString at305(const char *path, int n)
{
    const QStringList l = lines305(path);
    return n >= 1 && n <= l.size() ? l.at(n - 1) : QString();
}

bool matches305(const QString &query, const QString &line, QString *err = nullptr)
{
    LogQuery q;
    if (!q.parse(query)) {
        if (err) *err = q.errorString();
        return false;
    }
    LogEntry e;
    e.text = line;
    return q.match(e);
}

QString refused305(const QString &query)
{
    LogQuery q;
    return q.parse(query) ? QString() : q.errorString();
}

// A real DIP line with some of its 32 bits changed (pin N is bit N-1,
// lsb-first). Used only where replay/ has no frame with that input state.
QString withBits305(const QString &line, const QVector<QPair<int, bool>> &bits)
{
    QStringList parts = line.split(QLatin1Char(' '));
    for (const auto &b : bits) {
        const int byteIx = 3 + b.first / 8;
        bool ok = false;
        int v = parts.value(byteIx).toInt(&ok, 16);
        if (b.second) v |= 1 << (b.first % 8);
        else          v &= ~(1 << (b.first % 8));
        parts[byteIx] = QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

const TestAssertion *find305(const TestAssertionEngine &en, const QString &id)
{
    for (const TestAssertion &a : en.assertions())
        if (a.id == id) return &a;
    return nullptr;
}

QString clauseOf305(const QString &id) { return id.section(QLatin1Char('/'), 0, 0); }

bool section1(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^1\\.\\d+(/|$)"));
    return re.match(id).hasMatch();
}

// Run a whole capture through a fresh engine; no deadline is forced at the
// end, as a replay that simply stops does not expire anything.
void runFile305(TestAssertionEngine &en, const char *path)
{
    for (const QString &l : lines305(path))
        if (l.startsWith(QLatin1Char('@'))) en.observe(entry305(l));
}

// One engine per capture, run once and shared by the checks below.
const TestAssertionEngine &ranOn305(const char *path)
{
    static QHash<QString, QSharedPointer<TestAssertionEngine>> done;
    auto &en = done[QString::fromUtf8(path)];
    if (!en) {
        en = QSharedPointer<TestAssertionEngine>::create();
        en->load(QStringLiteral(DL_SRC_DIR "/testcases.json"));
        runFile305(*en, path);
    }
    return *en;
}

}  // namespace

TEST_SUITE(session305)
{
    FieldCatalog &cat = FieldCatalog::instance();
    const bool hadCatalog = cat.isLoaded();
    QString err;
    CHECK(cat.load(QStringLiteral(DL_SRC_DIR "/fieldmap.json"), &err), "the repository's fieldmap.json loads");

    const QString dmiStandstill = at305(k101300, 3148);   // Standstill Protection; Select SR / Shunt
    const QString dmiRollback   = at305(k101300, 3807);   // Rollback Protection
    const QString dmiSb         = at305(k101300, 2201);   // SB, speeds 0, cab 1
    const QString dmiTrainType  = at305(k101300, 3006);   // context Train Type Selected
    const QString dmiEoa        = at305(k103520, 14);     // End of Authority (FS)
    const QString dllogEoa      = at305(k81, 20);         // DLLOG LP-OCIP alert, End of Authority
    const QString dmiSf         = at305(k105200, 94);     // System Failure + System Fault alarm
    const QString arpSb         = at305(k101300, 2596);   // ARP Stand_By
    const QString arpSr1        = at305(k101300, 3454);   // ARP Staff_Responsible, 0 km/h
    const QString arpSr2        = at305(k101300, 3495);
    const QString arpRecv       = at305(k105200, 9);      // another loco's ARP, a non-zero LOCO_MODE
    const QString dip1          = at305(k101300, 2);      // cab 1 active, forward
    const QString dip2          = at305(k101300, 6);      // isolation normal, EM cock open
    CHECK(dmiStandstill.startsWith(QLatin1String("@dmi_")) && dmiRollback.startsWith(QLatin1String("@dmi_"))
              && dmiSb.startsWith(QLatin1String("@dmi_")) && dmiTrainType.startsWith(QLatin1String("@dmi_"))
              && dmiEoa.startsWith(QLatin1String("@dmi_")) && dllogEoa.startsWith(QLatin1String("@dllog_"))
              && dmiSf.startsWith(QLatin1String("@dmi_")) && arpSb.startsWith(QLatin1String("@arp_"))
              && arpSr1.startsWith(QLatin1String("@arp_")) && arpSr2.startsWith(QLatin1String("@arp_"))
              && arpRecv.startsWith(QLatin1String("@arprecv_")) && dip1.startsWith(QLatin1String("@dip1_"))
              && dip2.startsWith(QLatin1String("@dip2_")),
          "fixture: the real lines from replay/2026-10-08 and replay/loco_81");

    // ---- A quoted phrase matches one flag of a flag set ----------------------
    {
        LogEntry e;
        e.text = dmiStandstill;
        CHECK(FieldIndex::value(e, QStringLiteral("alarm_code"))
                  == QLatin1String("Standstill Protection, Brake Applied; Select Staff Responsible / Shunt mode"),
              "a flag set renders as its names joined by '; '");
        CHECK(matches305(QStringLiteral("field:dmi_alarm~\"Standstill Protection, Brake Applied\""), dmiStandstill),
              "~\"Standstill Protection, Brake Applied\": a flag name with a comma matches");
        CHECK(matches305(QStringLiteral("field:dmi_alarm~\"Select Staff Responsible / Shunt mode\""), dmiStandstill),
              "~\"Select Staff Responsible / Shunt mode\": the second flag, with a slash, matches");
        CHECK(!matches305(QStringLiteral("field:dmi_alarm~\"Rollback Protection, Brake Applied\""), dmiStandstill),
              "a flag that is not set does not match");
        CHECK(matches305(QStringLiteral("field:dmi_alarm~\"Rollback Protection, Brake Applied\""), dmiRollback)
                  && !matches305(QStringLiteral("field:dmi_alarm~\"Standstill Protection, Brake Applied\""), dmiRollback),
              "and the other way round on the Rollback frame");
        CHECK(matches305(QStringLiteral("field:dmi_alarm=\"Standstill Protection, Brake Applied\""), dmiStandstill),
              "with = a quoted value is one value, not a list split at its comma");
        CHECK(matches305(QStringLiteral("field:dmi_alarm~standstill"), dmiStandstill),
              "an unquoted word still matches, as before");
        CHECK(matches305(QStringLiteral("field:dmi_loco_mode=system_failure,stand_by"), dmiStandstill),
              "an unquoted list is still a list");
        CHECK(matches305(QStringLiteral("field:dmi_context~\"Train Type Selected\""), dmiTrainType),
              "a context flag (dmiCtx) by its name");
    }

    // ---- Packet scope: alarm_code is in DMI and in DLLOG ---------------------
    {
        const QString raw = QStringLiteral("field:alarm_code~\"End of Authority\"");
        const QString scoped = QStringLiteral("field:dmi_alarm~\"End of Authority\"");
        CHECK(matches305(raw, dmiEoa) && matches305(raw, dllogEoa), "the raw name matches the DMI and the DLLOG alert");
        CHECK(matches305(scoped, dmiEoa) && !matches305(scoped, dllogEoa), "dmi_alarm matches only the DMI");
        LogEntry recv;
        recv.text = arpRecv;
        CHECK(FieldIndex::number(recv, QStringLiteral("LOCO_MODE"), nullptr) > 0
                  && !matches305(QStringLiteral("field:arp_loco_mode>0"), arpRecv),
              "arp_loco_mode does not take another loco's ARP (arprecv)");
    }

    // ---- The new names decode on real frames ----------------------------------
    {
        CHECK(matches305(QStringLiteral("field:arp_loco_mode=stand_by"), arpSb), "arp_loco_mode on a real ARP");
        CHECK(matches305(QStringLiteral("field:arp_loco_mode=staff_responsible field:arp_train_speed=0"), arpSr1),
              "arp_train_speed on a real ARP");
        CHECK(!matches305(QStringLiteral("field:loco_mode=stand_by"), arpSb), "loco_mode stays LSRP's");
        CHECK(matches305(QStringLiteral("field:dmi_loco_mode=stand_by field:dmi_train_speed=0 "
                                        "field:dmi_speed_limit_permissible=0 field:dmi_active_cab=cab1"), dmiSb),
              "dmi_loco_mode, dmi_train_speed, dmi_speed_limit_permissible, dmi_active_cab");
        CHECK(matches305(QStringLiteral("field:dmi_brake_type=emergency_brake field:dmi_train_speed=14"), dmiStandstill),
              "dmi_brake_type on the standstill-protection frame: EB at 14 km/h");
        CHECK(matches305(QStringLiteral("field:dmi_loco_mode=system_failure "
                                        "field:dmi_alarm~\"System Fault, Isolate or Restart KAVACH\""), dmiSf),
              "System Failure with its message, on a real DMI frame");
        CHECK(matches305(QStringLiteral("field:dmi1_leading_non_leading_nc=leading field:dmi1_leading_non_leading_no=leading "
                                        "field:dmi2_leading_non_leading_nc=leading field:dmi2_leading_non_leading_no=leading"), dip1),
              "normal running: both leading/non-leading inputs read 'leading' (NC=0, NO=1)");
        CHECK(matches305(QStringLiteral("field:kavach_isolation_fb_no=normal field:kavach_isolation_fb_nc=normal "
                                        "field:sifa_isolation_cock_fb_no=open field:sifa_isolation_cock_fb_nc=open"), dip2),
              "normal running: isolation normal (NO=0, NC=1), EM cock open (NO=0, NC=1)");
        // The other states have no real frame: the pins flipped on the real one.
        const QString isolated = withBits305(dip2, { { 0, true }, { 1, false } });
        CHECK(matches305(QStringLiteral("field:kavach_isolation_fb_no=isolated field:kavach_isolation_fb_nc=isolated"), isolated),
              "pins 1/2 flipped: isolated");
        const QString cockClosed = withBits305(dip2, { { 8, true }, { 9, false } });
        CHECK(matches305(QStringLiteral("field:sifa_isolation_cock_fb_no=closed field:sifa_isolation_cock_fb_nc=closed"), cockClosed)
                  && matches305(QStringLiteral("field:kavach_isolation_fb_no=normal"), cockClosed),
              "pins 9/10 flipped: EM cock closed, and nothing else changed");
        const QString nl2 = withBits305(dip1, { { 14, true }, { 15, false } });
        CHECK(matches305(QStringLiteral("field:dmi2_leading_non_leading_nc=non_leading field:dmi2_leading_non_leading_no=non_leading "
                                        "field:dmi1_leading_non_leading_no=leading"), nl2),
              "pins 15/16 flipped: cab 2 non-leading, cab 1 still leading");
    }

    // ---- A misspelt symbol is refused at parse time --------------------------
    {
        CHECK(refused305(QStringLiteral("field:dmi_brake_type>=normal_brak")).contains(QLatin1String("normal_brake")),
              "dmi_brake_type>=normal_brak refused, naming normal_brake");
        CHECK(!refused305(QStringLiteral("field:sifa_isolation_cock_fb_no=closd")).isEmpty(), "EM cock 'closd' refused");
        CHECK(!refused305(QStringLiteral("field:arp_loco_mode=stand_bye")).isEmpty(), "arp_loco_mode=stand_bye refused");
        CHECK(!refused305(QStringLiteral("field:dmi2_leading_non_leading_no=nonleading")).isEmpty(), "non-leading misspelt refused");
        CHECK(!refused305(QStringLiteral("field:dmi_active_cab=cab3")).isEmpty(), "dmi_active_cab=cab3 refused");
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([{"id":"1.x","title":"t","sequence":[
                {"match":"field:dmi_loco_mode=stand_by"},
                {"match":"field:kavach_isolation_fb_no=isolatd","within_ms":1000}]}])", &err),
              "a case with a misspelt value loads");
        CHECK(en.assertions().size() == 1 && !en.assertions().first().queryOk
                  && en.assertions().first().queryError.contains(QLatin1String("isolated")),
              "and is kept, flagged, with the known values named");
    }

    // ---- The Section 1 cases ----------------------------------------------------
    TestAssertionEngine all;
    CHECK(all.load(QStringLiteral(DL_SRC_DIR "/testcases.json"), &err), "testcases.json loads");
    QSet<QString> clauses;
    int sec1 = 0, broken = 0;
    for (const TestAssertion &a : all.assertions()) {
        if (!section1(a.id)) continue;
        ++sec1;
        clauses.insert(clauseOf305(a.id));
        if (!a.queryOk) {
            ++broken;
            std::printf("  broken %s: %s\n", qPrintable(a.id), qPrintable(a.queryError));
        }
    }
    CHECK(sec1 == 76, "76 Section 1 cases after expansion");
    CHECK(broken == 0, "every Section 1 case parses");
    {
        QSet<QString> want;
        for (int i = 1; i <= 33; ++i) want.insert(QStringLiteral("1.%1").arg(i));
        for (const char *manual : { "1.3", "1.11", "1.24", "1.31" }) want.remove(QString::fromLatin1(manual));
        CHECK(clauses == want, "every clause but the four manual ones (1.3, 1.11, 1.24, 1.31) has a case");
    }
    {
        // Every clause id is one the catalog (the SIF as printed) has in section 1.
        QFile f(QStringLiteral(DL_SRC_DIR "/test_catalog/sif0533_v4_22.json"));
        CHECK(f.open(QIODevice::ReadOnly), "the SIF catalog opens");
        QSet<QString> catalog;
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("cases")).toArray())
            catalog.insert(v.toObject().value(QStringLiteral("id")).toString());
        bool allThere = true;
        for (const QString &c : clauses) allThere = allThere && catalog.contains(c);
        CHECK(allThere, "every case id is a clause of the catalog");
    }
    {
        // Every quoted alarm / context name is a flag the schema has: a typo
        // there would parse and never match.
        QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
        CHECK(f.open(QIODevice::ReadOnly), "kavach.xml opens");
        const QString xml = QString::fromUtf8(f.readAll());
        static const QRegularExpression flagRe(QStringLiteral("field:dmi_(?:alarm|context)~\"([^\"]+)\""));
        QStringList unknown;
        int named = 0;
        for (const TestAssertion &a : all.assertions()) {
            if (!section1(a.id)) continue;
            QStringList texts;
            for (const auto &st : a.steps) {
                texts << st.matchText;
                for (const auto &p : st.parts) texts << p.matchText;
            }
            for (const QString &t : texts) {
                auto it = flagRe.globalMatch(t);
                while (it.hasNext()) {
                    const QString flag = it.next().captured(1);
                    ++named;
                    const bool prefix = flag == QLatin1String("Brake Test - ");
                    if (!prefix && !xml.contains(QStringLiteral("<flag name=\"%1\"/>").arg(flag)))
                        unknown << flag;
                }
            }
        }
        if (!unknown.isEmpty()) std::printf("  not a schema flag: %s\n", qPrintable(unknown.join(QStringLiteral(" | "))));
        CHECK(named > 30 && unknown.isEmpty(), "every DMI message a case names is a dmiAlarm / dmiCtx flag, spelt as the schema spells it");
    }

    // ---- Real captures: what each case reports ----------------------------------
    {
        const TestAssertionEngine &en = ranOn305(k101300);
        auto obs = [&](const char *id) { const TestAssertion *a = find305(en, QString::fromLatin1(id)); return a && a->observed(); };
        CHECK(obs("1.2"), "101300: 1.2 OBSERVED (SB, cab 1, 14 km/h: Standstill Protection and EB, 10:15:48)");
        const TestAssertion *a2 = find305(en, QStringLiteral("1.2"));
        CHECK(a2 && a2->firstText.contains(QLatin1String("10:15:48")) && a2->firstText.contains(QLatin1String("10:15:50")),
              "1.2's evidence: the movement at 10:15:48, the reaction by 10:15:50");
        CHECK(obs("1.4"), "101300: 1.4 OBSERVED (self test 10:15:03 → success 10:15:20 → brakes 10:15:26 → config prompt)");
        CHECK(obs("1.6") && obs("1.7") && obs("1.32") && obs("1.20"),
              "101300: 1.6, 1.7, 1.32, 1.20 OBSERVED");
        const TestAssertion *steps = find305(en, QStringLiteral("1.4/STEPS"));
        CHECK(steps && !steps->observed() && steps->violated == 0 && steps->stepAt == 0,
              "1.4/STEPS: NOT ANSWERED, waiting for the brake test's step messages (the DMI log shows none)");
        const TestAssertion *f5 = find305(en, QStringLiteral("1.5"));
        CHECK(f5 && f5->stepAt == -1 && f5->violated == 0, "1.5 is not armed by a brake test that succeeded");
    }
    {
        const TestAssertionEngine &en = ranOn305(k105200);
        const TestAssertion *sf = find305(en, QStringLiteral("1.29"));
        CHECK(sf && sf->observed() && sf->firstText.contains(QLatin1String("10:52:36")),
              "105200: 1.29 OBSERVED (SB → System Failure with its message, 10:52:36)");
        const TestAssertion *c30 = find305(en, QStringLiteral("1.30"));
        CHECK(c30 && c30->satisfied == 1 && c30->violated == 2 && !c30->observed(),
              "105200: 1.30 answered once (SF 10:56:27 → SB → prompt 10:56:34); its two findings fall in gaps of the DMI log");
        const TestAssertion *c20 = find305(en, QStringLiteral("1.20"));
        CHECK(c20 && c20->violated == 1 && c20->violationText.contains(QLatin1String("10:52:20")),
              "105200: 1.20 finding: configured at 11:00:48, no SR / Shunt prompt in the DMI log before SR");
        const TestAssertion *c4 = find305(en, QStringLiteral("1.4"));
        CHECK(c4 && c4->violated == 2 && c4->satisfied == 0,
              "105200: 1.4 not answered twice (System Failure after the self test; the DMI log stops at 11:08:45)");
    }
    {
        // No real frame for these: on every 2026-10-08/14 capture they stay
        // not observed, and raise no finding.
        const QStringList quiet = {
            QStringLiteral("1.1"), QStringLiteral("1.33"), QStringLiteral("1.5"), QStringLiteral("1.8"),
            QStringLiteral("1.9"), QStringLiteral("1.10"), QStringLiteral("1.12"), QStringLiteral("1.13"),
            QStringLiteral("1.14"), QStringLiteral("1.15"), QStringLiteral("1.16"), QStringLiteral("1.17"),
            QStringLiteral("1.18"), QStringLiteral("1.19"), QStringLiteral("1.21"), QStringLiteral("1.22"),
            QStringLiteral("1.23"), QStringLiteral("1.25"), QStringLiteral("1.26"), QStringLiteral("1.27"),
            QStringLiteral("1.28") };
        bool silent = true;
        for (const char *path : { k101300, k103520, k105200, k162008, k81 }) {
            const TestAssertionEngine &en = ranOn305(path);
            for (const TestAssertion &a : en.assertions()) {
                if (!quiet.contains(clauseOf305(a.id))) continue;
                if (a.satisfied || a.violated) {
                    silent = false;
                    std::printf("  %s moved on %s\n", qPrintable(a.id), path);
                }
            }
        }
        CHECK(silent, "the cases with no real frame stay quiet on all five captures (no false finding)");
    }

    // ---- Spliced: real mode frames, a real DIP frame with the input changed ----
    {
        const QString cabsOff = withBits305(dip1, { { 23, false }, { 26, false } });
        const QString cockClosed = withBits305(dip2, { { 8, true }, { 9, false } });
        CHECK(matches305(QStringLiteral("field:cab1_active=inactive field:cab2_active=inactive"), cabsOff),
              "fixture: pin 24 cleared on the real DIP1, both cabs inactive");
        auto run = [&](const QStringList &seq, int stepMs = 1000) {
            auto en = QSharedPointer<TestAssertionEngine>::create();
            en->load(QStringLiteral(DL_SRC_DIR "/testcases.json"), &err);
            qint64 t = stamp305(arpSr1);
            for (const QString &l : seq) { en->observe(entry305(l, t)); t += stepMs; }
            return en;
        };
        {
            auto en = run({ arpSr1, arpSr2, cabsOff, cabsOff, arpSb, dmiSb, dip1, arpSr2 });
            CHECK(find305(*en, QStringLiteral("1.14/SR")) && find305(*en, QStringLiteral("1.14/SR"))->observed(),
                  "1.14/SR: SR at standstill, cabs off, SB on ARP and DMI");
            CHECK(find305(*en, QStringLiteral("1.15/SR")) && find305(*en, QStringLiteral("1.15/SR"))->observed(),
                  "1.15/SR: and back to SR when cab 1 is active again");
            CHECK(find305(*en, QStringLiteral("1.1")) && find305(*en, QStringLiteral("1.1"))->observed(),
                  "1.1: no cab, SB reported and shown");
            CHECK(!find305(*en, QStringLiteral("1.14/FS"))->observed() && !find305(*en, QStringLiteral("1.14/SH"))->observed(),
                  "the other modes' variants are not satisfied by an SR run");
        }
        {
            QStringList seq = { arpSr1, cabsOff };
            for (int i = 0; i < 12; ++i) seq << arpSr2;
            auto en = run(seq);
            const TestAssertion *a = find305(*en, QStringLiteral("1.14/SR"));
            CHECK(a && a->violated == 1 && !a->observed(),
                  "1.14/SR: cabs off and SR still reported 12 s later: NOT ANSWERED");
        }
        {
            auto en = run({ arpSr1, cockClosed, arpSb, dip1, dip2, arpSr2 });
            CHECK(find305(*en, QStringLiteral("1.16/SR"))->observed(), "1.16/SR: EM cock closed at standstill, SB, cab 1 active");
            CHECK(find305(*en, QStringLiteral("1.19/SR"))->observed(), "1.19/SR: EM cock opened, back to SR");
            CHECK(find305(*en, QStringLiteral("1.17/SR"))->observed(), "1.17/SR: the same evidence");
        }
    }

    if (!hadCatalog) cat.clear();
}
