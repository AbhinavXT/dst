#include "testutil.h"
#include "flasherfakeboard.h"

#include "flashercore.h"
#include "flasherflashingpage.h"
#include "flasherqueuepage.h"
#include "flashersummarypage.h"
#include "flasherwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <memory>

// =============================================================================
//  Session 291 — the Firmware Flasher flashes several VCCs at once.
//
//  The VCC IP field takes a list or a range; each VCC gets its own worker,
//  engine and socket, so its handshake, first pass, resends and rate run
//  in parallel with the others'. Against four FakeBoards (one per "VCC", on
//  their own ports of 127.0.0.1): one board loses every 7th DATA and needs
//  resends, one rejects the image; the transfers overlap in time, each
//  board's resends are its own, and the Summary, history and Try again
//  name each VCC.
// =============================================================================

using namespace FlasherTest;

namespace {

bool runParallel(FlasherWindow &window, int timeoutMs, int afterMs, const std::function<void()> &duringRun)
{
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&window, &FlasherWindow::batchFinished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    if (!window.startBatch(false)) {
        return false;
    }
    if (duringRun) {
        QTimer::singleShot(afterMs, &loop, duringRun);
    }
    if (!finished) {
        loop.exec();
    }
    return finished;
}

bool hasItem(const Flasher::PreflightReport &report, const QString &text)
{
    for (const Flasher::PreflightItem &item : report.items) {
        if (item.text.contains(text)) return true;
    }
    return false;
}

Flasher::PreflightInput vccInput(const QString &ips)
{
    Flasher::PreflightInput input;
    input.vccIp = ips;
    input.port = 50001;
    input.adapterOnSubnet = true;
    input.subnetText = QStringLiteral("192.168.25.x");
    Flasher::PreflightRow row;
    row.cardType = Flasher::CardVcc;
    row.ticked = true;
    row.hasPath = true;
    row.loaded = true;
    row.fileName = QStringLiteral("LKAVACH_v1.2.9.appimage");
    row.nameCheck = Flasher::checkImageName(row.fileName, Flasher::CardVcc);
    input.rows.append(row);
    return input;
}

}  // namespace

