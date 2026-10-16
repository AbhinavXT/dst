#include "testutil.h"
#include "flasherfakeboard.h"

#include "flash_engine.h"
#include "flashercore.h"
#include "flasherqueuepage.h"
#include "flashersummarypage.h"
#include "flasherwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <thread>

// =============================================================================
//  Session 299 — the Firmware Flasher collects the cards' own results (2.6).
//
//  After an Input/Output/Analog image, the VCC relays it over CAN and each
//  card controller reports how its update ended (OK, VERIFY_FAIL,
//  FLASH_FAIL, INCOMPLETE, REJECTED) back to the VCC, which shows them in
//  its INFO. The engine asks for them until they stop coming, then
//  releases a held VCC; a failure reported by a card makes the run a
//  failure, and no report at all is "accepted by the VCC, no card reported".
// =============================================================================

using namespace FlasherTest;

namespace {

kflash::FlashResult runEngine(const kflash::FlashConfig &config)
{
    kflash::FlashEngine engine;
    kflash::FlashResult result;
    std::atomic<bool> done(false);
    kflash::Callbacks callbacks;
    std::thread worker([&]() {
        result = engine.run(config, callbacks);
        done.store(true);
    });
    QElapsedTimer guard;
    guard.start();
    while (!done.load() && guard.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (!done.load()) engine.cancel();
    worker.join();
    return result;
}

kflash::FlashConfig configFor(const FakeBoard &board, kflash::Updater updater, int cardType, int seed)
{
    kflash::FlashConfig config;
    config.receiver_ip = "127.0.0.1";
    config.port = board.port();
    config.card_type = cardType;
    const QByteArray bytes = patternBytes(30 * 1450 + 9, seed);
    config.image.assign(bytes.constBegin(), bytes.constEnd());
    config.updater = updater;
    config.tuning.poll_give_up_ms = 1000;
    config.tuning.card_wait_total_ms = 3000;
    config.tuning.card_wait_quiet_ms = 600;
    config.tuning.card_wait_poll_ms = 150;
    return config;
}

Flasher::Verdict verdictFor(int cardType, const kflash::FlashResult &result)
{
    Flasher::BatchEntry entry;
    entry.cardType = cardType;
    entry.updater = kflash::Updater::V26;
    Flasher::BatchPlan plan(QVector<Flasher::BatchEntry>{ entry }, true);
    plan.startAll();
    plan.finishEntry(0, result, false);
    return Flasher::batchVerdict(plan);
}

}  // namespace

TEST_SUITE(session299)
{
    using kflash::Updater;

    // ---- an Input image: both controllers report OK, then BOOT ---------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.cardReports = { qMakePair(5, 1), qMakePair(6, 1) };
        board.powerOn();
        QElapsedTimer clock;
        clock.start();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardInput, 1));
        CHECK(result.success && result.card_results_asked && result.card_results_supported, "accepted, and the VCC was asked");
        CHECK(result.card_reports.size() == 2 && result.card_reports.at(0).controller_id == 5
                  && result.card_reports.at(0).result == 1 && result.card_reports.at(1).controller_id == 6,
              "both Input card 1 controllers reported OK");
        CHECK(board.infoRequests > 0 && result.boot_acknowledged && board.bootsSeen == 1,
              "the reports were asked for, then the held VCC was released");
        CHECK(clock.elapsed() < 3000 + 2000, QByteArray("it stops once the reports stop (quiet rule): ")
                                                 + QByteArray::number(clock.elapsed()) + " ms");
        const QStringList lines = Flasher::cardReportLines(result);
        CHECK(lines.size() == 2 && lines.first() == QStringLiteral("Input card 1 controller 1 (5): OK — flashed"),
              lines.join(QLatin1String(" | ")).toUtf8());
        const Flasher::Verdict verdict = verdictFor(Flasher::CardInput, result);
        CHECK(verdict.tone == Flasher::Verdict::Tone::Success
                  && verdict.headline == QStringLiteral("Input card — updated, the card reported OK")
                  && verdict.explanation.contains(QLatin1String("2 card controllers reported writing it")),
              QByteArray("verdict: ") + verdict.headline.toUtf8() + " / " + verdict.explanation.toUtf8());
    }

    // ---- an Output image: one controller fails -----------------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.cardReports = { qMakePair(13, 1), qMakePair(14, 3) };
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardOutput, 2));
        CHECK(!result.success && result.card_failed && result.card_reports.size() == 2,
              "a FLASH_FAIL from a controller fails the run");
        CHECK(QString::fromStdString(result.message) == QLatin1String(
                  "Card update failed: the VCC accepted the image, but controller 14 reported FLASH_FAIL"),
              QByteArray("the engine says which: ") + QByteArray::fromStdString(result.message));
        CHECK(result.boot_sent && result.boot_acknowledged, "the VCC did its part: it is still released");
        const QString why = Flasher::failureExplanation(result);
        CHECK(why.startsWith(QLatin1String("CARD UPDATE FAILED"))
                  && why.contains(QStringLiteral("Output card controller 2 (14): FLASH_FAIL — flash write failed on the card"))
                  && !why.contains(QLatin1String("(13)")),
              QByteArray("the Summary names the failing controller: ") + why.toUtf8());
        const Flasher::Verdict verdict = verdictFor(Flasher::CardOutput, result);
        CHECK(verdict.headline == QStringLiteral("Output card — card update failed"), verdict.headline.toUtf8());
    }

    // ---- no report at all -------------------------------------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardAnalog, 3));
        CHECK(result.success && result.card_results_supported && result.card_reports.empty(),
              "no controller reported within the wait: still accepted by the VCC");
        const Flasher::Verdict verdict = verdictFor(Flasher::CardAnalog, result);
        CHECK(verdict.tone == Flasher::Verdict::Tone::Partial
                  && verdict.headline == QStringLiteral("Analog card — accepted by the VCC, no card reported back"),
              QByteArray("not called a success: ") + verdict.headline.toUtf8());
    }

    // ---- a VCC whose updater has no card results (INFO v1) -------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.infoLayout = 1;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardInput, 4));
        CHECK(result.success && result.card_results_asked && !result.card_results_supported,
              "an INFO without card results: asked once, not supported");
        CHECK(verdictFor(Flasher::CardInput, result).headline == QStringLiteral("Input card — image verified"),
              "the verdict as before session 299");
    }

    // ---- a stale result for another image is not this run's ------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.cardReports = { qMakePair(7, 1) };
        board.powerOn();
        kflash::FlashConfig first = configFor(board, Updater::V26, Flasher::CardInput, 5);
        runEngine(first);
        board.powerOn();
        board.cardReports.clear();
        const kflash::FlashResult second = runEngine(configFor(board, Updater::V26, Flasher::CardInput, 6));
        CHECK(second.success && second.card_reports.empty(), "a report for the previous image is ignored");
    }

    // ---- Classic and VCC images are not asked ----------------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.cardReports = { qMakePair(5, 1) };
        board.powerOn();
        const kflash::FlashResult classic = runEngine(configFor(board, Updater::Classic, Flasher::CardVcc, 7));
        CHECK(classic.success && !classic.card_results_asked, "Classic never asks");
        board.powerOn();
        const kflash::FlashResult vcc = runEngine(configFor(board, Updater::V26, Flasher::CardVcc, 8));
        CHECK(vcc.success && !vcc.card_results_asked, "a VCC image has no card results to wait for");
    }

    // ---- the window: the Summary and the report ----------------------------------------
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");
    FakeBoard board;
    board.v26 = true;
    board.cardReports = { qMakePair(5, 1), qMakePair(6, 1) };
    const QString path = writeFile(temp.path(), QStringLiteral("KAVACH_Input_Card_v5.appimage"), patternBytes(40 * 1450, 299));
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile bench;
        bench.name = QStringLiteral("Bench");
        bench.vccIp = QStringLiteral("127.0.0.1");
        bench.port = board.port();
        bench.updater = Updater::V26;
        bench.defaultImages.insert(Flasher::CardInput, path);
        bench.tuning.poll_give_up_ms = 1000;
        bench.tuning.card_wait_total_ms = 3000;
        bench.tuning.card_wait_quiet_ms = 600;
        bench.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
        store.upsert(bench, store.names().first());
        store.setActiveName(bench.name);
        store.save();
        Flasher::ProfileStore reread(store.filePath());
        reread.load();
        CHECK(reread.profile(QStringLiteral("Bench")).tuning.card_wait_quiet_ms == 600,
              "the card wait is saved with the profile's tuning");
    }
    FlasherWindow window(nullptr, temp.path());
    window.queuePage()->selectCard(Flasher::CardInput);
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&window, &FlasherWindow::batchFinished, &loop, [&]() { finished = true; loop.quit(); });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    CHECK(window.startBatch(false), "the run starts");
    QTimer::singleShot(300, &loop, [&]() { board.powerOn(); });
    if (!finished) loop.exec();
    CHECK(finished && window.plan().size() == 1
              && window.plan().entries().first().outcome == Flasher::CardOutcome::Accepted,
          "the Input card run is verified");
    CHECK(window.summaryPage()->headline() == QStringLiteral("Input card — updated, the card reported OK"),
          window.summaryPage()->headline().toUtf8());
    bool rowShown = false;
    for (QLabel *label : window.summaryPage()->findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("Input card 1 controller 2 (6): OK — flashed"))) rowShown = true;
    }
    CHECK(rowShown, "the Summary lists each controller's report");
    const QString report = Flasher::batchReportText(window.plan(), QStringLiteral("B"), QStringLiteral("op"),
                                                    QStringLiteral("Bench"), QStringLiteral("127.0.0.1"), 50001);
    CHECK(report.contains(QStringLiteral("Card reports    : Input card 1 controller 1 (5): OK — flashed")),
          "and so does the report");
}
