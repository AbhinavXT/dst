#include "flasherimagedialog.h"

#include "settings.h"
#include "statusline.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace {
const char *kScriptKey = "flasher/makeImage/fwSignScript";
const char *kKeyKey    = "flasher/makeImage/privateKey";
const char *kKeyIdKey  = "flasher/makeImage/keyId";

QByteArray readAll(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return QByteArray();
    }
    return file.readAll();
}
}  // namespace

FlasherImageDialog::FlasherImageDialog(const Flasher::PublicKeys &keys, QWidget *parent)
    : QDialog(parent), m_keys(keys), m_python(Flasher::findPython())
{
    setWindowTitle(tr("Make image"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Stamp a built .appimage with its card and version, and sign it with the "
                                "updater's fw_sign.py. The original file is not changed: a new file is written."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto pathRow = [this](QLineEdit *edit, const QString &title, const QString &filter, bool save) {
        auto *row = new QHBoxLayout();
        edit->setFont(UiStyle::monoFont());
        edit->setMinimumWidth(420);
        row->addWidget(edit, 1);
        auto *browse = new QPushButton(tr("Browse…"), this);
        connect(browse, &QPushButton::clicked, this,
                [this, edit, title, filter, save]() { browseInto(edit, title, filter, save); });
        row->addWidget(browse);
        return row;
    };

    // ---- the built image ---------------------------------------------------------
    auto *inputForm = new QFormLayout();
    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("makeImageInput"));
    inputForm->addRow(tr("Built image"), pathRow(m_input, tr("Built image"), tr("Images (*.appimage *.bin);;All files (*)"), false));
    m_inputState = new QLabel(this);
    m_inputState->setWordWrap(true);
    inputForm->addRow(QString(), m_inputState);
    layout->addLayout(inputForm);
    connect(m_input, &QLineEdit::editingFinished, this, &FlasherImageDialog::refreshInput);

    // ---- stamp ---------------------------------------------------------------------
    m_stampBox = new QGroupBox(tr("Stamp (as fw_stamp.py)"), this);
    m_stampBox->setCheckable(true);
    auto *stampForm = new QFormLayout(m_stampBox);
    m_card = new QComboBox(m_stampBox);
    for (int card : Flasher::defaultCardOrder()) m_card->addItem(Flasher::cardName(card), card);
    stampForm->addRow(tr("Card"), m_card);
    m_version = new QLineEdit(m_stampBox);
    m_version->setObjectName(QStringLiteral("makeImageVersion"));
    m_version->setPlaceholderText(QStringLiteral("1.2.9"));
    m_version->setFont(UiStyle::monoFont());
    stampForm->addRow(tr("Version"), m_version);
    m_hwRev = new QSpinBox(m_stampBox);
    m_hwRev->setRange(0, 255);
    m_hwRev->setToolTip(tr("0 = any (reserved for a future check)"));
    stampForm->addRow(tr("Hardware rev"), m_hwRev);
    m_replace = new QCheckBox(tr("Replace the image's existing trailer"), m_stampBox);
    m_replace->hide();
    stampForm->addRow(QString(), m_replace);
    layout->addWidget(m_stampBox);

    // ---- sign ----------------------------------------------------------------------
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    m_signBox = new QGroupBox(tr("Sign (runs the updater's fw_sign.py)"), this);
    m_signBox->setCheckable(true);
    auto *signForm = new QFormLayout(m_signBox);
    m_script = new QLineEdit(settings.value(QLatin1String(kScriptKey)).toString(), m_signBox);
    m_script->setObjectName(QStringLiteral("makeImageScript"));
    m_script->setPlaceholderText(tr("updater/tools/fw_sign.py"));
    signForm->addRow(tr("fw_sign.py"), pathRow(m_script, tr("fw_sign.py"), tr("Python (*.py);;All files (*)"), false));
    m_key = new QLineEdit(settings.value(QLatin1String(kKeyKey)).toString(), m_signBox);
    m_key->setObjectName(QStringLiteral("makeImageKey"));
    m_key->setPlaceholderText(tr("release.pem — the private key"));
    signForm->addRow(tr("Private key"), pathRow(m_key, tr("Private key"), tr("PEM (*.pem);;All files (*)"), false));
    m_keyId = new QSpinBox(m_signBox);
    m_keyId->setRange(0, 255);
    m_keyId->setValue(settings.value(QLatin1String(kKeyIdKey), 0).toInt());
    m_keyId->setToolTip(tr("The key's place in fw_pubkeys.h (the first key is 0)"));
    signForm->addRow(tr("Key id"), m_keyId);
    m_passphrase = new QLineEdit(m_signBox);
    m_passphrase->setObjectName(QStringLiteral("makeImagePassphrase"));
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_passphrase->setPlaceholderText(tr("asked each time, never saved; empty for an unencrypted key"));
    signForm->addRow(tr("Passphrase"), m_passphrase);
    auto *python = new QLabel(m_python.isEmpty() ? tr("Python: not found on this PC — fw_sign.py cannot run")
                                                 : tr("Python: %1").arg(QDir::toNativeSeparators(m_python.join(QLatin1Char(' ')))),
                              m_signBox);
    python->setWordWrap(true);
    signForm->addRow(QString(), python);
    layout->addWidget(m_signBox);

    // ---- output --------------------------------------------------------------------
    auto *outForm = new QFormLayout();
    m_output = new QLineEdit(this);
    m_output->setObjectName(QStringLiteral("makeImageOutput"));
    outForm->addRow(tr("Write to"), pathRow(m_output, tr("Write the image to"), tr("Images (*.appimage);;All files (*)"), true));
    layout->addLayout(outForm);
    connect(m_stampBox, &QGroupBox::toggled, this, &FlasherImageDialog::suggestOutput);
    connect(m_signBox, &QGroupBox::toggled, this, &FlasherImageDialog::suggestOutput);

    auto *buttons = new QHBoxLayout();
    m_status = new StatusLine(this);
    m_status->state(tr("Choose the built image."));
    buttons->addWidget(m_status, 1);
    auto *make = new QPushButton(tr("Make image"), this);
    make->setObjectName(QStringLiteral("makeImageButton"));
    make->setDefault(true);
    connect(make, &QPushButton::clicked, this, &FlasherImageDialog::makeImage);
    buttons->addWidget(make);
    auto *close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);
    layout->addLayout(buttons);
}

