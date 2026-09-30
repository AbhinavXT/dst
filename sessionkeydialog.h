#ifndef SESSIONKEYDIALOG_H
#define SESSIONKEYDIALOG_H
// =====================================================================
//  sessionkeydialog.{h,cpp} -- Tools -> Session Key.
//
//  Collects the three inputs (the two auth key sets, the station random
//  and the loco random) plus the loco/station ids, applies the selection
//  rules (SessionKeyGen), and shows the derived session key with an
//  explanation of which set and key were chosen and why.
//
//  The auth keys are entered by the operator: DLConsole's @auth_keys
//  decode keeps only fingerprints, never key material, so nothing
//  sensitive is retained from the capture stream — the keys live only in
//  this dialog while it is open. The randoms can be filled from a pasted
//  @rand_num line for convenience.
// =====================================================================
#include <QDialog>

class QComboBox;
class QDateTimeEdit;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;

class SessionKeyDialog : public QDialog {
    Q_OBJECT
public:
    explicit SessionKeyDialog(QWidget *parent = nullptr);

private slots:
    void onDerive();
    void onParseRandLine();
    void onUseNow();
    void onCopyKey();
    void onStoreChanged();
    void onLoadLive();
    void refreshLiveKey();

private:
    static qint64 parseNum(const QString &s, bool *ok);

    // The loco the live section reads, or -1 for the store's active one.
    int selectedLoco() const;

    // live (from log)
    QComboBox *m_liveLoco   = nullptr;
    QLabel    *m_liveStatus = nullptr;
    QLineEdit *m_liveKey    = nullptr;
    // key set 1
    QDateTimeEdit *m_s1Start = nullptr, *m_s1End = nullptr;
    QLineEdit     *m_s1Key0  = nullptr, *m_s1Key1 = nullptr;
    // key set 2
    QDateTimeEdit *m_s2Start = nullptr, *m_s2End = nullptr;
    QLineEdit     *m_s2Key0  = nullptr, *m_s2Key1 = nullptr;

    QSpinBox      *m_locoId  = nullptr, *m_stnId  = nullptr;
    QLineEdit     *m_locoRnd = nullptr, *m_stnRnd = nullptr;
    QLineEdit     *m_randLine = nullptr;
    QDateTimeEdit *m_now     = nullptr;

    QLineEdit     *m_keyOut  = nullptr;
    QLabel        *m_explain = nullptr;
    QPushButton   *m_copyBtn = nullptr;
};

#endif  // SESSIONKEYDIALOG_H
