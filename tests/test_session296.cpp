#include "testutil.h"
#include "flasherfakeboard.h"

#include "flash_engine.h"
#include "flashercore.h"
#include "flasherqueuepage.h"
#include "flasherwindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
//  Session 296 — the Firmware Flasher: a Classic | 2.6 updater toggle.
//
//  Classic is the flasher as it was. 2.6 (updater 2.2+) holds the board in
//  the updater with HOLD while the chassis is power-cycled, stops on STATUS
//  REJECTED, calls silence after the session "result unknown", and releases
//  a held VCC that took a card image with BOOT. The engine runs against
//  FakeBoards taught those packets; the window's toggle is the profile's.
// =============================================================================

using namespace FlasherTest;

namespace {

// Runs the engine on its own thread while this one serves the FakeBoard.
kflash::FlashResult runEngine(const kflash::FlashConfig &config, std::vector<std::string> *logLines = nullptr)
{
    kflash::FlashEngine engine;
    kflash::FlashResult result;
    std::atomic<bool> done(false);
    std::vector<std::string> lines;
    std::mutex linesMutex;
    kflash::Callbacks callbacks;
    callbacks.log = [&](kflash::LogLevel, const std::string &text) {
        std::lock_guard<std::mutex> lock(linesMutex);
        lines.push_back(text);
    };
    std::thread worker([&]() {
        result = engine.run(config, callbacks);
        done.store(true);
    });
    QElapsedTimer guard;
    guard.start();
    while (!done.load() && guard.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (!done.load()) {
        engine.cancel();
    }
    worker.join();
    if (logLines) {
        *logLines = lines;
    }
    return result;
}

kflash::FlashConfig configFor(const FakeBoard &board, kflash::Updater updater, int cardType, int blocks, int seed)
{
    kflash::FlashConfig config;
    config.receiver_ip = "127.0.0.1";
    config.port = board.port();
    config.card_type = cardType;
    const QByteArray bytes = patternBytes(blocks * 1450 + 77, seed);
    config.image.assign(bytes.constBegin(), bytes.constEnd());
    config.updater = updater;
    config.tuning.poll_give_up_ms = 1000;
    config.tuning.meta_handshake_tries = 3;
    config.tuning.card_wait_total_ms = 1500;   // session 299: no fake card reports here
    return config;
}

bool logHas(const std::vector<std::string> &lines, const char *text)
{
    for (const std::string &line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

bool runWindow(FlasherWindow &window, int timeoutMs, const std::function<void()> &afterStart)
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
    if (afterStart) {
        QTimer::singleShot(300, &loop, afterStart);
    }
    if (!finished) {
        loop.exec();
    }
    return finished;
}

}  // namespace

TEST_SUITE(session296)
{
    using kflash::Updater;

    CHECK(QString::fromLatin1(kflash::updater_name(Updater::Classic)) == QLatin1String("Classic")
              && QString::fromLatin1(kflash::updater_name(Updater::V26)) == QLatin1String("2.6"),
          "the two updaters' names");
    CHECK(QString::fromLatin1(kflash::reject_reason_text(2)).contains(QLatin1String("different card type"))
              && QString::fromLatin1(kflash::reject_reason_text(3)).contains(QLatin1String("downgrade")),
          "the board's reject reasons in words");

    // ---- Classic is unchanged: no HOLD, no BOOT, even to a 2.6 board ----------------
    {
        FakeBoard board;
        board.v26 = true;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::Classic, Flasher::CardVcc, 60, 1));
        CHECK(result.success && result.updater == Updater::Classic, "Classic flashes a VCC as before");
        CHECK(board.holdsSeen == 0 && board.bootsSeen == 0 && !result.board_held,
              "Classic sends neither HOLD nor BOOT");
    }