QString FlasherImageDialog::browseInto(QLineEdit *edit, const QString &title, const QString &filter, bool save)
{
    const QString start = edit->text().isEmpty() ? m_input->text() : edit->text();
    const QString chosen = save ? QFileDialog::getSaveFileName(this, title, start, filter)
                                : QFileDialog::getOpenFileName(this, title, start, filter);
    if (!chosen.isEmpty()) {
        edit->setText(QDir::toNativeSeparators(chosen));
        if (edit == m_input) refreshInput();
    }
    return chosen;
}

void FlasherImageDialog::setInput(const QString &path)
{
    m_input->setText(QDir::toNativeSeparators(path));
    refreshInput();
}

QString FlasherImageDialog::outputPath() const
{
    return QDir::fromNativeSeparators(m_output->text().trimmed());
}

void FlasherImageDialog::refreshInput()
{
    const QString path = QDir::fromNativeSeparators(m_input->text().trimmed());
    QString error;
    const QByteArray bytes = path.isEmpty() ? QByteArray() : readAll(path, &error);
    if (path.isEmpty() || !error.isEmpty()) {
        m_inputState->setText(path.isEmpty() ? QString() : tr("Cannot read it: %1").arg(error));
        return;
    }
    const Flasher::ImageTrailer trailer = Flasher::readImageTrailer(bytes);
    QString state = trailer.state == Flasher::ImageTrailer::State::Valid     ? tr("Stamped: %1").arg(trailer.text())
                    : trailer.state == Flasher::ImageTrailer::State::Damaged ? tr("Damaged trailer: %1").arg(trailer.damage)
                                                                             : tr("Not stamped");
    m_inputState->setText(tr("%1 · %2 · %3 bytes").arg(state, trailer.signatureText()).arg(bytes.size()));
    m_status->state(tr("Ready: Make image writes the new file."));

    // Fill the stamp from the trailer, else from the file name.
    int card = trailer.state == Flasher::ImageTrailer::State::Valid ? trailer.cardType : 0;
    if (card == 0) {
        const QList<int> named = Flasher::cardsNamedIn(QFileInfo(path).fileName());
        if (named.size() == 1) card = named.first();
    }
    if (card != 0) m_card->setCurrentIndex(m_card->findData(card));
    if (trailer.state == Flasher::ImageTrailer::State::Valid) {
        m_version->setText(Flasher::fwVersionText(trailer.fwVersion));
        m_hwRev->setValue(trailer.hwRev);
    }
    m_replace->setVisible(trailer.state != Flasher::ImageTrailer::State::None);
    m_replace->setChecked(false);
    // A stamped image only needs signing; a signed one can be neither.
    m_stampBox->setChecked(trailer.state != Flasher::ImageTrailer::State::Valid);
    suggestOutput();
}

