#include "testutil.h"
#include "flasherfakeboard.h"

#include "flasherboarddialog.h"
#include "flashercore.h"
#include "flasherqueuepage.h"
#include "flashersummarypage.h"
#include "flasherwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstring>
#include <functional>

// =============================================================================
//  Session 297 — the Firmware Flasher: Ask the board (INFO).
//
//  A 2.6 board answers HOLD / INFO_REQ / BOOT with an INFO reply: updater
//  version, controller ID, both banks, the transfer, the VCC's card results,
//  the firmware versions in its banks. "Ask board…" (2.6 only) shows it,
//  holds the board and releases it; the Summary offers it after a "result
//  unknown", and the active bank's CRC then says whether the image went in.
// =============================================================================

using namespace FlasherTest;

namespace {

void put32(QByteArray &bytes, int at, quint32 value)
{
    std::memcpy(bytes.data() + at, &value, 4);
}

bool askAndWait(FlasherBoardDialog *dialog, int timeoutMs = 8000)
{
    QEventLoop loop;
    QObject::connect(dialog, &FlasherBoardDialog::finishedTalking, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, [&]() { loop.exit(1); });
    dialog->ask();
    return loop.exec() == 0;
}

bool releaseAndWait(FlasherBoardDialog *dialog)
{
    QEventLoop loop;
    QObject::connect(dialog, &FlasherBoardDialog::finishedTalking, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, [&]() { loop.exit(1); });
    dialog->release();
    return loop.exec() == 0;
}

}  // namespace