    // ---- 2.6, a VCC image: held, flashed, no BOOT (the VCC boots by itself) ---------
    {
        FakeBoard board;
        board.v26 = true;
        board.powerOn();
        std::vector<std::string> lines;
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardVcc, 60, 2), &lines);
        CHECK(result.success && result.updater == Updater::V26, "2.6 flashes a VCC");
        CHECK(result.board_held && board.holdsSeen >= 1, "the board answered HOLD and was held");
        CHECK(board.bootsSeen == 0 && !result.boot_sent, "a VCC image needs no BOOT");
        CHECK(logHas(lines, "Board held in the updater"), "the log says it was held");
    }

    // ---- 2.6, an Input image: the held VCC relays it and is released with BOOT ------
    {
        FakeBoard board;
        board.v26 = true;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardInput, 40, 3));
        CHECK(result.success && result.board_held, "the Input image is accepted by the held VCC");
        CHECK(result.boot_sent && result.boot_acknowledged && board.bootsSeen == 1 && board.inApplication(),
              "then the VCC is released: BOOT, acknowledged, and it leaves the updater");
    }

    // ---- 2.6 against an updater older than 2.2: found by POLL, not held -------------
    {
        FakeBoard board;   // v26 off: ignores HOLD and BOOT
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardInput, 40, 4));
        CHECK(result.success, "an older updater is still flashed in 2.6 mode");
        CHECK(result.older_updater && !result.board_held && !result.boot_sent,
              "it is reported as older (POLL only), not held, and gets no BOOT");
    }

    // ---- REJECTED: final, with its reason, no second transfer -----------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.rejectReason = 2;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardVcc, 40, 5));
        CHECK(!result.success && result.rejected && result.reject_reason == 2, "REJECTED with reason 2");
        CHECK(QString::fromStdString(result.message).contains(QLatin1String("REJECTED")), "the message says REJECTED");
        CHECK(board.metaCards.size() <= 3 * 3, // one META burst per handshake try, never a restart
              QByteArray("no restart after REJECTED: ") + QByteArray::number(board.metaCards.size()) + " METAs");
        CHECK(board.held(), "the refused board stays held for a retry with another file");
        const QString why = Flasher::failureExplanation(result);
        CHECK(why.startsWith(QLatin1String("REJECTED")) && why.contains(QLatin1String("different card type")),
              QByteArray("the Summary explains it: ") + why.toUtf8());

        Flasher::BatchEntry entry;
        entry.cardType = Flasher::CardVcc;
        entry.updater = Updater::V26;
        Flasher::BatchPlan plan(QVector<Flasher::BatchEntry>{ entry }, true);
        plan.startAll();
        plan.finishEntry(0, result, false);
        CHECK(Flasher::batchVerdict(plan).headline == QStringLiteral("VCC (master) — image refused by the board"),
              QByteArray("verdict: ") + Flasher::batchVerdict(plan).headline.toUtf8());
    }

    // ---- silence after the session: "result unknown" in 2.6, a failure in Classic ---
    {
        FakeBoard board;
        board.v26 = true;
        board.loseComplete = true;
        board.powerOn();
        const kflash::FlashResult result = runEngine(configFor(board, Updater::V26, Flasher::CardVcc, 40, 6));
        CHECK(!result.success && result.result_unknown, "2.6: every COMPLETE lost = result unknown");
        CHECK(QString::fromStdString(result.message).startsWith(QLatin1String("Result unknown"))
                  && QString::fromStdString(result.message).contains(QLatin1String("started flashing")),
              QByteArray("and it says the board may be flashing: ") + QByteArray::fromStdString(result.message));
        CHECK(Flasher::failureExplanation(result).startsWith(QLatin1String("Result unknown")),
              "the Summary does not call it a failure");

        Flasher::BatchEntry entry;
        entry.cardType = Flasher::CardVcc;
        entry.updater = Updater::V26;
        Flasher::BatchPlan plan(QVector<Flasher::BatchEntry>{ entry }, true);
        plan.startAll();
        plan.finishEntry(0, result, false);
        const Flasher::Verdict verdict = Flasher::batchVerdict(plan);
        CHECK(verdict.headline == QStringLiteral("VCC (master) — result unknown")
                  && verdict.tone == Flasher::Verdict::Tone::Partial,
              QByteArray("verdict: ") + verdict.headline.toUtf8());

        FakeBoard classicBoard;
        classicBoard.loseComplete = true;
        classicBoard.powerOn();
        const kflash::FlashResult classic = runEngine(configFor(classicBoard, Updater::Classic, Flasher::CardVcc, 40, 6));
        CHECK(!classic.success && !classic.result_unknown
                  && QString::fromStdString(classic.message).contains(QLatin1String("aborting")),
              "Classic reports the same silence as before");
    }

    // ---- nobody answers: the wait runs out with the usual message -------------------
    {
        FakeBoard board;   // never powered on
        kflash::FlashConfig config = configFor(board, Updater::V26, Flasher::CardVcc, 10, 7);
        config.tuning.meta_handshake_tries = 1;
        config.tuning.poll_give_up_ms = 800;
        QElapsedTimer clock;
        clock.start();
        const kflash::FlashResult result = runEngine(config);
        CHECK(!result.success && QString::fromStdString(result.message).contains(QLatin1String("META never acknowledged")),
              "no board: META never acknowledged (the Summary's power-cycle advice)");
        CHECK(clock.elapsed() < 3000, QByteArray("within the wait budget: ") + QByteArray::number(clock.elapsed()) + " ms");
    }

    // ---- the profile, history and report carry the updater --------------------------
    {
        Flasher::FlashProfile profile;
        CHECK(profile.updater == Updater::Classic, "a new profile is Classic");
        profile.updater = Updater::V26;
        const QJsonObject json = profile.toJson();
        CHECK(json.value(QStringLiteral("updater")).toString() == QLatin1String("2.6"), "saved as \"2.6\"");
        CHECK(Flasher::FlashProfile::fromJson(json).updater == Updater::V26, "read back as 2.6");
        QJsonObject old = json;
        old.remove(QStringLiteral("updater"));
        CHECK(Flasher::FlashProfile::fromJson(old).updater == Updater::Classic, "a profile from before is Classic");

        Flasher::BatchEntry entry;
        entry.cardType = Flasher::CardInput;
        entry.updater = Updater::V26;
        entry.outcome = Flasher::CardOutcome::Accepted;
        entry.result.board_held = true;
        entry.result.boot_sent = true;
        entry.result.boot_acknowledged = true;
        CHECK(Flasher::updaterSummary(entry) == QStringLiteral("2.6 · board held, released with BOOT"),
              Flasher::updaterSummary(entry).toUtf8());
        entry.result = kflash::FlashResult();
        entry.result.older_updater = true;
        CHECK(Flasher::updaterSummary(entry).contains(QLatin1String("older than 2.2")), "an older updater is named");

        const Flasher::HistoryRecord record = Flasher::makeHistoryRecord(entry, QStringLiteral("B"), QStringLiteral("op"),
                                                                         QStringLiteral("Bench"), QStringLiteral("192.168.25.16"));
        CHECK(record.updater == QLatin1String("2.6")
                  && Flasher::HistoryRecord::fromJson(record.toJson()).updater == QLatin1String("2.6"),
              "history records the updater");
        QJsonObject oldRecord = record.toJson();
        oldRecord.remove(QStringLiteral("updater"));
        CHECK(Flasher::HistoryRecord::fromJson(oldRecord).updater.isEmpty(), "an older record has none (shown as Classic)");
    }

    // ---- the window: the toggle is the profile's ------------------------------------
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");
    FakeBoard board;
    board.v26 = true;
    const QString vccPath = writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                      patternBytes(80 * 1450 + 11, 296));
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile bench;
        bench.name = QStringLiteral("Bench");
        bench.vccIp = QStringLiteral("127.0.0.1");
        bench.port = board.port();
        bench.defaultImages.insert(Flasher::CardVcc, vccPath);
        bench.tuning.poll_give_up_ms = 1000;
        bench.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
        store.upsert(bench, store.names().first());
        Flasher::FlashProfile site = bench;
        site.name = QStringLiteral("Site");
        site.updater = Updater::V26;
        store.upsert(site);
        store.setActiveName(bench.name);
        store.save();
    }

    FlasherWindow window(nullptr, temp.path());
    QToolButton *classic = window.updaterButton(Updater::Classic);
    QToolButton *v26 = window.updaterButton(Updater::V26);
    CHECK(classic && v26 && classic->objectName() == QLatin1String("flasherUpdaterClassic"),
          "the app bar has the Classic | 2.6 toggle");
    CHECK(window.updater() == Updater::Classic && classic->isChecked() && !v26->isChecked(),
          "it shows the Bench profile's updater: Classic");

    window.queuePage()->selectCard(Flasher::CardVcc);
    CHECK(window.queuePage()->procedureNote().contains(QLatin1String("listens for 5 s")),
          "Classic: the note under the queue speaks of the 5 s window");
    v26->click();
    CHECK(window.updater() == Updater::V26, "clicking 2.6 switches it");
    CHECK(window.queuePage()->procedureNote().contains(QLatin1String("holds it there as soon as it answers")),
          QByteArray("2.6: the note says the board is held: ") + window.queuePage()->procedureNote().toUtf8());
    {
        Flasher::ProfileStore reread(window.profilesPath());
        reread.load();
        CHECK(reread.profile(QStringLiteral("Bench")).updater == Updater::V26, "and saves it into the Bench profile");
    }
    classic->click();
    CHECK(window.updater() == Updater::Classic, "and back to Classic");
    {
        Flasher::ProfileStore reread(window.profilesPath());
        reread.load();
        CHECK(reread.profile(QStringLiteral("Bench")).updater == Updater::Classic, "saved again");
    }

    // The profile combo switches the toggle with the profile.
    QComboBox *combo = window.findChild<QComboBox *>();
    CHECK(combo != nullptr, "the profile combo");
    if (combo) {
        const int siteIndex = combo->findData(QStringLiteral("Site"));
        combo->setCurrentIndex(siteIndex);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, siteIndex));
        CHECK(window.updater() == Updater::V26, "choosing the Site profile shows its 2.6");
    }

    // A 2.6 run through the window: held, locked toggle, recorded.
    window.queuePage()->selectCard(Flasher::CardVcc);
    bool lockedWhileFlashing = false;
    const bool finished = runWindow(window, 30000, [&]() {
        lockedWhileFlashing = !classic->isEnabled() && !v26->isEnabled()
                              && !classic->isVisibleTo(&window) && !v26->isVisibleTo(&window);
        board.powerOn();
    });
    CHECK(finished, "the 2.6 run finishes");
    CHECK(lockedWhileFlashing, "the toggle is locked and out of the bar while flashing");
    CHECK(classic->isEnabled() && v26->isEnabled() && classic->isVisibleTo(&window),
          "and back, free, after");
    if (window.plan().size() == 1) {
        const Flasher::BatchEntry &entry = window.plan().entries().first();
        CHECK(entry.outcome == Flasher::CardOutcome::Accepted && entry.updater == Updater::V26
                  && entry.result.board_held,
              QByteArray("the VCC was held and verified, as a 2.6 entry: ")
                  + Flasher::outcomeText(entry.outcome).toUtf8() + " / "
                  + kflash::updater_name(entry.updater) + " / " + QByteArray::fromStdString(entry.result.message));
        const QString report = Flasher::batchReportText(window.plan(), QStringLiteral("B"), QStringLiteral("op"),
                                                        QStringLiteral("Site"), QStringLiteral("127.0.0.1"), 50001);
        CHECK(report.contains(QStringLiteral("Updater         : 2.6 · board held in the updater")),
              "the report says which updater, and that the board was held");
    }
    const QList<Flasher::HistoryRecord> records = Flasher::HistoryLog(window.historyPath()).readAll();
    CHECK(!records.isEmpty() && records.last().updater == QLatin1String("2.6"),
          "the history record says 2.6");
}