void FlasherImageDialog::suggestOutput()
{
    const QString path = QDir::fromNativeSeparators(m_input->text().trimmed());
    if (path.isEmpty()) return;
    const QFileInfo info(path);
    QString base = info.completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("_(stamped|signed)$")));
    const QString suffix = m_signBox->isChecked() ? QStringLiteral("_signed") : QStringLiteral("_stamped");
    m_output->setText(QDir::toNativeSeparators(info.dir().filePath(base + suffix + QStringLiteral(".appimage"))));
}

bool FlasherImageDialog::makeImage()
{
    const QString input = QDir::fromNativeSeparators(m_input->text().trimmed());
    const QString output = outputPath();
    const bool stamp = m_stampBox->isChecked();
    const bool sign = m_signBox->isChecked();
    auto fail = [this](const QString &text) { m_status->fail(text); return false; };

    if (input.isEmpty()) return fail(tr("Choose the built image."));
    if (!stamp && !sign) return fail(tr("Tick Stamp, Sign or both."));
    if (output.isEmpty()) return fail(tr("Choose where to write the image."));
    if (QFileInfo(output).absoluteFilePath() == QFileInfo(input).absoluteFilePath())
        return fail(tr("Write to a new file: the built image is never changed."));
    QString error;
    QByteArray bytes = readAll(input, &error);
    if (!error.isEmpty()) return fail(tr("Cannot read the image: %1").arg(error));

    if (stamp) {
        quint32 version = 0;
        if (!Flasher::parseFwVersion(m_version->text(), &version))
            return fail(tr("Version must be MAJOR.MINOR.PATCH, each 0..255."));
        bytes = Flasher::stampImage(bytes, m_card->currentData().toInt(), version, m_hwRev->value(),
                                    quint32(QDateTime::currentSecsSinceEpoch()), m_replace->isChecked(), &error);
        if (bytes.isEmpty()) return fail(tr("Not stamped: %1.").arg(error));
    }

    if (!sign) {
        QSaveFile file(output);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return fail(tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(output), file.errorString()));
        m_status->ok(tr("Stamped: %1.").arg(Flasher::readImageTrailer(bytes).text()));
        return true;
    }

    Flasher::SignRequest request;
    request.python = m_python;
    request.script = QDir::fromNativeSeparators(m_script->text().trimmed());
    request.key = QDir::fromNativeSeparators(m_key->text().trimmed());
    request.keyId = m_keyId->value();
    request.passphrase = m_passphrase->text();
    request.output = output;
    if (!QFileInfo::exists(request.script)) return fail(tr("Choose the updater's tools/fw_sign.py."));
    if (!QFileInfo::exists(request.key)) return fail(tr("Choose the private key (.pem)."));
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(kScriptKey), m_script->text().trimmed());
    settings.setValue(QLatin1String(kKeyKey), m_key->text().trimmed());
    settings.setValue(QLatin1String(kKeyIdKey), m_keyId->value());

    // The stamped bytes go to fw_sign.py through a private temporary file.
    QTemporaryDir temp;
    request.input = input;
    if (stamp) {
        request.input = temp.filePath(QStringLiteral("stamped.appimage"));
        QFile file(request.input);
        if (!temp.isValid() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return fail(tr("Cannot write a temporary file for fw_sign.py."));
    }
    m_status->say(tr("Signing…"));
    const Flasher::SignResult result = Flasher::runFwSign(request);
    m_passphrase->clear();
    if (!result.ok) return fail(tr("Not signed: %1").arg(result.message));

    // Read back what fw_sign.py wrote and check it as pre-flight would.
    const QByteArray made = readAll(output, &error);
    const Flasher::ImageTrailer trailer = Flasher::readImageTrailer(made);
    const QString what = trailer.state == Flasher::ImageTrailer::State::Valid ? trailer.text() : tr("not stamped");
    switch (Flasher::checkImageSignature(trailer, m_keys)) {
    case Flasher::SignatureCheck::Valid:
        m_status->ok(tr("Signed with key %1 and checked against the profile's fw_pubkeys.h: %2.")
                         .arg(trailer.signatureKeyId).arg(what));
        return true;
    case Flasher::SignatureCheck::NotChecked:
        m_status->warn(tr("Signed with key %1 (%2). Not checked here: the profile names no fw_pubkeys.h.")
                           .arg(trailer.signatureKeyId).arg(what));
        return true;
    case Flasher::SignatureCheck::UnknownKey:
        return fail(tr("Signed with key %1, but the profile's fw_pubkeys.h has %2 key(s): the boards would refuse it.")
                        .arg(trailer.signatureKeyId).arg(m_keys.keys.size()));
    case Flasher::SignatureCheck::Mismatch:
        return fail(tr("Signed, but not with the key the profile's fw_pubkeys.h has as key %1: the boards would refuse it.")
                        .arg(trailer.signatureKeyId));
    default:
        return fail(tr("fw_sign.py wrote no signature the flasher can read."));
    }
}
