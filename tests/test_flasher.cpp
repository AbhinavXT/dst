#include "testutil.h"

#include "flashercore.h"
#include "flasherflashingpage.h"
#include "flasherqueuemodel.h"
#include "flasherqueuepage.h"
#include "flasherstyle.h"
#include "flashersummarypage.h"
#include "flasherwindow.h"
#include "flasherhistorydialog.h"
#include "theme.h"
#include "uicolors.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUdpSocket>
#include <QImage>
#include <QTableView>

#include <cstring>
#include <functional>

// =============================================================================
//  Firmware Flasher.
//
//  Two suites:
//
//    flasher     everything in flashercore (name checks, images, profiles,
//                the batch rules, pre-flight, history), the queue model, and
//                the badge colours against both themes. No sockets.
//
//    flasherrun  the real FlasherWindow, the real FlashWorker thread and the
//                real, unmodified engine, against a fake updater that lives
//                in this file (FakeBoard) and behaves like the Kavach one:
//                silent until "powered on", listens for 5 s, re-arms on
//                every META, and leaves for the application after COMPLETE.
//                Four single-card runs: power-cycled while the flasher
//                waits (verified); not power-cycled (never answers); an
//                image the board rejects (IMAGE_FAIL); aborted while
//                waiting. What is checked is what an operator would read:
//                the power-cycle prompt, the outcome, the Summary headline,
//                and the history file on disk.
//
//  FakeBoard is C++ rather than engine/board_sim.py so the gate needs no
//  Python for this suite and so it can be told to reject one card type. It
//  speaks the same four packets with the same layouts; if the engine's wire
//  format ever changed, these runs would fail at the handshake.
// =============================================================================

namespace {

// ---- helpers ------------------------------------------------------------------

QByteArray patternBytes(int size, int seed)
{
    QByteArray bytes(size, '\0');
    quint32 state = 0x9E3779B9u ^ static_cast<quint32>(seed);
    for (int index = 0; index < size; ++index) {
        state = state * 1664525u + 1013904223u;
        bytes[index] = static_cast<char>(state >> 24);
    }
    return bytes;
}

QString writeFile(const QString &directory, const QString &name, const QByteArray &bytes)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(bytes);
    }
    return path;
}

kflash::FlashResult okResult()
{
    kflash::FlashResult result;
    result.success = true;
    result.message = "Image accepted";
    result.total_blocks = 10;
    result.final_rate_kBps = 1234.0;
    return result;
}

kflash::FlashResult imageFailResult()
{
    kflash::FlashResult result;
    result.success = false;
    result.message = "Board rejected the image again (IMAGE_FAIL) - stopping. "
                     "Check the card type and that the file is the right build.";
    return result;
}

Flasher::BatchEntry entryFor(int cardType)
{
    Flasher::BatchEntry entry;
    entry.cardType = cardType;
    entry.image.path = QStringLiteral("/tmp/x.appimage");
    return entry;
}

Flasher::PreflightInput readyInput()
{
    Flasher::PreflightInput input;
    input.mode = Flasher::Mode::Engineer;
    input.vccIp = QStringLiteral("192.168.25.168");
    input.port = 50001;
    input.adapterOnSubnet = true;
    input.subnetText = QStringLiteral("192.168.25.x");
    Flasher::PreflightRow row;
    row.cardType = Flasher::CardInput;
    row.ticked = true;
    row.hasPath = true;
    row.loaded = true;
    row.fileName = QStringLiteral("KAVACH_Input_Card_v5.appimage");
    row.nameCheck = Flasher::checkImageName(row.fileName, Flasher::CardInput);
    input.rows.append(row);
    return input;
}

bool hasItemContaining(const Flasher::PreflightReport &report, const QString &text)
{
    for (const Flasher::PreflightItem &item : report.items) {
        if (item.text.contains(text)) {
            return true;
        }
    }
    return false;
}

// ---- FakeBoard: a minimal updater on 127.0.0.1 ----------------------------------

#pragma pack(push, 1)
struct FakeMeta   { quint8 type; quint8 card; quint32 size; quint32 crc; quint8 sha[32]; };
struct FakeChunk  { quint8 type; quint32 offset; quint32 size; quint32 crc; };
struct FakeStatus { quint8 type; quint8 status; quint32 crc; quint32 total; quint32 got; quint16 bitmapBytes; };
#pragma pack(pop)

class FakeBoard : public QObject
{
public:
    FakeBoard()
    {
        m_socket.bind(QHostAddress(QHostAddress::LocalHost), 0);
        QObject::connect(&m_socket, &QUdpSocket::readyRead, this, [this]() { drain(); });
    }
    quint16 port() const { return m_socket.localPort(); }

    // The chassis power switch. Power-on starts the updater and its 5 s
    // listen window (FW_WATCHDOG_DEFAULT_MS); power-off or leaving for the
    // application makes the board ignore the flasher entirely.
    void powerOn()
    {
        m_mode = Mode::Updater;
        m_hasSession = false;
        m_lastPacket.start();
    }
    void powerOff() { m_mode = Mode::Off; }
    bool inApplication() const { return m_mode == Mode::Application; }

    int rejectCardType = 0;     // IMAGE_FAIL every image for this card type
    QList<int> metaCards;       // card types seen in META, in order

private:
    enum class Mode { Off, Updater, Application };

    struct Session {
        int card = 0;
        quint32 size = 0;
        quint32 crc = 0;
        QByteArray buffer;
        QByteArray have;
        quint32 blocks = 0;
        quint32 got = 0;
        quint8 state = 1;       // INCOMPLETE
    };

