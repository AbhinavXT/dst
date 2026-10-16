#include "testutil.h"
#include "flasherfakeboard.h"

#include "flashercore.h"
#include "flasherqueuemodel.h"
#include "flasherqueuepage.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 298 — the Firmware Flasher reads the image trailer (2.6).
//
//  A stamped .appimage ends in 24 bytes saying which card it is built for
//  and its version (tools/fw_stamp.py in the updater repo). With the 2.6
//  updater the queue checks it: a wrong card or a damaged trailer is not
//  sent (the board would refuse it), a valid one replaces the name check,
//  an unstamped image is flagged. The fixture is stamped by fw_stamp.py
//  itself (tests/fixtures/fw_trailer/README.md).
// =============================================================================

using namespace FlasherTest;

namespace {

const char kFixture[] = DL_SRC_DIR "/tests/fixtures/fw_trailer/KAVACH_Input_Card_stamped_1.2.9.appimage";

QByteArray fixtureBytes()
{
    QFile file(QString::fromLatin1(kFixture));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

Flasher::PreflightInput inputFor(kflash::Updater updater, int cardType, const QString &fileName,
                                 const Flasher::ImageTrailer &trailer)
{
    Flasher::PreflightInput input;
    input.vccIp = QStringLiteral("192.168.25.16");
    input.port = 50001;
    input.adapterOnSubnet = true;
    input.subnetText = QStringLiteral("192.168.25.x");
    input.updater = updater;
    Flasher::PreflightRow row;
    row.cardType = cardType;
    row.ticked = true;
    row.hasPath = true;
    row.loaded = true;
    row.sizeBytes = 5024;
    row.fileName = fileName;
    row.nameCheck = Flasher::checkImageName(fileName, cardType);
    row.trailer = trailer;
    input.rows.append(row);
    return input;
}

bool hasItem(const Flasher::PreflightReport &report, const QString &text, bool *blocks = nullptr)
{
    for (const Flasher::PreflightItem &item : report.items) {
        if (item.text.contains(text)) {
            if (blocks) *blocks = item.blocksFlash;
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_SUITE(session298)
{
    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    CHECK(kflash::image_crc32(check, 9) == 0x2DFD2D88u,
          "the firmware's crcFast(\"123456789\") is 0x2DFD2D88, as fw_stamp.py documents");

    // ---- fw_stamp.py's own output -----------------------------------------------------
    const QByteArray stamped = fixtureBytes();
    CHECK(stamped.size() == 5024, QByteArray("the fixture: 5000 bytes + the 24-byte trailer: ")
                                      + QByteArray::number(stamped.size()));
    const Flasher::ImageTrailer trailer = Flasher::readImageTrailer(stamped);
    CHECK(trailer.state == Flasher::ImageTrailer::State::Valid, "fw_stamp.py's trailer reads as valid");
    CHECK(trailer.cardType == Flasher::CardInput && trailer.fwVersion == ((1u << 16) | (2u << 8) | 9u)
              && trailer.hwRev == 0 && trailer.imageLength == 5000 && trailer.buildTime == 0x6AD261C0u,
          "card 2, version 1.2.9, hw rev 0, 5000 bytes, built at UNIX 0x6AD261C0 (what fw_stamp.py show says)");
    CHECK(trailer.text().startsWith(QStringLiteral("Input card · 1.2.9 · built 2026-10-16 ")),
          trailer.text().toUtf8());

    // ---- damaged and absent -------------------------------------------------------------
    {
        QByteArray crc = stamped;
        crc[crc.size() - 24 + 8] = static_cast<char>(crc.at(crc.size() - 24 + 8) ^ 0x01);   // version byte
        const Flasher::ImageTrailer bad = Flasher::readImageTrailer(crc);
        CHECK(bad.state == Flasher::ImageTrailer::State::Damaged && bad.damage == QLatin1String("trailer CRC mismatch"),
              "one changed field: CRC mismatch");
        QByteArray version = stamped;
        version[version.size() - 24 + 4] = 2;
        CHECK(Flasher::readImageTrailer(version).damage == QLatin1String("unknown trailer version 2"),
              "an unknown trailer version");
        QByteArray longer = stamped.left(5000) + QByteArray(1, 'x') + stamped.right(24);
        CHECK(Flasher::readImageTrailer(longer).damage == QLatin1String("image length 5000 does not match the file (5001)"),
              "a trailer moved off the image's end: the length says so");
        CHECK(Flasher::readImageTrailer(stamped.left(5000)).state == Flasher::ImageTrailer::State::None,
              "the same bytes unstamped: no trailer");
        CHECK(Flasher::readImageTrailer(QByteArray(10, 'x')).state == Flasher::ImageTrailer::State::None,
              "a file shorter than a trailer: none");
    }

    // ---- pre-flight -----------------------------------------------------------------------
    {
        bool blocks = true;
        // An Input row with the Input image, named anything: the trailer speaks.
        const Flasher::PreflightReport ok = Flasher::evaluatePreflight(
            inputFor(kflash::Updater::V26, Flasher::CardInput, QStringLiteral("build_42.appimage"), trailer));
        CHECK(ok.ready && hasItem(ok, QStringLiteral("Image trailer: Input card · 1.2.9"), &blocks) && !blocks,
              "2.6: a matching trailer is an OK line");
        CHECK(!ok.needsNameConfirmation && !hasItem(ok, QStringLiteral("does not name")),
              "and the name is not asked about");

        const Flasher::PreflightReport wrong = Flasher::evaluatePreflight(
            inputFor(kflash::Updater::V26, Flasher::CardOutput, QStringLiteral("KAVACH_Output_Card.appimage"), trailer));
        CHECK(!wrong.ready && wrong.firstBlocker == QStringLiteral(
                  "Output card: the image's trailer says it is built for the Input card — the board would refuse it"),
              QByteArray("2.6: a wrong card blocks Flash, whatever the name says: ") + wrong.firstBlocker.toUtf8());

        QByteArray crc = stamped;
        crc[crc.size() - 1] = static_cast<char>(crc.at(crc.size() - 1) ^ 0x80);
        const Flasher::PreflightReport damaged = Flasher::evaluatePreflight(
            inputFor(kflash::Updater::V26, Flasher::CardInput, QStringLiteral("KAVACH_Input_Card.appimage"),
                     Flasher::readImageTrailer(crc)));
        CHECK(!damaged.ready && damaged.firstBlocker.contains(QLatin1String("trailer is damaged (trailer CRC mismatch)")),
              "2.6: a damaged trailer blocks Flash");

        const Flasher::PreflightReport plain = Flasher::evaluatePreflight(
            inputFor(kflash::Updater::V26, Flasher::CardInput, QStringLiteral("KAVACH_Input_Card.appimage"),
                     Flasher::ImageTrailer()));
        CHECK(plain.ready && hasItem(plain, QStringLiteral("no image trailer (unstamped)"), &blocks) && !blocks
                  && hasItem(plain, QStringLiteral("Card IDs match image names")),
              "2.6: an unstamped image is flagged, not blocked, and its name is checked as before");

        const Flasher::PreflightReport classic = Flasher::evaluatePreflight(
            inputFor(kflash::Updater::Classic, Flasher::CardOutput, QStringLiteral("KAVACH_Output_Card.appimage"), trailer));
        CHECK(classic.ready && !hasItem(classic, QStringLiteral("trailer")),
              "Classic: no trailer lines, as before");
    }

    // ---- the queue page --------------------------------------------------------------------
    {
        QTemporaryDir temp;
        CHECK(temp.isValid(), "a temporary directory");
        const QString path = writeFile(temp.path(), QStringLiteral("KAVACH_Input_Card_stamped_1.2.9.appimage"), stamped);
        const Flasher::ImageInfo info = Flasher::loadImage(path);
        CHECK(info.loaded && info.trailer.state == Flasher::ImageTrailer::State::Valid, "loadImage reads the trailer");

        FlasherQueuePage page;
        Flasher::FlashProfile profile;
        profile.vccIp = QStringLiteral("127.0.0.1");
        profile.updater = kflash::Updater::V26;
        page.applyProfile(profile);
        page.setImageForCard(Flasher::CardInput, path);
        page.setImageForCard(Flasher::CardOutput, path);
        int inputRow = -1, outputRow = -1;
        for (int index = 0; index < page.model()->rowCount(); ++index) {
            if (page.model()->row(index).cardType == Flasher::CardInput) inputRow = index;
            if (page.model()->row(index).cardType == Flasher::CardOutput) outputRow = index;
        }
        CHECK(inputRow >= 0 && outputRow >= 0, "the Input and Output rows");
        if (inputRow >= 0 && outputRow >= 0) {
            const QString second = page.model()->index(inputRow, FlasherQueueModel::ColumnImage)
                                       .data(FlasherQueueModel::SecondaryTextRole).toString();
            CHECK(second.endsWith(QStringLiteral(" · v1.2.9")), QByteArray("the row shows the version: ") + second.toUtf8());

            page.selectCard(Flasher::CardInput);
            CHECK(page.preflight().ready, "2.6: the Input image on the Input row is ready");
            page.selectCard(Flasher::CardOutput);
            CHECK(!page.preflight().ready
                      && page.preflight().firstBlocker.contains(QLatin1String("built for the Input card")),
                  "2.6: the same image on the Output row is not");
            page.setUpdater(kflash::Updater::Classic);
            CHECK(page.preflight().ready && page.preflight().needsNameConfirmation,
                  "Classic: as before, the name mismatch only asks for confirmation");
        }
    }
}
