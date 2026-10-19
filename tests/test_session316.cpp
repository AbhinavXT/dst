#include "testutil.h"

#include "flashercore.h"
#include "flasherimagedialog.h"
#include "statusline.h"

#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QTemporaryDir>

// =============================================================================
//  Session 316 — Make image…: stamp and sign a built image in the flasher.
//
//  Stamping is native (fw_stamp.py's "stamp"), checked byte for byte against
//  the real tool's output (session 298's fixture). Signing runs the updater's
//  fw_sign.py; here a stand-in (fixtures/fw_trailer/fake_fw_sign.py) checks
//  the call and appends the real tool's signature block.
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

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const quint32 kFixtureBuildTime = 0x6AD261C0u;   // fw_stamp.py's, in the README

}  // namespace

TEST_SUITE(session316)
{
    const QByteArray stamped = fixture("KAVACH_Input_Card_stamped_1.2.9.appimage");
    const QByteArray signedImage = fixture("KAVACH_Input_Card_signed_1.2.9.appimage");
    const QByteArray body = stamped.left(stamped.size() - Flasher::kTrailerBytes);
    CHECK(body.size() == 5000 && signedImage.size() == 5000 + 24 + 72, "fixtures");

    // ---- versions --------------------------------------------------------------------
    {
        quint32 v = 0;
        CHECK(Flasher::parseFwVersion(QStringLiteral("1.2.9"), &v) && v == 0x010209u, "1.2.9");
        CHECK(Flasher::parseFwVersion(QStringLiteral(" 255.0.10 "), &v) && v == 0xFF000Au, "255.0.10");
        CHECK(!Flasher::parseFwVersion(QStringLiteral("1.2"), &v), "two parts refused");
        CHECK(!Flasher::parseFwVersion(QStringLiteral("1.256.0"), &v), "256 refused");
        CHECK(!Flasher::parseFwVersion(QStringLiteral("1.-2.0"), &v), "negative refused");
        CHECK(!Flasher::parseFwVersion(QStringLiteral("1.2.x"), &v), "not a number refused");
    }

    // ---- stamping ----------------------------------------------------------------------
    {
        QString error;
        const QByteArray made = Flasher::stampImage(body, 2, 0x010209u, 0, kFixtureBuildTime, false, &error);
        CHECK(made == stamped, "the same bytes fw_stamp.py wrote");
        CHECK(Flasher::stampImage(stamped, 2, 0x010209u, 0, kFixtureBuildTime, false, &error).isEmpty()
                  && error.contains(QStringLiteral("already has a trailer")),
              "a stamped image is not stamped twice");
        CHECK(Flasher::stampImage(stamped, 2, 0x010209u, 0, kFixtureBuildTime, true, &error) == stamped,
              "Replace swaps the trailer");
        QByteArray damaged = stamped;
        damaged[damaged.size() - 1] = char(damaged.at(damaged.size() - 1) ^ 0x01);
        CHECK(Flasher::stampImage(damaged, 2, 0x010209u, 0, kFixtureBuildTime, true, &error) == stamped,
              "a damaged trailer is replaced too");
        CHECK(Flasher::stampImage(signedImage, 2, 0x010209u, 0, kFixtureBuildTime, true, &error).isEmpty()
                  && error.contains(QStringLiteral("signed")),
              "a signed image is refused: the signature would break");
        CHECK(Flasher::stampImage(body, 5, 0x010209u, 0, kFixtureBuildTime, false, &error).isEmpty(),
              "card type 5 refused");
        const Flasher::ImageTrailer t =
            Flasher::readImageTrailer(Flasher::stampImage(body, 4, 0x020001u, 3, 1u, false, &error));
        CHECK(t.state == Flasher::ImageTrailer::State::Valid && t.cardType == 4 && t.hwRev == 3
                  && t.fwVersion == 0x020001u && t.imageLength == 5000,
              "the reader reads back what was stamped");
    }

    // ---- running fw_sign.py --------------------------------------------------------------
    QTemporaryDir dir;
    const QStringList python = Flasher::findPython();
    CHECK(!python.isEmpty(), "Python on this PC (fw_sign.py and the validators need it)");
    {
        Flasher::SignRequest r;
        r.python = python;
        r.script = fixturePath("fake_fw_sign.py");
        r.input = fixturePath("KAVACH_Input_Card_stamped_1.2.9.appimage");
        r.output = dir.filePath(QStringLiteral("out.appimage"));
        r.key = fixturePath("fw_pubkeys_one.h");
        r.keyId = 1;
        r.passphrase = QStringLiteral("secret");
        Flasher::SignResult result = Flasher::runFwSign(r);
        CHECK(result.ok && result.message.contains(QStringLiteral("key 1 (passphrase set)")),
              QByteArray("signed, the passphrase in the environment (") + result.message.toUtf8() + ")");
        QFile out(r.output);
        CHECK(out.open(QIODevice::ReadOnly) && out.readAll() == signedImage, "the output is the signed image");

        r.passphrase = QStringLiteral("wrong");
        result = Flasher::runFwSign(r);
        CHECK(!result.ok && result.message == QStringLiteral("ValueError: Bad decrypt. Incorrect password?"),
              QByteArray("a failure gives the tool's last line (") + result.message.toUtf8() + ")");

        r.python = QStringList{ dir.filePath(QStringLiteral("no-such-python")) };
        result = Flasher::runFwSign(r);
        CHECK(!result.ok && result.message.startsWith(QStringLiteral("could not start")), "no Python: named");
        r.python.clear();
        CHECK(!Flasher::runFwSign(r).ok, "no Python found: refused");
    }

    // ---- the dialog ------------------------------------------------------------------------
    auto keysFrom = [](const char *name) { return Flasher::readPublicKeys(fixturePath(name)); };
    auto setUp = [&](FlasherImageDialog &d, const QString &input) {
        d.setInput(input);
        d.findChild<QLineEdit *>(QStringLiteral("makeImageScript"))->setText(fixturePath("fake_fw_sign.py"));
        d.findChild<QLineEdit *>(QStringLiteral("makeImageKey"))->setText(fixturePath("fw_pubkeys_one.h"));
    };
    auto status = [](FlasherImageDialog &d) { return d.findChild<StatusLine *>()->text(); };
    {
        // A built (unstamped) image: stamped, card from the file name.
        const QString built = dir.filePath(QStringLiteral("KAVACH_Input_Card_v5.appimage"));
        CHECK(writeFile(built, body), "fixture: the built image");
        FlasherImageDialog d(keysFrom("fw_pubkeys_two.h"));
        setUp(d, built);
        const QList<QGroupBox *> boxes = d.findChildren<QGroupBox *>();
        CHECK(boxes.size() == 2 && boxes.at(0)->isChecked(), "an unstamped image: Stamp is ticked");
        boxes.at(1)->setChecked(false);
        d.findChild<QLineEdit *>(QStringLiteral("makeImageVersion"))->setText(QStringLiteral("1.2.10"));
        CHECK(d.outputPath().endsWith(QStringLiteral("KAVACH_Input_Card_v5_stamped.appimage")), "the suggested name");
        CHECK(d.makeImage(), QByteArray("stamped (") + status(d).toUtf8() + ")");
        QFile out(d.outputPath());
        const Flasher::ImageTrailer t = Flasher::readImageTrailer(out.open(QIODevice::ReadOnly) ? out.readAll() : QByteArray());
        CHECK(t.state == Flasher::ImageTrailer::State::Valid && t.cardType == 2 && t.fwVersion == 0x01020Au,
              "Input card 1.2.10, as the file's name says");
        QFile original(built);
        CHECK(original.open(QIODevice::ReadOnly) && original.readAll() == body, "the built image is unchanged");

        d.findChild<QLineEdit *>(QStringLiteral("makeImageOutput"))->setText(built);
        CHECK(!d.makeImage() && status(d).contains(QStringLiteral("new file")), "never written over the original");
    }
    {
        // The stamped image signed, checked with the boards' keys.
        FlasherImageDialog d(keysFrom("fw_pubkeys_two.h"));
        setUp(d, fixturePath("KAVACH_Input_Card_stamped_1.2.9.appimage"));
        d.findChild<QLineEdit *>(QStringLiteral("makeImageOutput"))->setText(dir.filePath(QStringLiteral("a.appimage")));
        const QList<QGroupBox *> boxes = d.findChildren<QGroupBox *>();
        CHECK(!boxes.at(0)->isChecked(), "a stamped image: Stamp is not ticked");
        CHECK(d.makeImage() && status(d).contains(QStringLiteral("checked against")),
              QByteArray("signed and the signature matches key 1 (") + status(d).toUtf8() + ")");
        CHECK(d.findChild<QLineEdit *>(QStringLiteral("makeImagePassphrase"))->text().isEmpty(),
              "the passphrase is cleared after use");
    }
    {
        FlasherImageDialog d(keysFrom("fw_pubkeys_swapped.h"));
        setUp(d, fixturePath("KAVACH_Input_Card_stamped_1.2.9.appimage"));
        d.findChild<QLineEdit *>(QStringLiteral("makeImageOutput"))->setText(dir.filePath(QStringLiteral("b.appimage")));
        CHECK(!d.makeImage() && status(d).contains(QStringLiteral("would refuse")),
              QByteArray("a key the boards do not have as key 1 (") + status(d).toUtf8() + ")");
    }
    {
        FlasherImageDialog d(Flasher::PublicKeys{});
        setUp(d, fixturePath("KAVACH_Input_Card_stamped_1.2.9.appimage"));
        d.findChild<QLineEdit *>(QStringLiteral("makeImageOutput"))->setText(dir.filePath(QStringLiteral("c.appimage")));
        d.findChild<QLineEdit *>(QStringLiteral("makeImagePassphrase"))->setText(QStringLiteral("wrong"));
        CHECK(!d.makeImage() && status(d).contains(QStringLiteral("Bad decrypt")),
              QByteArray("a wrong passphrase, in fw_sign.py's words (") + status(d).toUtf8() + ")");
        CHECK(d.makeImage() && status(d).contains(QStringLiteral("Not checked here")),
              "no fw_pubkeys.h in the profile: signed, not checked");
    }
}