    void drain()
    {
        while (m_socket.hasPendingDatagrams()) {
            QByteArray packet;
            packet.resize(static_cast<int>(m_socket.pendingDatagramSize()));
            QHostAddress from;
            quint16 fromPort = 0;
            m_socket.readDatagram(packet.data(), packet.size(), &from, &fromPort);
            if (packet.isEmpty()) {
                continue;
            }
            handle(packet, from, fromPort);
        }
    }

    void handle(const QByteArray &packet, const QHostAddress &from, quint16 fromPort)
    {
        if (m_mode != Mode::Updater) {
            return;   // off, or running the application: nobody is listening
        }
        // The updater's boot watchdog: 5 s without a firmware packet and it
        // boots the application (checked lazily, on the next arrival).
        if (m_lastPacket.elapsed() > 5000) {
            m_mode = Mode::Application;
            return;
        }
        m_lastPacket.restart();

        const quint8 kind = static_cast<quint8>(packet.at(0));
        if (kind == 0 && packet.size() >= static_cast<int>(sizeof(FakeMeta))) {
            FakeMeta meta;
            std::memcpy(&meta, packet.constData(), sizeof(meta));
            // Like handle_meta_packet(): every META re-arms the session.
            {
                m_session = Session();
                m_session.card = meta.card;
                m_session.size = meta.size;
                m_session.crc = meta.crc;
                m_session.blocks = (meta.size + kflash::kBlockSize - 1) / kflash::kBlockSize;
                m_session.buffer = QByteArray(static_cast<int>(meta.size), '\0');
                m_session.have = QByteArray(static_cast<int>((m_session.blocks + 7) / 8), '\0');
                m_hasSession = true;
                metaCards.append(meta.card);
            }
        } else if (kind == 1 && m_hasSession && packet.size() >= static_cast<int>(sizeof(FakeChunk))) {
            FakeChunk chunk;
            std::memcpy(&chunk, packet.constData(), sizeof(chunk));
            const quint32 block = chunk.offset / kflash::kBlockSize;
            if (block < m_session.blocks && chunk.offset + chunk.size <= m_session.size) {
                const char *payload = packet.constData() + sizeof(FakeChunk);
                std::memcpy(m_session.buffer.data() + chunk.offset, payload, chunk.size);
                const int byteIndex = static_cast<int>(block >> 3);
                const char byte = m_session.have.at(byteIndex);
                if (((byte >> (block & 7)) & 1) == 0) {
                    m_session.have[byteIndex] = static_cast<char>(byte | (1 << (block & 7)));
                    ++m_session.got;
                }
            }
        } else if (kind == 2) {
            FakeStatus status;
            std::memset(&status, 0, sizeof(status));
            status.type = 3;
            if (!m_hasSession) {
                status.status = 2;   // NEED_META
                m_socket.writeDatagram(reinterpret_cast<const char *>(&status), sizeof(status), from, fromPort);
                return;
            }
            if (m_session.got == m_session.blocks && m_session.state == 1) {
                const quint32 crc = kflash::image_crc32(
                    reinterpret_cast<const uint8_t *>(m_session.buffer.constData()),
                    static_cast<size_t>(m_session.buffer.size()));
                const bool good = (crc == m_session.crc) && (m_session.card != rejectCardType);
                if (good) {
                    m_session.state = 0;   // COMPLETE
                } else {
                    m_session.state = 3;   // IMAGE_FAIL
                }
            }
            status.status = m_session.state;
            status.crc = m_session.crc;
            status.total = m_session.blocks;
            status.got = m_session.got;
            status.bitmapBytes = static_cast<quint16>(m_session.have.size());
            QByteArray reply(reinterpret_cast<const char *>(&status), sizeof(status));
            reply.append(m_session.have);
            if (m_session.state == 0) {
                // COMPLETE goes out FW_COMPLETE_REPEATS times, then the board
                // writes flash and Boot_Switch() jumps into the application.
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_mode = Mode::Application;
                return;
            }
            m_socket.writeDatagram(reply, from, fromPort);
        }
    }

    QUdpSocket    m_socket;
    Session       m_session;
    bool          m_hasSession = false;
    Mode          m_mode = Mode::Off;
    QElapsedTimer m_lastPacket;
};

// Run the window's flash to completion (or time out), pumping events.
// `duringRun` is called from the event loop `afterMs` after the start -- the
// test's hand on the power switch or the Abort button.
bool runFlash(FlasherWindow &window, int timeoutMs, int afterMs = -1,
              std::function<void()> duringRun = std::function<void()>())
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool finished = false;
    QObject::connect(&window, &FlasherWindow::batchFinished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    if (!window.startBatch(false)) {
        return false;
    }
    if (afterMs >= 0 && duringRun) {
        QTimer::singleShot(afterMs, &loop, duringRun);
    }
    if (!finished) {
        loop.exec();
    }
    return finished;
}

}  // namespace