TEST_SUITE(session297)
{
    // ---- the reply, byte by byte ---------------------------------------------------
    {
        QByteArray reply(136, '\0');
        reply[0] = 0x05;
        reply[1] = 3;
        reply[2] = 2; reply[3] = 6; reply[4] = 1;
        reply[5] = 7;                       // Input card 2, controller 1
        reply[6] = 2;
        reply[7] = 1;
        reply[8] = 1;                       // bank B active
        reply[9] = 0;
        reply[10] = 2;
        reply[11] = 1;
        put32(reply, 12, 400000); put32(reply, 16, 0x11223344u);
        put32(reply, 20, 512000); put32(reply, 24, 0xA1B2C3D4u);
        reply[28] = 1; reply[29] = 2;
        put32(reply, 30, 0xCAFEF00Du); put32(reply, 34, 8512); put32(reply, 38, 100); put32(reply, 42, 4321);
        reply[46] = 1;                      // forwarded, collecting
        reply[47 + 4 * 5] = 3;              // controller 5: FLASH_FAIL
        put32(reply, 47 + 4 * 5 + 1, 0xCAFEF00Du);
        reply[47 + 5 * 5] = 1;              // controller 6: OK
        put32(reply, 47 + 5 * 5 + 1, 0xCAFEF00Du);
        put32(reply, 127, (1u << 16) | (2u << 8) | 9u);
        put32(reply, 131, 0);
        reply[135] = 3;

        const Flasher::BoardInfo info = Flasher::parseInfoReply(reply);
        CHECK(info.valid && info.infoVersion == 3 && info.updaterVersionText() == QLatin1String("2.6.1"), "v3: version");
        CHECK(info.controllerId == 7 && info.cardType == 2 && info.metaValid && info.activeBank == 1
                  && !info.confirmed && info.attemptCount == 2 && info.counterEnabled,
              "identity and boot state");
        CHECK(info.bankASize == 400000 && info.bankACrc == 0x11223344u && info.bankBSize == 512000
                  && info.bankBCrc == 0xA1B2C3D4u && info.activeBankCrc() == 0xA1B2C3D4u,
              "both banks, little-endian; the active one is B");
        CHECK(info.sessionState == 1 && info.sessionCardType == 2 && info.sessionCrc == 0xCAFEF00Du
                  && info.sessionTotalBlocks == 8512 && info.sessionBlocksReceived == 100 && info.uptimeMs == 4321,
              "the transfer and uptime");
        CHECK(info.hasCardResults && info.forwardState == 1 && info.cardResults.size() == 16
                  && info.cardResults.at(4).result == 3 && info.cardResults.at(5).result == 1
                  && info.cardResults.at(4).imageCrc == 0xCAFEF00Du,
              "v2: card results by controller ID - 1");
        CHECK(info.hasVersions && info.activeFwVersion == 0x010209u && info.inactiveFwVersion == 0 && info.rejectReason == 3,
              "v3: versions and the last rejection");
        CHECK(info.canHold(), "2.6 can be held");

        const Flasher::BoardInfo v1 = Flasher::parseInfoReply(reply.left(46));
        CHECK(v1.valid && !v1.hasCardResults && !v1.hasVersions && v1.bankBCrc == 0xA1B2C3D4u,
              "a 46-byte v1 reply: the v1 fields only");
        QByteArray old = reply;
        old[3] = 1;
        CHECK(!Flasher::parseInfoReply(old).canHold(), "2.1 cannot be held");
        CHECK(!Flasher::parseInfoReply(reply.left(45)).valid, "a runt is not INFO");
        QByteArray status = reply;
        status[0] = 0x03;
        CHECK(!Flasher::parseInfoReply(status).valid, "nor is a STATUS");

        const QString text = Flasher::boardInfoText(info, { qMakePair(QStringLiteral("VCC image a.appimage"), 0x1u),
                                                            qMakePair(QStringLiteral("Input image b.appimage"), 0xA1B2C3D4u) });
        CHECK(text.contains(QLatin1String("Updater version : 2.6.1  (info layout v3)"))
                  && text.contains(QLatin1String("Controller ID   : 7  (Input card 2 controller 1)"))
                  && text.contains(QLatin1String("Bank B          : 512000 bytes, CRC 0xA1B2C3D4")),
              QByteArray("the text, as flash info prints it:\n") + text.toUtf8());
        CHECK(text.contains(QLatin1String("Active bank is  : the queued Input image b.appimage (same CRC)")),
              "the active bank is named when its CRC matches a queued image");
        CHECK(text.contains(QStringLiteral("Input card 1 controller 1 : FLASH_FAIL — flash write failed on the card")),
              "card results by name and in words");
        CHECK(text.contains(QLatin1String("Active FW       : 1.2.9"))
                  && text.contains(QLatin1String("Other bank FW   : unknown (no trailer)"))
                  && text.contains(QLatin1String("Last rejection  : downgrade")),
              "versions and the last rejection");
    }

    CHECK(Flasher::controllerName(1) == QLatin1String("VCC 1 controller 1")
              && Flasher::controllerName(4) == QLatin1String("VCC 2 controller 2")
              && Flasher::controllerName(12) == QLatin1String("Input card 4 controller 2")
              && Flasher::controllerName(13) == QLatin1String("Output card controller 1")
              && Flasher::controllerName(16) == QLatin1String("Analog card controller 2"),
          "controller IDs by name (const.h)");

    // ---- the dialog against FakeBoards -----------------------------------------------
    {
        FakeBoard board;
        board.v26 = true;
        board.controllerId = 2;
        board.powerOn();
        FlasherBoardDialog dialog(QStringLiteral("127.0.0.1"), board.port(), 5, {});
        CHECK(askAndWait(&dialog) && dialog.hasAnswer(), "a 2.6 board answers");
        CHECK(dialog.info().controllerId == 2 && dialog.resultText().contains(QLatin1String("VCC 1 controller 2")),
              "and says who it is");
        CHECK(board.held() && dialog.statusText().contains(QLatin1String("held")), "it is held, and the status says so");
        CHECK(dialog.releaseButton()->isEnabled(), "Release is offered");
        CHECK(releaseAndWait(&dialog) && board.inApplication() && board.bootsSeen == 1
                  && dialog.statusText().contains(QLatin1String("acknowledged BOOT")),
              "Release: BOOT, acknowledged, the board starts its program");
    }
    {
        FakeBoard board;
        board.v26 = true;
        board.updaterMinor = 1;               // 2.1: INFO, but no HOLD
        board.powerOn();
        FlasherBoardDialog dialog(QStringLiteral("127.0.0.1"), board.port(), 5, {});
        CHECK(askAndWait(&dialog) && dialog.hasAnswer() && board.infoRequests > 0 && !board.held(),
              "a 2.1 board answers INFO_REQ and is not held");
        CHECK(!dialog.releaseButton()->isEnabled() && dialog.statusText().contains(QLatin1String("cannot be held")),
              "and Release is not offered");
    }
    {
        FakeBoard board;                      // classic: POLL only
        board.powerOn();
        FlasherBoardDialog dialog(QStringLiteral("127.0.0.1"), board.port(), 5, {});
        CHECK(askAndWait(&dialog) && !dialog.hasAnswer()
                  && dialog.statusText().contains(QLatin1String("older than 2.0")),
              QByteArray("an updater before 2.0 is found but has nothing to tell: ") + dialog.statusText().toUtf8());
    }
    {
        FakeBoard board;                      // off
        FlasherBoardDialog dialog(QStringLiteral("127.0.0.1"), board.port(), 1, {});
        CHECK(askAndWait(&dialog, 5000) && dialog.statusText().contains(QLatin1String("No answer in 1 s")),
              QByteArray("nobody: no answer, said so: ") + dialog.statusText().toUtf8());
        FlasherBoardDialog bad(QStringLiteral("192.168.25"), 50001, 1, {});
        bad.ask();
        CHECK(bad.statusText().contains(QLatin1String("not an IPv4 address")), "a bad address is refused");
    }

    // ---- the window: the button, and a "result unknown" settled ----------------------
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");
    FakeBoard board;
    board.v26 = true;
    board.loseComplete = true;
    const QString vccPath = writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                      patternBytes(60 * 1450 + 5, 297));
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
        store.setActiveName(bench.name);
        store.save();
    }
    FlasherWindow window(nullptr, temp.path());
    CHECK(window.askBoardButton() && window.askBoardButton()->isHidden(), "Classic: no Ask board button");
    window.updaterButton(kflash::Updater::V26)->click();
    CHECK(!window.askBoardButton()->isHidden(), "2.6: Ask board… in the app bar");

    window.queuePage()->selectCard(Flasher::CardVcc);
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&window, &FlasherWindow::batchFinished, &loop, [&]() { finished = true; loop.quit(); });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    bool buttonHidden = false;
    CHECK(window.startBatch(false), "a 2.6 run starts");
    QTimer::singleShot(300, &loop, [&]() {
        buttonHidden = window.askBoardButton()->isHidden();
        board.powerOn();
    });
    if (!finished) loop.exec();
    CHECK(finished && buttonHidden, "it finishes; the button is out of the bar while flashing");
    CHECK(window.plan().size() == 1 && window.plan().entries().first().result.result_unknown,
          "every COMPLETE lost: result unknown");

    auto *summaryAsk = window.summaryPage()->findChild<QPushButton *>(QStringLiteral("summaryAskBoard"));
    CHECK(summaryAsk && summaryAsk->isVisibleTo(window.summaryPage()), "the Summary offers Ask the board…");
    if (summaryAsk) {
        summaryAsk->click();
        auto *dialog = window.findChild<FlasherBoardDialog *>();
        CHECK(dialog != nullptr, "it opens the dialog on the board just flashed");
        if (dialog) {
            board.powerOn();                  // the chassis power-cycled back into the updater
            CHECK(askAndWait(dialog), "the board answers");
            CHECK(dialog->resultText().contains(QLatin1String("Active bank is  : the queued VCC image LKAVACH_v1.2.9.appimage")),
                  QByteArray("its active bank is the image: settled, it went in\n") + dialog->resultText().toUtf8());
            dialog->close();
        }
    }
}
