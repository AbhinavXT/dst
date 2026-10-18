#include "testutil.h"

#include "flashercore.h"

#include <QFile>

// =============================================================================
//  Session 314 — the Firmware Flasher checks image signatures on the PC.
//
//  The profile names the boards' fw_pubkeys.h; pre-flight runs the board's
//  own Ed25519 check (Monocypher) on each signed image, and with "Boards
//  require signed images" an unsigned image is not sent. The key headers are
//  written by the updater's fw_sign.py ("header"): two keys, the same two
//  swapped, one key, and the updater's shipped empty file
//  (tests/fixtures/fw_trailer/README.md).
// =============================================================================

namespace {

QString fixturePath(const char *name)
{
    return QStringLiteral(DL_SRC_DIR "/tests/fixtures/fw_trailer/") + QLatin1String(name);
}

QByteArray fixture(const char *name)
{
    QFile file(fixturePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

Flasher::PreflightInput inputFor(kflash::Updater updater, const Flasher::ImageTrailer &trailer,
                                 const Flasher::PublicKeys &keys, bool requireSignature)
{
    Flasher::PreflightInput input;
    input.vccIp = QStringLiteral("192.168.25.16");
    input.port = 50001;
    input.adapterOnSubnet = true;
    input.subnetText = QStringLiteral("192.168.25.x");
    input.updater = updater;
    input.publicKeys = keys;
    input.requireSignature = requireSignature;
    Flasher::PreflightRow row;
    row.cardType = Flasher::CardInput;
    row.ticked = true;
    row.hasPath = true;
    row.loaded = true;
    row.sizeBytes = 5096;
    row.fileName = QStringLiteral("KAVACH_Input_Card.appimage");
    row.nameCheck = Flasher::checkImageName(row.fileName, row.cardType);
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

TEST_SUITE(session314)
{
    using Check = Flasher::SignatureCheck;

    // ---- fw_pubkeys.h --------------------------------------------------------------------
    const Flasher::PublicKeys two = Flasher::readPublicKeys(fixturePath("fw_pubkeys_two.h"));
    CHECK(two.error.isEmpty() && two.keys.size() == 2 && two.keys.at(1).size() == 32
              && quint8(two.keys.at(1).at(0)) == 0x98 && quint8(two.keys.at(1).at(31)) == 0xB7,
          QByteArray("fw_sign.py's header (CRLF): two keys, key 1 = 98 … B7: ") + two.error.toUtf8());
    const Flasher::PublicKeys none = Flasher::readPublicKeys(fixturePath("fw_pubkeys_none.h"));
    CHECK(none.error.isEmpty() && none.keys.isEmpty(), "the updater's shipped header: no keys, not an error");
    CHECK(Flasher::readPublicKeys(QString()).path.isEmpty() && Flasher::readPublicKeys(QString()).error.isEmpty(),
          "no file named: nothing to report");
    CHECK(Flasher::readPublicKeys(fixturePath("README.md")).error.contains(QLatin1String("no FW_PUBKEY_COUNT")),
          "a file that is not a key header");
    CHECK(Flasher::readPublicKeys(fixturePath("missing.h")).error.startsWith(QLatin1String("cannot read")),
          "a missing file");

    // ---- the check --------------------------------------------------------------------------
    const QByteArray signedImage = fixture("KAVACH_Input_Card_signed_1.2.9.appimage");
    const Flasher::ImageTrailer trailer = Flasher::readImageTrailer(signedImage);
    CHECK(Flasher::checkImageSignature(trailer, two) == Check::Valid, "key 1 of the two: valid");
    CHECK(Flasher::checkImageSignature(trailer, Flasher::readPublicKeys(fixturePath("fw_pubkeys_swapped.h")))
              == Check::Mismatch, "the keys swapped: key 1 is the other key, no match");
    CHECK(Flasher::checkImageSignature(trailer, Flasher::readPublicKeys(fixturePath("fw_pubkeys_one.h")))
              == Check::UnknownKey, "one key: key 1 is unknown");
    CHECK(Flasher::checkImageSignature(trailer, none) == Check::UnknownKey,
          "no keys: refused, as the board without keys refuses every signed image");
    CHECK(Flasher::checkImageSignature(trailer, Flasher::PublicKeys()) == Check::NotChecked, "no file: not checked");
    CHECK(Flasher::checkImageSignature(Flasher::readImageTrailer(fixture("KAVACH_Input_Card_stamped_1.2.9.appimage")),
                                       two) == Check::NotSigned, "the unsigned image");
    QByteArray body = signedImage;
    body[10] = char(body.at(10) ^ 0x01);
    CHECK(Flasher::checkImageSignature(Flasher::readImageTrailer(body), two) == Check::Mismatch,
          "one bit changed in the application: no match");
    QByteArray version = signedImage;
    version[5024 + 4] = 2;
    CHECK(Flasher::checkImageSignature(Flasher::readImageTrailer(version), two) == Check::Unsupported,
          "signature block version 2: unsupported");

    // ---- pre-flight ----------------------------------------------------------------------------
    bool blocks = true;
    const Flasher::PreflightReport ok =
        Flasher::evaluatePreflight(inputFor(kflash::Updater::V26, trailer, two, false));
    CHECK(ok.ready && hasItem(ok, QStringLiteral("Input card: signature matches key 1 (checked on this PC)"), &blocks)
              && !blocks, "2.6: a valid signature is an OK line");

    const Flasher::PreflightReport bad = Flasher::evaluatePreflight(
        inputFor(kflash::Updater::V26, trailer, Flasher::readPublicKeys(fixturePath("fw_pubkeys_swapped.h")), false));
    CHECK(!bad.ready && bad.firstBlocker == QStringLiteral(
              "Input card: the signature does not match key 1 — the board would refuse it"),
          QByteArray("2.6: a wrong signature blocks Flash: ") + bad.firstBlocker.toUtf8());

    const Flasher::PreflightReport unknown = Flasher::evaluatePreflight(
        inputFor(kflash::Updater::V26, trailer, Flasher::readPublicKeys(fixturePath("fw_pubkeys_one.h")), false));
    CHECK(!unknown.ready && unknown.firstBlocker.contains(QLatin1String("signed with key 1, but the profile's "
                                                                        "fw_pubkeys.h has 1 key(s)")),
          unknown.firstBlocker.toUtf8());

    const Flasher::PreflightReport unchecked = Flasher::evaluatePreflight(
        inputFor(kflash::Updater::V26, trailer, Flasher::PublicKeys(), true));
    CHECK(unchecked.ready && hasItem(unchecked, QStringLiteral("signed, key 1 — not checked on this PC (no "
                                                               "fw_pubkeys.h in the profile)"), &blocks) && !blocks,
          "no key file: a signed image is sent, and says it was not checked here");

    const Flasher::ImageTrailer plain = Flasher::readImageTrailer(fixture("KAVACH_Input_Card_stamped_1.2.9.appimage"));
    const Flasher::PreflightReport required =
        Flasher::evaluatePreflight(inputFor(kflash::Updater::V26, plain, two, true));
    CHECK(!required.ready && required.firstBlocker.contains(QStringLiteral("the image is not signed — boards that "
                                                                           "require signatures would refuse it")),
          QByteArray("signatures required: an unsigned image blocks Flash: ") + required.firstBlocker.toUtf8());
    const Flasher::PreflightReport optional =
        Flasher::evaluatePreflight(inputFor(kflash::Updater::V26, plain, two, false));
    CHECK(optional.ready && !hasItem(optional, QStringLiteral("signed")),
          "not required: an unsigned image is sent, with no signature line");

    const Flasher::PreflightReport classic = Flasher::evaluatePreflight(
        inputFor(kflash::Updater::Classic, trailer, Flasher::readPublicKeys(fixturePath("fw_pubkeys_swapped.h")), true));
    CHECK(classic.ready && !hasItem(classic, QStringLiteral("signature")), "Classic: no signature lines");

    // ---- the profile -------------------------------------------------------------------------------
    Flasher::FlashProfile profile;
    profile.publicKeysPath = fixturePath("fw_pubkeys_two.h");
    profile.requireSignature = true;
    const Flasher::FlashProfile back = Flasher::FlashProfile::fromJson(profile.toJson());
    CHECK(back.publicKeysPath == profile.publicKeysPath && back.requireSignature,
          "the key file and the switch are saved with the profile");
    CHECK(Flasher::FlashProfile::fromJson(QJsonObject()).publicKeysPath.isEmpty()
              && !Flasher::FlashProfile::fromJson(QJsonObject()).requireSignature,
          "an older profile: no keys, not required");
}