// =============================================================================
TEST_SUITE(flasher)
{
    // ---- card names and the image-name check ------------------------------------
    CHECK(Flasher::defaultCardOrder().last() == Flasher::CardVcc,
          "VCC is flashed last by default: the IOA images are relayed through it");
    CHECK(Flasher::defaultCardOrder().size() == 4, "all four card types are in the queue");
    CHECK(Flasher::deliveryText(Flasher::CardOutput) == QStringLiteral("ID 3 · via VCC relay"),
          "an IOA card is described as relayed");

    using Flasher::NameCheck;
    CHECK(Flasher::checkImageName(QStringLiteral("KAVACH_Input_Card_v5.appimage"), Flasher::CardInput).kind == NameCheck::Matches,
          "the real Input build name matches the Input card");
    CHECK(Flasher::checkImageName(QStringLiteral("LKAVACH_v1.2.9.appimage"), Flasher::CardVcc).kind == NameCheck::Matches,
          "LKAVACH names the VCC");
    CHECK(Flasher::checkImageName(QStringLiteral("kavach_input_card.APPIMAGE"), Flasher::CardInput).kind == NameCheck::Matches,
          "the match is case-insensitive");
    CHECK(Flasher::checkImageName(QStringLiteral("KAVACH_Input_Card_v5.appimage"), Flasher::CardVcc).kind == NameCheck::OtherCard,
          "KAVACH_ (no L) does not count as the VCC, so the Input build on the VCC row is flagged");
    {
        const Flasher::NameCheckResult check =
            Flasher::checkImageName(QStringLiteral("KAVACH_Output_Card.appimage"), Flasher::CardInput);
        CHECK(check.kind == NameCheck::OtherCard && check.namedCard == Flasher::CardOutput,
              "the Output build on the Input row names the Output card as the likely mix-up");
        CHECK(check.badgeText.contains(QStringLiteral("Output")), "and the badge says so");
    }
    CHECK(Flasher::checkImageName(QStringLiteral("firmware_final.appimage"), Flasher::CardAnalog).kind == NameCheck::Unrecognised,
          "a name that mentions no card is 'unrecognised', not 'wrong'");
    CHECK(Flasher::checkImageName(QStringLiteral("LKAVACH_Input.appimage"), Flasher::CardInput).kind == NameCheck::OtherCard,
          "a name that mentions two cards is not accepted for either");
    CHECK(Flasher::checkImageName(QString(), Flasher::CardInput).kind == NameCheck::NoImage,
          "no file is its own answer");

    // ---- images ------------------------------------------------------------------------
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary directory for the image and store tests");
    {
        const QByteArray bytes = patternBytes(3 * 1450 + 7, 1);
        const QString path = writeFile(temp.path(), QStringLiteral("KAVACH_Input_Card.appimage"), bytes);
        const Flasher::ImageInfo image = Flasher::loadImage(path);
        CHECK(image.loaded, "an image on disk loads");
        CHECK(image.blockCount == 4, "3 blocks and 7 bytes is 4 blocks");
        CHECK(image.crc32 == kflash::image_crc32(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                                 static_cast<size_t>(bytes.size())),
              "the CRC is the engine's own (what META will carry)");
        CHECK(image.sha256 == QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
              "the engine's SHA-256 agrees with Qt's, so the detail strip shows a standard digest");
        CHECK(image.crcText().startsWith(QStringLiteral("0x")) && image.crcText().size() == 10,
              "CRC text is 0x + eight hex digits");
        CHECK(image.shaHex().size() == 64, "SHA text is 64 hex characters");

        const Flasher::ImageInfo missing = Flasher::loadImage(QDir(temp.path()).filePath(QStringLiteral("nope.appimage")));
        CHECK(!missing.loaded && !missing.error.isEmpty(), "a missing file says why");
        const Flasher::ImageInfo empty =
            Flasher::loadImage(writeFile(temp.path(), QStringLiteral("empty.appimage"), QByteArray()));
        CHECK(!empty.loaded, "an empty file is refused");
    }
    CHECK(Flasher::blocksForSize(0) == 0 && Flasher::blocksForSize(1450) == 1 && Flasher::blocksForSize(1451) == 2,
          "block counting at the boundaries");

    // ---- profiles ----------------------------------------------------------------------
    {
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("Bench — Chassis A");
        profile.vccIp = QStringLiteral("192.168.25.168");
        profile.port = 50011;
        profile.defaultImages.insert(Flasher::CardInput, QStringLiteral("G:/builds/KAVACH_Input.appimage"));
        profile.pinBySha = true;
        profile.pinnedSha.insert(Flasher::CardInput, QString(64, QLatin1Char('a')));
        profile.tuning.adaptive_rate = false;
        profile.tuning.start_rate_kBps = 777;
        profile.tuning.max_rounds = 42;
        profile.startFromLastSettled = true;
        profile.lastSettledRateKBps = 1500.0;
        profile.updaterWaitSeconds = 90;

        const Flasher::FlashProfile back = Flasher::FlashProfile::fromJson(profile.toJson());
        CHECK(back.name == profile.name && back.vccIp == profile.vccIp && back.port == 50011,
              "profile basics survive a JSON round trip");
        CHECK(back.defaultImages.value(Flasher::CardInput) == profile.defaultImages.value(Flasher::CardInput),
              "default images survive");
        CHECK(back.pinBySha && back.pinnedSha.value(Flasher::CardInput) == profile.pinnedSha.value(Flasher::CardInput),
              "SHA pins survive");
        CHECK(!back.tuning.adaptive_rate && back.tuning.start_rate_kBps == 777 && back.tuning.max_rounds == 42,
              "tuning survives");
        CHECK(back.startFromLastSettled && back.lastSettledRateKBps == 1500.0, "the settled rate survives");
        CHECK(back.updaterWaitSeconds == 90, "the updater wait survives");
        QJsonObject silly = profile.toJson();
        silly[QStringLiteral("updater_wait_s")] = 1;
        CHECK(Flasher::FlashProfile::fromJson(silly).updaterWaitSeconds == Flasher::kMinUpdaterWaitSeconds,
              "a wait shorter than the minimum is raised to it");

        QJsonObject broken = profile.toJson();
        QJsonObject tuning = broken.value(QStringLiteral("tuning")).toObject();
        tuning[QStringLiteral("start_rate_kBps")] = QStringLiteral("fast");
        broken[QStringLiteral("tuning")] = tuning;
        CHECK(Flasher::FlashProfile::fromJson(broken).tuning.start_rate_kBps == kflash::TransferTuning().start_rate_kBps,
              "a hand-edited non-number keeps the default instead of becoming zero");

        // effectiveTuning: last settled rate, clamped into [min, max].
        Flasher::FlashProfile settled;
        settled.startFromLastSettled = true;
        settled.lastSettledRateKBps = 2500.4;
        CHECK(Flasher::effectiveTuning(settled).start_rate_kBps == 2500, "the settled rate becomes the start rate");
        settled.lastSettledRateKBps = 99999.0;
        CHECK(Flasher::effectiveTuning(settled).start_rate_kBps == settled.tuning.max_rate_kBps,
              "but never above the ceiling");
        settled.startFromLastSettled = false;
        CHECK(Flasher::effectiveTuning(settled).start_rate_kBps == settled.tuning.start_rate_kBps,
              "and only when the profile asks for it");

        // The wait window becomes the engine's handshake budget.
        Flasher::FlashProfile waiting;
        waiting.updaterWaitSeconds = 60;
        waiting.tuning.poll_give_up_ms = 2500;
        CHECK(Flasher::effectiveTuning(waiting).meta_handshake_tries == 24,
              "60 s at one META attempt per 2.5 s is 24 attempts");
        waiting.updaterWaitSeconds = 11;
        CHECK(Flasher::effectiveTuning(waiting).meta_handshake_tries == 5, "rounded up, never short of the wait");

        // The store: default on first run, save/load, rename, delete.
        const QString storePath = QDir(temp.path()).filePath(QStringLiteral("profiles.json"));
        Flasher::ProfileStore store(storePath);
        CHECK(store.load(), "a missing profiles file is not an error");
        CHECK(store.names().size() == 1, "and gives one default profile");
        store.upsert(profile);
        store.setActiveName(profile.name);
        CHECK(store.save(), "the store saves");

        Flasher::ProfileStore reread(storePath);
        CHECK(reread.load() && reread.names().size() == 2, "and loads back both profiles");
        CHECK(reread.activeName() == profile.name, "with the active one remembered");

        Flasher::FlashProfile renamed = reread.profile(profile.name);
        renamed.name = QStringLiteral("Bench — Chassis B");
        reread.upsert(renamed, profile.name);
        CHECK(!reread.contains(profile.name) && reread.contains(renamed.name), "a rename replaces the old name");
        CHECK(reread.activeName() == renamed.name, "and the active profile follows the rename");
        CHECK(reread.remove(renamed.name), "a profile can be deleted");
        CHECK(!reread.remove(reread.names().first()), "but never the last one");

        writeFile(temp.path(), QStringLiteral("broken.json"), QByteArray("{ not json"));
        Flasher::ProfileStore brokenStore(QDir(temp.path()).filePath(QStringLiteral("broken.json")));
        CHECK(!brokenStore.load() && !brokenStore.lastError().isEmpty(), "an unreadable profiles file is reported");
        CHECK(brokenStore.names().size() == 1, "and the window still has a profile to work with");
    }

    // ---- the batch rules ------------------------------------------------------------------
    {
        QVector<Flasher::BatchEntry> three{ entryFor(Flasher::CardInput), entryFor(Flasher::CardOutput),
                                            entryFor(Flasher::CardVcc) };

        Flasher::BatchPlan allGood(three, true);
        int runs = 0;
        for (int index = allGood.startNext(); index >= 0; index = allGood.startNext()) {
            allGood.finishCurrent(okResult(), false);
            ++runs;
        }
        CHECK(runs == 3 && allGood.succeededCount() == 3 && allGood.isFinished(), "a clean batch runs every card");
        CHECK(Flasher::batchVerdict(allGood).tone == Flasher::Verdict::Tone::Success, "and is a success");
        CHECK(Flasher::batchVerdict(allGood).headline == QStringLiteral("All 3 cards updated"), "headline for a clean batch");

        Flasher::BatchPlan single(QVector<Flasher::BatchEntry>{ entryFor(Flasher::CardOutput) }, true);
        single.startNext();
        single.finishCurrent(okResult(), false);
        const Flasher::Verdict singleVerdict = Flasher::batchVerdict(single);
        CHECK(singleVerdict.headline == QStringLiteral("Output card — image verified"), "one card: named, and 'verified'");
        CHECK(singleVerdict.explanation.contains(QStringLiteral("no confirmation from the card itself")),
              "an IOA card's summary says the PC cannot see the card's own result");
        CHECK(Flasher::successExplanation(Flasher::CardVcc).contains(QStringLiteral("spare flash bank")),
              "the VCC's says it writes the spare bank and boots it");
        CHECK(Flasher::outcomeText(Flasher::CardOutcome::Accepted) == QStringLiteral("Verified"),
              "COMPLETE is shown as Verified, not Accepted");

        Flasher::BatchPlan singleFail(QVector<Flasher::BatchEntry>{ entryFor(Flasher::CardInput) }, true);
        singleFail.startNext();
        singleFail.finishCurrent(imageFailResult(), false);
        CHECK(Flasher::batchVerdict(singleFail).headline == QStringLiteral("Input card — not updated"),
              "one card failed: named, 'not updated'");

        Flasher::BatchPlan stopping(three, true);
        stopping.startNext();
        stopping.finishCurrent(okResult(), false);
        stopping.startNext();
        stopping.finishCurrent(imageFailResult(), false);
        CHECK(stopping.startNext() == -1, "stop on first failure: nothing runs after the failure");
        CHECK(stopping.entries().at(1).outcome == Flasher::CardOutcome::Failed, "the failed card is Failed");
        CHECK(stopping.entries().at(2).outcome == Flasher::CardOutcome::NotRun, "the card after it is Not run, not Queued");
        const Flasher::Verdict stoppedVerdict = Flasher::batchVerdict(stopping);
        CHECK(stoppedVerdict.headline == QStringLiteral("Batch stopped — 1 of 3 cards updated"),
              "the design's headline, word for word");
        CHECK(stoppedVerdict.explanation.contains(QStringLiteral("failed image verification")),
              "IMAGE_FAIL is named as an image-verification failure");
        CHECK(stoppedVerdict.explanation.contains(QStringLiteral("VCC (master) was not flashed because")),
              "and the banner says which card was skipped and why");

        Flasher::BatchPlan carryOn(three, false);
        carryOn.startNext();
        carryOn.finishCurrent(imageFailResult(), false);
        CHECK(carryOn.startNext() == 1, "with stop-on-failure off, the next card still runs");

        Flasher::BatchPlan stopAfter(three, true);
        stopAfter.startNext();
        stopAfter.requestStopAfterCurrent();
        stopAfter.finishCurrent(okResult(), false);
        CHECK(stopAfter.startNext() == -1, "\"Stop after this card\" lets it finish and stops");
        CHECK(stopAfter.entries().at(0).outcome == Flasher::CardOutcome::Accepted, "the running card still counts as accepted");
        CHECK(stopAfter.notRunCount() == 2, "the rest are Not run");

        Flasher::BatchPlan aborted(three, false);
        aborted.startNext();
        aborted.requestAbort();
        kflash::FlashResult cancelled;
        cancelled.cancelled = true;
        cancelled.message = "Cancelled during send";
        aborted.finishCurrent(cancelled, false);
        CHECK(aborted.entries().at(0).outcome == Flasher::CardOutcome::Cancelled, "an aborted card is Cancelled");
        CHECK(aborted.startNext() == -1, "abort stops the batch even with stop-on-failure off");

        Flasher::BatchPlan raceWon(three, false);
        raceWon.startNext();
        raceWon.requestAbort();
        raceWon.finishCurrent(okResult(), false);
        CHECK(raceWon.entries().at(0).outcome == Flasher::CardOutcome::Accepted,
              "a card that completed before the cancel was seen is still Accepted — the board has the image");

        Flasher::BatchPlan held(QVector<Flasher::BatchEntry>{ entryFor(Flasher::CardVcc) }, true);
        held.startNext();
        held.finishCurrent(okResult(), true);
        CHECK(held.entries().at(0).outcome == Flasher::CardOutcome::AlreadyHeld, "already-held is its own outcome");
        CHECK(Flasher::outcomeIsSuccess(Flasher::CardOutcome::AlreadyHeld), "and counts as a success");
    }
    CHECK(Flasher::logLineMeansAlreadyHeld(QStringLiteral("Board already holds this exact image - nothing to send")),
          "the engine's already-held log line is recognised");
    CHECK(Flasher::failureExplanation(imageFailResult()).contains(QStringLiteral("Resending the same file won't help")),
          "IMAGE_FAIL is explained in the handoff's words");
    {
        kflash::FlashResult silent;
        silent.message = "META never acknowledged - check link / IP / board is in updater";
        CHECK(Flasher::failureExplanation(silent).contains(QStringLiteral("power-cycle the chassis")),
              "a board that never answers points at the power-cycle procedure");
    }

    // ---- pre-flight ------------------------------------------------------------------------
    {
        const Flasher::PreflightReport ready = Flasher::evaluatePreflight(readyInput());
        CHECK(ready.ready, "a matched, readable image on the right subnet is ready");
        CHECK(ready.tickedCount == 1, "one card ticked");
        CHECK(ready.items.last().text.contains(QStringLiteral("power-cycle the chassis")) && !ready.items.last().blocksFlash,
              "the power-cycle step is always last, and never blocks");

        Flasher::PreflightInput mismatch = readyInput();
        mismatch.rows[0].fileName = QStringLiteral("KAVACH_Output_Card.appimage");
        mismatch.rows[0].nameCheck = Flasher::checkImageName(mismatch.rows[0].fileName, Flasher::CardInput);
        const Flasher::PreflightReport engineerMismatch = Flasher::evaluatePreflight(mismatch);
        CHECK(engineerMismatch.ready && engineerMismatch.needsNameConfirmation,
              "Engineer: a name mismatch is allowed, behind a confirmation");
        mismatch.mode = Flasher::Mode::Operator;
        const Flasher::PreflightReport operatorMismatch = Flasher::evaluatePreflight(mismatch);
        CHECK(!operatorMismatch.ready, "Operator: a name mismatch blocks the flash");
        CHECK(operatorMismatch.firstBlocker.contains(QStringLiteral("looks like the Output")),
              "and the action bar says which image looks wrong");

        Flasher::PreflightInput noImage = readyInput();
        noImage.rows[0].hasPath = false;
        noImage.rows[0].loaded = false;
        const Flasher::PreflightReport missing = Flasher::evaluatePreflight(noImage);
        CHECK(!missing.ready && missing.firstBlocker == QStringLiteral("Input card: choose an image or pick another card"),
              "a selected card with no image blocks");

        Flasher::PreflightInput huge = readyInput();
        huge.rows[0].sizeBytes = Flasher::kBoardMaxImageBytes + 1;
        const Flasher::PreflightReport hugeReport = Flasher::evaluatePreflight(huge);
        CHECK(!hugeReport.ready && hugeReport.firstBlocker.contains(QStringLiteral("at most 800.0 KB")),
              "an image over the updater's 800 KB buffer is refused with the reason");
        huge.rows[0].sizeBytes = Flasher::kBoardMaxImageBytes;
        CHECK(Flasher::evaluatePreflight(huge).ready, "exactly 800 KB is allowed");

        Flasher::PreflightInput two = readyInput();
        two.rows.append(two.rows.first());
        two.rows[1].cardType = Flasher::CardOutput;
        CHECK(!Flasher::evaluatePreflight(two).ready, "two selected cards is refused: one card per power cycle");

        Flasher::PreflightInput offSubnet = readyInput();
        offSubnet.adapterOnSubnet = false;
        CHECK(Flasher::evaluatePreflight(offSubnet).ready, "Engineer: off-subnet is a warning");
        offSubnet.mode = Flasher::Mode::Operator;
        CHECK(!Flasher::evaluatePreflight(offSubnet).ready, "Operator: off-subnet blocks");

        Flasher::PreflightInput none = readyInput();
        none.rows[0].ticked = false;
        CHECK(!Flasher::evaluatePreflight(none).ready, "no card selected is not ready");

        Flasher::PreflightInput badIp = readyInput();
        badIp.vccIp = QStringLiteral("192.168.25");
        CHECK(!Flasher::evaluatePreflight(badIp).ready, "a malformed IP blocks");

        Flasher::PreflightInput engineRefused = readyInput();
        engineRefused.rows[0].engineProblem = QStringLiteral("Image too large for a single-datagram STATUS bitmap");
        CHECK(!Flasher::evaluatePreflight(engineRefused).ready, "an image the engine would refuse blocks");

        Flasher::PreflightInput pinned = readyInput();
        pinned.rows[0].shaPinMismatch = true;
        const Flasher::PreflightReport pinReport = Flasher::evaluatePreflight(pinned);
        CHECK(pinReport.ready && hasItemContaining(pinReport, QStringLiteral("changed on disk")),
              "a SHA-pin mismatch warns");

    }
    CHECK(Flasher::sameSubnet(0xC0A8190Au, 0xFFFFFF00u, 0xC0A819A8u), "192.168.25.10/24 reaches .168");
    CHECK(!Flasher::sameSubnet(0xC0A8010Au, 0xFFFFFF00u, 0xC0A819A8u), "192.168.1.10/24 does not");
    CHECK(!Flasher::sameSubnet(0xC0A8190Au, 0, 0xC0A819A8u), "no netmask is not a match");
    CHECK(Flasher::subnetDescription(0xC0A819A8u, 0xFFFFFF00u) == QStringLiteral("192.168.25.x"), "a /24 reads as a.b.c.x");
    CHECK(Flasher::subnetDescription(0x0A000001u, 0xFFFFFFF0u) == QStringLiteral("10.0.0.0/28"), "other masks are CIDR");

    // ---- history -------------------------------------------------------------------------
    {
        const QString historyPath = QDir(temp.path()).filePath(QStringLiteral("history.jsonl"));
        Flasher::HistoryLog history(historyPath);
        Flasher::BatchEntry entry = entryFor(Flasher::CardInput);
        // A double quote cannot be in a Windows file name (session 116: the
        // first Windows run wrote no file here, so the image never loaded).
        // Windows keeps the comma, which is what makes CSV quote the field.
#ifdef Q_OS_WIN
        const QString awkwardName = QStringLiteral("KAVACH_Input, 'quoted'.appimage");
#else
        const QString awkwardName = QStringLiteral("KAVACH_Input, \"quoted\".appimage");
#endif
        entry.image = Flasher::loadImage(writeFile(temp.path(), awkwardName, patternBytes(5000, 2)));
        entry.outcome = Flasher::CardOutcome::Accepted;
        entry.result = okResult();
        entry.log << QStringLiteral("line one") << QStringLiteral("line two");
        entry.startedAt = QDateTime::currentDateTimeUtc();
        const Flasher::HistoryRecord record =
            Flasher::makeHistoryRecord(entry, QStringLiteral("B1"), QStringLiteral("abhinav"),
                                       QStringLiteral("Bench A"), QStringLiteral("192.168.25.168"));
        CHECK(record.crcText == entry.image.crcText(), "history takes the CRC from the loaded image");
        CHECK(history.append(record), "a record appends");

        Flasher::BatchEntry notRun = entryFor(Flasher::CardVcc);
        notRun.outcome = Flasher::CardOutcome::NotRun;
        CHECK(history.append(Flasher::makeHistoryRecord(notRun, QStringLiteral("B1"), QStringLiteral("abhinav"),
                                                        QStringLiteral("Bench A"), QStringLiteral("192.168.25.168"))),
              "a not-run card is recorded too");

        // A power cut mid-write leaves half a line. It must cost that line only.
        {
            QFile file(historyPath);
            file.open(QIODevice::WriteOnly | QIODevice::Append);
            file.write("{\"when\":\"2026-09-24T10:");
        }
        int skipped = 0;
        const QList<Flasher::HistoryRecord> all = history.readAll(&skipped);
        CHECK(all.size() == 2 && skipped == 1, "a torn last line is skipped, the rest is kept");
        CHECK(all.first().log.size() == 2 && all.first().result == QStringLiteral("Verified"),
              "the session log and result come back");
        CHECK(all.last().message == QStringLiteral("Batch stopped before this card"), "not-run says why");

        Flasher::HistoryFilter filter;
        filter.cardType = Flasher::CardVcc;
        CHECK(!Flasher::historyMatches(all.first(), filter, QDateTime::currentDateTimeUtc()), "card filter excludes");
        filter.cardType = 0;
        filter.text = QStringLiteral("bench a");
        CHECK(Flasher::historyMatches(all.first(), filter, QDateTime::currentDateTimeUtc()), "text search is case-insensitive");
        filter.text.clear();
        filter.lastDays = 7;
        CHECK(!Flasher::historyMatches(all.first(), filter, QDateTime::currentDateTimeUtc().addDays(10)),
              "the period filter drops old records");

        const QString csv = Flasher::historyToCsv(all);
#ifdef Q_OS_WIN
        // The field is the full path (D:\\...\\KAVACH_Input, ...): quoted means
        // its closing quote follows the name.
        CHECK(csv.contains(QStringLiteral("KAVACH_Input, 'quoted'.appimage\"")),
              "CSV quotes a path with a comma");
#else
        CHECK(csv.contains(QStringLiteral("\"KAVACH_Input, \"\"quoted\"\".appimage\"")) || csv.contains(QStringLiteral("\"\"quoted\"\"")),
              "CSV quotes a path with a comma and doubles its quotes");
#endif
        CHECK(csv.startsWith(QStringLiteral("when_utc,")), "CSV has a header row");

        // The dialog reads the same file.
        FlasherHistoryDialog dialog(historyPath);
        CHECK(dialog.visibleRowCount() == 2, "the History dialog shows both records");
    }

    // ---- badge colours: readable in both themes ---------------------------------------
    {
        const QPalette saved = qApp->palette();
        for (Theme theme : ThemeUtil::all()) {
            ThemeUtil::apply(theme);
            const QColor inks[] = { FlasherStyle::accepted(), FlasherStyle::attention(),
                                    FlasherStyle::danger(), FlasherStyle::muted() };
            bool allReadable = true;
            double worst = 99.0;
            for (const QColor &ink : inks) {
                const double ratio = UiColor::contrastRatio(ink, FlasherStyle::tint(ink));
                if (ratio < worst) {
                    worst = ratio;
                }
                if (ratio < 4.5) {
                    allReadable = false;
                }
            }
            CHECK(allReadable, QStringLiteral("%1 theme: badge text on its tint is at least 4.5:1 (worst %2:1)")
                                   .arg(QString::fromLatin1(ThemeUtil::toString(theme)))
                                   .arg(worst, 0, 'f', 2).toUtf8().constData());
        }
        qApp->setPalette(saved);
    }

    // ---- the queue model ----------------------------------------------------------------
    {
        FlasherQueueModel model;
        CHECK(model.rowCount() == 4 && model.row(3).cardType == Flasher::CardVcc, "four rows, VCC last");
        CHECK(model.tickedRow() == -1, "no card selected at first");
        const int inputRow = model.rowForCard(Flasher::CardInput);
        const int outputRow = model.rowForCard(Flasher::CardOutput);
        Flasher::ImageInfo image = Flasher::loadImage(writeFile(temp.path(), QStringLiteral("KAVACH_Input.appimage"),
                                                                patternBytes(2000, 3)));
        model.setImage(inputRow, image);
        CHECK(model.tickedRow() == -1, "loading an image does not select the card");
        model.setTicked(inputRow, true);
        model.setTicked(outputRow, true);
        CHECK(model.tickedRow() == outputRow && !model.row(inputRow).ticked,
              "selecting a card deselects the other: one card per run");
        model.setTicked(outputRow, false);
        CHECK(model.tickedRow() == -1, "and a card can be deselected");
        model.setTicked(inputRow, true);
        model.clearImage(inputRow);
        CHECK(!model.row(inputRow).hasImage() && model.tickedRow() == -1, "remove clears the image and the selection");
    }

    // ---- the pages, headless ------------------------------------------------------------
    {
        FlasherQueuePage page;
        page.setMode(Flasher::Mode::Operator);
        Flasher::FlashProfile profile;
        profile.vccIp = QStringLiteral("127.0.0.1");
        page.applyProfile(profile);
        page.setImageForCard(Flasher::CardOutput,
                             writeFile(temp.path(), QStringLiteral("KAVACH_Input_mislabelled.appimage"), patternBytes(3000, 4)));
        page.selectCard(Flasher::CardOutput);
        CHECK(!page.preflight().ready, "Operator mode: the Input build on the Output row keeps the button off");

        // The selector is drawn, not left to the style: the selected card's
        // circle is filled green, the others are empty. Rendered and counted,
        // because the bug this guards against (every row a solid dark dot)
        // was only visible on screen.
        {
            page.resize(1300, 720);
            page.show();
            QApplication::processEvents();
            auto *table = page.findChild<QTableView *>(QStringLiteral("flasherQueueTable"));
            CHECK(table != nullptr, "the queue table is there");
            if (table != nullptr) {
                const QImage shot = table->viewport()->grab().toImage();
                const QColor green = UiColor::selectedMark();
                auto greenPixels = [&](int row) {
                    const QRect cell = table->visualRect(table->model()->index(row, FlasherQueueModel::ColumnCard));
                    const QRect area(cell.left(), cell.top(), 40, cell.height());
                    int count = 0;
                    for (int y = area.top(); y <= area.bottom() && y < shot.height(); ++y) {
                        for (int x = area.left(); x <= area.right() && x < shot.width(); ++x) {
                            const QColor pixel = shot.pixelColor(x, y);
                            if (qAbs(pixel.red() - green.red()) < 30 && qAbs(pixel.green() - green.green()) < 30
                                && qAbs(pixel.blue() - green.blue()) < 30) {
                                ++count;
                            }
                        }
                    }
                    return count;
                };
                const int selectedRow = page.model()->rowForCard(Flasher::CardOutput);
                CHECK(greenPixels(selectedRow) > 40, "the selected card's circle is green");
                bool othersClear = true;
                for (int row = 0; row < page.model()->rowCount(); ++row) {
                    if (row != selectedRow && greenPixels(row) > 0) {
                        othersClear = false;
                    }
                }
                CHECK(othersClear, "no other card shows green");
            }
            page.hide();
        }
        page.setImageForCard(Flasher::CardOutput,
                             writeFile(temp.path(), QStringLiteral("KAVACH_Output.appimage"), patternBytes(3000, 5)));
        CHECK(page.batchEntries().size() == 1 && page.batchEntries().first().cardType == Flasher::CardOutput,
              "the right image goes into the batch");
    }
    {
        FlasherPhaseStepper stepper;
        stepper.setStep(FlasherPhaseStepper::StepRepair, FlasherPhaseStepper::StepState::Active, 0.5,
                        QStringLiteral("Repair · round 2"), QStringLiteral("0.3 s"));
        CHECK(stepper.labelOf(FlasherPhaseStepper::StepRepair) == QStringLiteral("Repair · round 2"),
              "the stepper carries the repair round in its label");
    }
}