TEST_SUITE(session291)
{
    // ---- the list ------------------------------------------------------------------
    {
        const Flasher::VccTargetList one = Flasher::parseVccTargets(QStringLiteral("192.168.25.168"));
        CHECK(one.ok() && one.targets.size() == 1 && one.targets.first().ip == QLatin1String("192.168.25.168"),
              "one address, as before");
        const Flasher::VccTargetList two = Flasher::parseVccTargets(QStringLiteral(" 192.168.25.168, 192.168.25.172 "));
        CHECK(two.ok() && two.targets.size() == 2 && two.targets.at(1).ip == QLatin1String("192.168.25.172"),
              "a comma list, in the order typed");
        const Flasher::VccTargetList range = Flasher::parseVccTargets(QStringLiteral("192.168.25.168-175"));
        CHECK(range.ok() && range.targets.size() == 8 && range.targets.first().ip == QLatin1String("192.168.25.168")
                  && range.targets.last().ip == QLatin1String("192.168.25.175"),
              "a range of the last number: .168-175 is eight VCCs");
        const Flasher::VccTargetList ported = Flasher::parseVccTargets(QStringLiteral("127.0.0.1:50012; 127.0.0.1:50013"));
        CHECK(ported.ok() && ported.targets.size() == 2 && ported.targets.first().port == 50012
                  && ported.targets.first().text(50001) == QLatin1String("127.0.0.1:50012"),
              "ip:port for the simulator, and ; or spaces between");
        const Flasher::VccTargetList nine = Flasher::parseVccTargets(QStringLiteral("192.168.25.168-175 192.168.25.180"));
        CHECK(!nine.ok() && nine.error.contains(QLatin1String("at most 8")), QByteArray("nine refused: ") + nine.error.toUtf8());
        const Flasher::VccTargetList twice = Flasher::parseVccTargets(QStringLiteral("192.168.25.168, 192.168.25.168"));
        CHECK(!twice.ok() && twice.error.contains(QLatin1String("listed twice")),
              "the same VCC twice refused: two transfers to one updater");
        const Flasher::VccTargetList badInList = Flasher::parseVccTargets(QStringLiteral("192.168.25.168, 192.168.25"));
        CHECK(!badInList.ok() && badInList.error.contains(QStringLiteral("“192.168.25” in the VCC list")),
              "a bad entry in a list is named");
        CHECK(Flasher::parseVccTargets(QStringLiteral("192.168.25")).error
                  == QLatin1String("VCC IP address is not a valid IPv4 address"),
              "one bad address: the message the flasher always gave");
        CHECK(!Flasher::parseVccTargets(QStringLiteral("192.168.25.250-255")).ok()
                  && !Flasher::parseVccTargets(QStringLiteral("192.168.25.170-168")).ok(),
              "a range into .255 or running backwards is refused");
    }

    // ---- pre-flight ------------------------------------------------------------------
    {
        const Flasher::PreflightReport four = Flasher::evaluatePreflight(vccInput(QStringLiteral("192.168.25.168-171")));
        CHECK(four.ready && hasItem(four, QStringLiteral("4 VCCs, flashed in parallel")), "four VCCs: ready, and says so");
        CHECK(hasItem(four, QStringLiteral("power-cycle all 4 chassis")), "the procedure line names all four chassis");

        Flasher::PreflightInput input = vccInput(QStringLiteral("192.168.25.168-171"));
        input.rows[0].cardType = Flasher::CardInput;
        input.rows[0].fileName = QStringLiteral("KAVACH_Input_Card_v5.appimage");
        input.rows[0].nameCheck = Flasher::checkImageName(input.rows[0].fileName, Flasher::CardInput);
        const Flasher::PreflightReport input4 = Flasher::evaluatePreflight(input);
        CHECK(!input4.ready && input4.firstBlocker.contains(QLatin1String("only the VCC card")),
              "an IOA card with several VCC addresses is refused: it goes through its VCC's relay");

        Flasher::PreflightInput off = vccInput(QStringLiteral("192.168.25.168, 192.168.26.5"));
        off.adapterOnSubnet = false;
        off.offSubnetTargets = QStringList{ QStringLiteral("192.168.26.5") };
        const Flasher::PreflightReport offReport = Flasher::evaluatePreflight(off);
        CHECK(hasItem(offReport, QStringLiteral("No adapter on the subnet of 192.168.26.5")),
              "a VCC no adapter reaches is named");

        const Flasher::PreflightReport tooMany =
            Flasher::evaluatePreflight(vccInput(QStringLiteral("192.168.25.160-170")));
        CHECK(!tooMany.ready && tooMany.firstBlocker.contains(QLatin1String("at most 8")), "eleven blocks Flash");
    }

    // ---- the parallel plan ------------------------------------------------------------
    {
        QVector<Flasher::BatchEntry> entries;
        for (int octet = 168; octet <= 171; ++octet) {
            Flasher::BatchEntry entry;
            entry.cardType = Flasher::CardVcc;
            entry.targetIp = QStringLiteral("192.168.25.%1").arg(octet);
            entries.append(entry);
        }
        Flasher::BatchPlan plan(entries, true);
        const QVector<int> started = plan.startAll();
        CHECK(started.size() == 4 && plan.runningCount() == 4 && plan.isParallel(), "all four run at once");
        kflash::FlashResult ok;
        ok.success = true;
        kflash::FlashResult fail;
        fail.message = "No STATUS for 2500 ms - aborting";
        plan.finishEntry(2, fail, false);
        CHECK(plan.runningCount() == 3 && !plan.isFinished(), "one failing stops no other (stop-on-first-failure is for one chassis)");
        plan.finishEntry(0, ok, false);
        plan.finishEntry(1, ok, true);
        CHECK(!plan.isFinished(), "not finished while one still runs");
        plan.finishEntry(3, ok, false);
        CHECK(plan.isFinished() && plan.succeededCount() == 3 && plan.failedCount() == 1, "finished with the last one");
        CHECK(plan.entries().at(1).outcome == Flasher::CardOutcome::AlreadyHeld, "already held, per VCC");
        const Flasher::Verdict verdict = Flasher::batchVerdict(plan);
        CHECK(verdict.headline == QLatin1String("3 of 4 VCCs updated")
                  && verdict.explanation.contains(QLatin1String("VCC 192.168.25.170 failed")),
              QByteArray("the verdict names the VCC: ") + verdict.headline.toUtf8() + " / " + verdict.explanation.toUtf8());
        const Flasher::HistoryRecord record = Flasher::makeHistoryRecord(plan.entries().at(2), QStringLiteral("B"),
                                                                         QStringLiteral("op"), QStringLiteral("Bench"),
                                                                         QStringLiteral("192.168.25.168-171"));
        CHECK(record.vccIp == QLatin1String("192.168.25.170"), "history keeps each VCC's own address");
        const QString report = Flasher::batchReportText(plan, QStringLiteral("B"), QStringLiteral("op"),
                                                        QStringLiteral("Bench"), QStringLiteral("192.168.25.168-171"), 50001);
        CHECK(report.contains(QLatin1String("Targets   : 4 VCCs (192.168.25.168-171), port 50001"))
                  && report.contains(QLatin1String("---- VCC 192.168.25.170 (ID 1) ----")),
              "the report lists the targets and a section per VCC");

        Flasher::BatchPlan allOk(entries, true);
        for (int index : allOk.startAll()) allOk.finishEntry(index, ok, false);
        CHECK(Flasher::batchVerdict(allOk).headline == QLatin1String("All 4 VCCs updated"), "all four: \"All 4 VCCs updated\"");
    }

    // ---- the real window, four fake VCCs ---------------------------------------------
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");
    std::vector<std::unique_ptr<FakeBoard>> boards;
    QStringList targets;
    for (int index = 0; index < 4; ++index) {
        boards.emplace_back(new FakeBoard);
        targets << QStringLiteral("127.0.0.1:%1").arg(boards.back()->port());
    }
    boards[1]->dropEvery = 7;                   // a lossy link: resends needed
    boards[3]->rejectCardType = Flasher::CardVcc; // rejects the image
    const QString vccPath = writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                      patternBytes(400 * 1450 + 33, 291));
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("Four locos");
        profile.vccIp = targets.join(QStringLiteral(", "));
        profile.port = 9;
        profile.defaultImages.insert(Flasher::CardVcc, vccPath);
        profile.tuning.poll_give_up_ms = 1500;
        profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
        store.upsert(profile, store.names().first());
        store.setActiveName(profile.name);
        store.save();
    }

    FlasherWindow window(nullptr, temp.path());
    Flasher::HistoryLog history(window.historyPath());
    window.queuePage()->selectCard(Flasher::CardVcc);
    const QVector<Flasher::BatchEntry> queued = window.queuePage()->batchEntries();
    CHECK(queued.size() == 4 && queued.at(2).targetIp == QLatin1String("127.0.0.1")
              && queued.at(2).targetPort == boards[2]->port(),
          "the VCC row with four addresses: four entries, each its own target");
    CHECK(window.queuePage()->preflight().ready, "and ready");

    QString waitText;
    int rowsShown = 0;
    bool switched = false;
    const bool finished = runParallel(window, 40000, 600, [&]() {
        waitText = window.flashingPage()->waitText();
        rowsShown = window.flashingPage()->rowCount();
        window.flashingPage()->showEntry(2);
        switched = window.flashingPage()->cardTitle() == QStringLiteral("VCC %1").arg(targets.at(2));
        for (auto &board : boards) board->powerOn();
    });
    CHECK(finished, "the four-VCC run finishes");
    CHECK(window.workerCount() >= 4, QByteArray("one worker per VCC: ") + QByteArray::number(window.workerCount()));
    CHECK(waitText.startsWith(QLatin1String("Power-cycle all 4 chassis now")) && waitText.contains(QLatin1String("4 of 4 still waiting")),
          QByteArray("the prompt asks for all four chassis: ") + waitText.toUtf8());
    CHECK(rowsShown == 4 && switched, "a row per VCC; clicking one shows it in the centre");

    const Flasher::BatchPlan &plan = window.plan();
    CHECK(plan.size() == 4, "four entries ran");
    if (plan.size() == 4) {
        CHECK(plan.entries().at(0).outcome == Flasher::CardOutcome::Accepted
                  && plan.entries().at(1).outcome == Flasher::CardOutcome::Accepted
                  && plan.entries().at(2).outcome == Flasher::CardOutcome::Accepted,
              "three VCCs verified");
        CHECK(plan.entries().at(3).outcome == Flasher::CardOutcome::Failed && Flasher::isImageFail(plan.entries().at(3).result),
              "the fourth rejected its image (IMAGE_FAIL), on its own");
        CHECK(plan.entries().at(1).result.blocks_resent > 0,
              QByteArray("the lossy VCC was repaired by resends: ") + QByteArray::number(plan.entries().at(1).result.blocks_resent));
        CHECK(plan.entries().at(0).result.blocks_resent == 0 && plan.entries().at(2).result.blocks_resent == 0,
              "and its resends were its own: the clean VCCs resent nothing");
        bool ownLogs = true;
        for (const QString &line : plan.entries().at(0).log) {
            if (line.contains(targets.at(1))) ownLogs = false;
        }
        CHECK(ownLogs && !plan.entries().at(0).log.isEmpty(), "each VCC's history log holds only its own lines");
    }
    // Overlap: every board had its first DATA before any board had its last.
    qint64 latestFirst = -1;
    qint64 earliestLast = -1;
    for (int index = 0; index < 3; ++index) {
        latestFirst = qMax(latestFirst, boards[index]->firstDataMs);
        earliestLast = earliestLast < 0 ? boards[index]->lastDataMs : qMin(earliestLast, boards[index]->lastDataMs);
    }
    CHECK(latestFirst >= 0 && latestFirst < earliestLast,
          QByteArray("the transfers ran at the same time (last first-DATA ") + QByteArray::number(latestFirst)
              + " ms < first last-DATA " + QByteArray::number(earliestLast) + " ms)");
    CHECK(window.summaryPage()->headline() == QLatin1String("3 of 4 VCCs updated"),
          QByteArray("the Summary: ") + window.summaryPage()->headline().toUtf8());
    const QList<Flasher::HistoryRecord> records = history.readAll();
    CHECK(records.size() == 4 && records.at(3).result == QLatin1String("Failed"), "four history records");
    {
        Flasher::ProfileStore store(window.profilesPath());
        store.load();
        CHECK(store.profile(QStringLiteral("Four locos")).lastSettledRateKBps == 0.0,
              "a shared link's rates are not saved as the next start rate");
    }

    // Try again: only the VCC not updated.
    QMetaObject::invokeMethod(window.summaryPage(), "retryRequested");
    QCoreApplication::processEvents();
    CHECK(window.queuePage()->vccIp() == targets.at(3) && window.currentPage() == FlasherWindow::QueuePageIndex,
          QByteArray("Try again lists only the VCC that failed: ") + window.queuePage()->vccIp().toUtf8());

    // ---- eight at once, the most the list takes --------------------------------------
    {
        std::vector<std::unique_ptr<FakeBoard>> eight;
        QStringList eightTargets;
        for (int index = 0; index < Flasher::kMaxParallelVccs; ++index) {
            eight.emplace_back(new FakeBoard);
            eight.back()->dropEvery = 11 + index;   // every link a little lossy, each differently
            eightTargets << QStringLiteral("127.0.0.1:%1").arg(eight.back()->port());
        }
        window.queuePage()->setVccIpText(eightTargets.join(QStringLiteral(", ")));
        window.queuePage()->selectCard(Flasher::CardVcc);
        const bool done = runParallel(window, 40000, 300, [&]() {
            for (auto &board : eight) board->powerOn();
        });
        int verified = 0;
        int repaired = 0;
        for (const Flasher::BatchEntry &entry : window.plan().entries()) {
            verified += entry.outcome == Flasher::CardOutcome::Accepted ? 1 : 0;
            repaired += entry.result.blocks_resent > 0 ? 1 : 0;
        }
        CHECK(done && window.plan().size() == 8 && verified == 8,
              QByteArray("eight VCCs at once, all verified: ") + QByteArray::number(verified));
        CHECK(repaired == 8 && window.workerCount() >= 8, "each repaired its own losses, on its own worker");
        CHECK(window.summaryPage()->headline() == QLatin1String("All 8 VCCs updated"), "\"All 8 VCCs updated\"");
    }

    // ---- abort stops all of them ------------------------------------------------------
    for (auto &board : boards) board->powerOff();
    window.queuePage()->setVccIpText(targets.join(QStringLiteral(", ")));
    window.queuePage()->selectCard(Flasher::CardVcc);
    const bool aborted = runParallel(window, 20000, 400, [&]() { window.abortCurrent(); });
    CHECK(aborted, "an aborted four-VCC run finishes");
    bool allCancelled = window.plan().size() == 4;
    for (const Flasher::BatchEntry &entry : window.plan().entries()) {
        allCancelled = allCancelled && entry.outcome == Flasher::CardOutcome::Cancelled;
    }
    CHECK(allCancelled, "Abort cancels every VCC");
    CHECK(window.summaryPage()->headline() == QLatin1String("0 of 4 VCCs updated"), "and the Summary says none");
}
