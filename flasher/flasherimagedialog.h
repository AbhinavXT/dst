#ifndef FLASHERIMAGEDIALOG_H
#define FLASHERIMAGEDIALOG_H
// =============================================================================
//  FlasherImageDialog -- Make image… (session 316)
//  ---------------------------------------------------------------------------
//  Makes a release image from a built .appimage: stamps it (the 24-byte
//  trailer, as tools/fw_stamp.py does) and signs it by running the updater's
//  own tools/fw_sign.py with the operator's private key. The original file is
//  never changed: the result is a new file. The passphrase is asked each time
//  and never saved. The result is checked on the PC against the profile's
//  fw_pubkeys.h, as pre-flight would.
// =============================================================================
#include <QDialog>

#include "flashercore.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class StatusLine;

class FlasherImageDialog : public QDialog
{
    Q_OBJECT
public:
    // `keys`: the profile's fw_pubkeys.h, for the check after signing.
    explicit FlasherImageDialog(const Flasher::PublicKeys &keys, QWidget *parent = nullptr);

    void setInput(const QString &path);  // reads the file, fills card/version from it
    bool makeImage();                    // Make image; public for the tests
    QString outputPath() const;

private:
    void refreshInput();
    void suggestOutput();
    QString browseInto(QLineEdit *edit, const QString &title, const QString &filter, bool save);

    Flasher::PublicKeys m_keys;
    QStringList m_python;
    QLineEdit  *m_input = nullptr;
    QLabel     *m_inputState = nullptr;
    QGroupBox  *m_stampBox = nullptr;
    QComboBox  *m_card = nullptr;
    QLineEdit  *m_version = nullptr;
    QSpinBox   *m_hwRev = nullptr;
    QCheckBox  *m_replace = nullptr;
    QGroupBox  *m_signBox = nullptr;
    QLineEdit  *m_script = nullptr;
    QLineEdit  *m_key = nullptr;
    QSpinBox   *m_keyId = nullptr;
    QLineEdit  *m_passphrase = nullptr;
    QLineEdit  *m_output = nullptr;
    StatusLine *m_status = nullptr;
};

#endif  // FLASHERIMAGEDIALOG_H