// =============================================================================
TEST_SUITE(flasherrun)
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");

    FakeBoard board;
    CHECK(board.port() != 0, "the fake updater is listening");

    const QString inputPath = writeFile(temp.path(), QStringLiteral("KAVACH_Input_Card_v5.appimage"),
                                        patternBytes(40 * 1450 + 100, 11));
    const QString outputPath = writeFile(temp.path(), QStringLiteral("KAVACH_Output_Card_v5.appimage"),
                                         patternBytes(25 * 1450, 12));
    const QString vccPath = writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                      patternBytes(30 * 1450 + 1, 13));

    // A profile pointing at the fake board, with defaults for three cards.
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("Test bench");
        profile.vccIp = QStringLiteral("127.0.0.1");
        profile.port = board.port();
        profile.defaultImages.insert(Flasher::CardInput, inputPath);
        profile.defaultImages.insert(Flasher::CardOutput, outputPath);
        profile.defaultImages.insert(Flasher::CardVcc, vccPath);
        profile.tuning.poll_give_up_ms = 1500;
        profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;   // keeps run 2's wasted wait short
        store.upsert(profile, store.names().first());
        store.setActiveName(profile.name);
        store.save();
    }

    FlasherWindow window(nullptr, temp.path());
    CHECK(window.currentPage() == FlasherWindow::QueuePageIndex, "the window opens on the queue");
    CHECK(!window.queuePage()->preflight().ready, "with three images loaded but no card selected, Flash is off");
    Flasher::HistoryLog history(window.historyPath());

    // ---- 1. Flash pressed first, chassis power-cycled while waiting ------------------
    window.queuePage()->selectCard(Flasher::CardInput);
    CHECK(window.queuePage()->preflight().ready, "selecting the Input card makes it ready");
    CHECK(window.queuePage()->batchEntries().size() == 1, "one card per run");
    bool promptWhileWaiting = false;
    const bool run1 = runFlash(window, 30000, 700, [&]() {
        promptWhileWaiting = window.flashingPage()->waitingForUpdater();
        board.powerOn();
    });
    CHECK(run1, "run 1 finishes");
    CHECK(promptWhileWaiting, "the power-cycle prompt was up while the flasher waited");
    CHECK(!window.flashingPage()->waitingForUpdater(), "and is gone once the updater answered");
    CHECK(window.currentPage() == FlasherWindow::SummaryPageIndex, "the run lands on the Summary");
    CHECK(window.plan().entries().first().outcome == Flasher::CardOutcome::Accepted, "Input verified");
    CHECK(window.summaryPage()->headline() == QStringLiteral("Input card — image verified"), "the Summary says so");
    CHECK(board.inApplication(), "and the board has left the updater for its application, like Boot_Switch()");
    CHECK(history.readAll().size() == 1 && history.readAll().last().result == QStringLiteral("Verified"),
          "one history record, Verified");
    {
        Flasher::ProfileStore store(window.profilesPath());
        store.load();
        CHECK(store.profile(QStringLiteral("Test bench")).lastSettledRateKBps >= 1.0,
              "the settled rate was saved to the profile");
    }

    // ---- 2. next card WITHOUT a power cycle: the updater is gone ------------------------
    window.queuePage()->selectCard(Flasher::CardVcc);
    CHECK(runFlash(window, 30000), "run 2 finishes");
    CHECK(window.plan().entries().first().outcome == Flasher::CardOutcome::Failed,
          "a board running its application never answers");
    CHECK(window.summaryPage()->headline() == QStringLiteral("VCC (master) — not updated"), "headline names the card");
    CHECK(Flasher::failureExplanation(window.plan().entries().first().result).contains(QStringLiteral("power-cycle")),
          "and the explanation tells the operator to power-cycle");
    CHECK(!board.metaCards.contains(Flasher::CardVcc), "nothing reached the board");

    // ---- 3. the board rejects the image ---------------------------------------------------
    board.rejectCardType = Flasher::CardOutput;
    window.queuePage()->selectCard(Flasher::CardOutput);
    CHECK(runFlash(window, 30000, 300, [&]() { board.powerOn(); }), "run 3 finishes");
    CHECK(window.plan().entries().first().outcome == Flasher::CardOutcome::Failed, "Output failed");
    CHECK(Flasher::isImageFail(window.plan().entries().first().result), "with IMAGE_FAIL");
    CHECK(board.metaCards.contains(Flasher::CardOutput), "the Output image was offered to the board");
    CHECK(!board.inApplication(), "a rejected image leaves the board in the updater");

    // ---- 4. aborted while waiting for the power cycle ----------------------------------
    board.powerOff();
    window.queuePage()->selectCard(Flasher::CardInput);
    CHECK(runFlash(window, 30000, 500, [&]() { window.abortCurrent(); }), "run 4 finishes");
    CHECK(window.plan().entries().first().outcome == Flasher::CardOutcome::Cancelled, "abort while waiting is Cancelled");
    CHECK(window.summaryPage()->headline() == QStringLiteral("Input card — aborted"), "and the Summary says aborted");

    const QList<Flasher::HistoryRecord> records = history.readAll();
    CHECK(records.size() == 4, "every run is in history");
    CHECK(records.at(1).result == QStringLiteral("Failed") && records.at(3).result == QStringLiteral("Cancelled"),
          "with the outcome of each");
}
