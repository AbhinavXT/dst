#include "testutil.h"

#include "flash_engine.h"
#include "flashercore.h"

#include <QFile>

// =============================================================================
//  Session 313 — the Firmware Flasher reads signed images (updater 2.7).
//
//  fw_sign.py appends a 72-byte "FWSG" block after the trailer. The flasher
//  finds the trailer in front of it and says which key signed the image; the
//  board checks the signature. Reject reasons 5 and 6 are named. The fixture
//  is signed by fw_sign.py itself (tests/fixtures/fw_trailer/README.md).
// =============================================================================

namespace {

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(DL_SRC_DIR "/tests/fixtures/fw_trailer/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

TEST_SUITE(session313)
{
    const QByteArray stamped = fixture("KAVACH_Input_Card_stamped_1.2.9.appimage");
    const QByteArray signedImage = fixture("KAVACH_Input_Card_signed_1.2.9.appimage");
    CHECK(signedImage.size() == 5024 + 72 && signedImage.startsWith(stamped),
          "the fixture: the stamped image + the 72-byte signature block");

    const Flasher::ImageTrailer trailer = Flasher::readImageTrailer(signedImage);
    CHECK(trailer.state == Flasher::ImageTrailer::State::Valid && trailer.cardType == Flasher::CardInput
              && trailer.fwVersion == ((1u << 16) | (2u << 8) | 9u) && trailer.imageLength == 5000,
          "the trailer is found in front of the signature: Input card, 1.2.9, 5000 bytes");
    CHECK(trailer.signatureKeyId == 1 && trailer.signatureText() == QLatin1String("signed, key 1 (checked by the board)"),
          trailer.signatureText().toUtf8());

    const Flasher::ImageTrailer unsignedTrailer = Flasher::readImageTrailer(stamped);
    CHECK(unsignedTrailer.state == Flasher::ImageTrailer::State::Valid && unsignedTrailer.signatureKeyId == -1
              && unsignedTrailer.signatureText() == QLatin1String("not signed"),
          "the stamped image alone: valid trailer, not signed");

    // A signed but unstamped body: no trailer, still signed.
    const QByteArray bodyOnly = stamped.left(5000) + signedImage.right(72);
    const Flasher::ImageTrailer noTrailer = Flasher::readImageTrailer(bodyOnly);
    CHECK(noTrailer.state == Flasher::ImageTrailer::State::None && noTrailer.signatureKeyId == 1,
          "signed, unstamped: no trailer, key 1");

    // A damaged signature magic is not a signature: the trailer is then not at the end.
    QByteArray badMagic = signedImage;
    badMagic[5024] = 'X';
    const Flasher::ImageTrailer notSigned = Flasher::readImageTrailer(badMagic);
    CHECK(notSigned.signatureKeyId == -1 && notSigned.state == Flasher::ImageTrailer::State::None,
          "no FWSG magic: not signed, and no trailer at the end (what an updater older than 2.7 sees)");

    const Flasher::ImageInfo info = Flasher::loadImage(
        QStringLiteral(DL_SRC_DIR "/tests/fixtures/fw_trailer/KAVACH_Input_Card_signed_1.2.9.appimage"));
    CHECK(info.loaded && info.trailer.state == Flasher::ImageTrailer::State::Valid && info.trailer.signatureKeyId == 1,
          "loadImage reads the signed image's trailer and key");

    // ---- reject reasons -------------------------------------------------------------------
    CHECK(QString::fromLatin1(kflash::reject_reason_text(5)).startsWith(QLatin1String("bad signature"))
              && QString::fromLatin1(kflash::reject_reason_text(6)).contains(QLatin1String("not signed")),
          "reasons 5 (bad signature) and 6 (unsigned) are named");
    CHECK(QString::fromLatin1(kflash::reject_reason_text(7)) == QLatin1String("no reason given"),
          "an unknown reason still reads as before");
}
