#include "sessionkeydialog.h"
#include "uicolors.h"

#include "sessionkeygen.h"
#include "capturedecoder.h"
#include "sessionkeystore.h"

#include <QDateTimeEdit>
#include <QComboBox>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>
#include <QClipboard>

namespace {
QLineEdit *hexField()
{
    auto *e = new QLineEdit;
    e->setPlaceholderText(QStringLiteral("32 hex chars"));
    e->setMaxLength(32);
    e->setFont(QFont(QStringLiteral("monospace")));
    return e;
}
QDateTimeEdit *dtField(const QDateTime &v)
{
    auto *e = new QDateTimeEdit(v);
    e->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    e->setCalendarPopup(true);
    return e;
}
}  // namespace

qint64 SessionKeyDialog::parseNum(const QString &s, bool *ok)
{
    const QString t = s.trimmed();
    if (t.isEmpty()) { if (ok) { *ok = false; } return 0; }
    return t.startsWith("0x", Qt::CaseInsensitive) ? t.mid(2).toLongLong(ok, 16)
                                                    : t.toLongLong(ok, 10);
}

SessionKeyDialog::SessionKeyDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Session Key"));
    resize(560, 620);
    auto *root = new QVBoxLayout(this);

    const QDateTime now = QDateTime::currentDateTime();

    // Live section: the key the store derived from the capture stream.
    auto *liveBox = new QGroupBox(tr("Live (from log)"), this);
    auto *lv = new QVBoxLayout(liveBox);
    m_liveStatus = new QLabel(liveBox);
    m_liveStatus->setWordWrap(true);
    lv->addWidget(m_liveStatus);
    // Which loco. The store has been per-loco all along — every accessor
    // takes a locoId and locos() lists them — but this dialog only ever
    // asked for the active one, so on a two-loco capture the operator got
    // whichever spoke last and no way to say otherwise.
    auto *locoRow = new QHBoxLayout;
    locoRow->addWidget(new QLabel(tr("loco:"), liveBox));
    m_liveLoco = new QComboBox(liveBox);
    m_liveLoco->setObjectName(QStringLiteral("sessionKeyLocoBox"));
    m_liveLoco->setToolTip(
        tr("Which loco's keys and randoms to read.\n\n"
           "Key sets and randoms are per loco: loading one loco's set against "
           "another's random derives a key that never existed."));
    locoRow->addWidget(m_liveLoco, 1);
    lv->addLayout(locoRow);

    auto *lkRow = new QHBoxLayout;
    lkRow->addWidget(new QLabel(tr("key:"), liveBox));
    m_liveKey = new QLineEdit(liveBox);
    m_liveKey->setReadOnly(true);
    m_liveKey->setFont(QFont(QStringLiteral("monospace")));
    lkRow->addWidget(m_liveKey);
    auto *copyLive = new QPushButton(tr("Copy"), liveBox);
    auto *loadLive = new QPushButton(tr("Load into form"), liveBox);
    lkRow->addWidget(copyLive); lkRow->addWidget(loadLive);
    lv->addLayout(lkRow);
    root->addWidget(liveBox);

    auto buildSet = [&](const QString &title, QDateTimeEdit *&s, QDateTimeEdit *&e,
                        QLineEdit *&k0, QLineEdit *&k1) {
        auto *box = new QGroupBox(title, this);
        auto *g = new QGridLayout(box);
        s = dtField(now.addMonths(-1)); e = dtField(now.addMonths(3));
        k0 = hexField(); k1 = hexField();
        g->addWidget(new QLabel(tr("start")), 0, 0); g->addWidget(s, 0, 1);
        g->addWidget(new QLabel(tr("end")),   0, 2); g->addWidget(e, 0, 3);
        g->addWidget(new QLabel(tr("key 1")), 1, 0); g->addWidget(k0, 1, 1, 1, 3);
        g->addWidget(new QLabel(tr("key 2")), 2, 0); g->addWidget(k1, 2, 1, 1, 3);
        root->addWidget(box);
    };
    buildSet(tr("Key set 1"), m_s1Start, m_s1End, m_s1Key0, m_s1Key1);
    buildSet(tr("Key set 2"), m_s2Start, m_s2End, m_s2Key0, m_s2Key1);

    // ids + randoms
    auto *idBox = new QGroupBox(tr("Randoms & ids"), this);
    auto *ig = new QGridLayout(idBox);
    m_locoId = new QSpinBox(idBox); m_locoId->setRange(0, 1 << 20);
    m_stnId  = new QSpinBox(idBox); m_stnId->setRange(0, 65535);
    m_locoRnd = new QLineEdit(idBox); m_locoRnd->setPlaceholderText(tr("dec or 0x"));
    m_stnRnd  = new QLineEdit(idBox); m_stnRnd->setPlaceholderText(tr("dec or 0x"));
    ig->addWidget(new QLabel(tr("loco_id")), 0, 0); ig->addWidget(m_locoId, 0, 1);
    ig->addWidget(new QLabel(tr("stn_id")),  0, 2); ig->addWidget(m_stnId, 0, 3);
    ig->addWidget(new QLabel(tr("loco_random")), 1, 0); ig->addWidget(m_locoRnd, 1, 1);
    ig->addWidget(new QLabel(tr("stn_random")),  1, 2); ig->addWidget(m_stnRnd, 1, 3);
    m_randLine = new QLineEdit(idBox);
    m_randLine->setPlaceholderText(tr("paste a @rand_num line to fill the randoms"));
    auto *parseBtn = new QPushButton(tr("Parse"), idBox);
    ig->addWidget(new QLabel(tr("@rand_num")), 2, 0);
    ig->addWidget(m_randLine, 2, 1, 1, 2); ig->addWidget(parseBtn, 2, 3);
    root->addWidget(idBox);

    // now + derive
    auto *nowRow = new QHBoxLayout;
    nowRow->addWidget(new QLabel(tr("Now:"), this));
    m_now = dtField(now);
    nowRow->addWidget(m_now);
    auto *nowBtn = new QPushButton(tr("current"), this);
    nowRow->addWidget(nowBtn);
    nowRow->addStretch();
    auto *deriveBtn = new QPushButton(tr("Derive session key"), this);
    nowRow->addWidget(deriveBtn);
    root->addLayout(nowRow);

    // output
    auto *outRow = new QHBoxLayout;
    outRow->addWidget(new QLabel(tr("Session key:"), this));
    m_keyOut = new QLineEdit(this);
    m_keyOut->setReadOnly(true);
    m_keyOut->setFont(QFont(QStringLiteral("monospace")));
    outRow->addWidget(m_keyOut);
    m_copyBtn = new QPushButton(tr("Copy"), this);
    m_copyBtn->setEnabled(false);
    outRow->addWidget(m_copyBtn);
    root->addLayout(outRow);

    m_explain = new QLabel(tr("Enter the key sets, ids and randoms, then Derive."), this);
    m_explain->setWordWrap(true);
    root->addWidget(m_explain);

    connect(deriveBtn, &QPushButton::clicked, this, &SessionKeyDialog::onDerive);
    connect(parseBtn,  &QPushButton::clicked, this, &SessionKeyDialog::onParseRandLine);
    connect(nowBtn,    &QPushButton::clicked, this, &SessionKeyDialog::onUseNow);
    connect(m_copyBtn, &QPushButton::clicked, this, &SessionKeyDialog::onCopyKey);
    connect(copyLive,  &QPushButton::clicked, this, [this]() {
        if (!m_liveKey->text().isEmpty()) {
            QApplication::clipboard()->setText(m_liveKey->text());
        }
    });
    connect(loadLive,  &QPushButton::clicked, this, &SessionKeyDialog::onLoadLive);
    connect(m_liveLoco, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshLiveKey(); });
    connect(&SessionKeyStore::instance(), &SessionKeyStore::changed,
            this, &SessionKeyDialog::onStoreChanged);
    onStoreChanged();   // seed with whatever the store already has
}

void SessionKeyDialog::onStoreChanged()
{
    const SessionKeyStore &s = SessionKeyStore::instance();
    // statusAll(): with one loco this reads exactly as before; with more it
    // names each one, so a two-loco capture does not look like one flickering
    // session.
    m_liveStatus->setText(s.statusAll());

    // Rebuild the loco list, keeping the operator's choice if that loco is
    // still there. Rebuilding blindly would snap the selection back to the
    // first loco every time a frame arrived, which on a live capture is
    // several times a second.
    const int keep = selectedLoco();
    const QSignalBlocker block(m_liveLoco);
    m_liveLoco->clear();
    m_liveLoco->addItem(tr("active loco"), -1);
    for (int id : s.locos()) {
        m_liveLoco->addItem(tr("loco %1").arg(id), id);
    }
    const int at = m_liveLoco->findData(keep);
    m_liveLoco->setCurrentIndex(at >= 0 ? at : 0);

    refreshLiveKey();
}

int SessionKeyDialog::selectedLoco() const
{
    // -1 is the store's own "whichever loco is active", so the default entry
    // needs no special case here.
    return m_liveLoco ? m_liveLoco->currentData().toInt() : -1;
}

void SessionKeyDialog::refreshLiveKey()
{
    const SessionKeyStore &s = SessionKeyStore::instance();
    const int loco = selectedLoco();
    m_liveKey->setText(s.haveSessionKey(loco)
                           ? QString::fromLatin1(s.sessionKey(loco).toHex())
                           : QString());
}

void SessionKeyDialog::onLoadLive()
{
    const SessionKeyStore &s = SessionKeyStore::instance();
    auto putSet = [](QDateTimeEdit *st, QDateTimeEdit *en, QLineEdit *k0, QLineEdit *k1,
                     const SessionKeyGen::KeySet &ks) {
        if (ks.start.isValid()) { st->setDateTime(ks.start); }
        if (ks.end.isValid())   { en->setDateTime(ks.end); }
        if (ks.key0.size() == 16) { k0->setText(QString::fromLatin1(ks.key0.toHex())); }
        if (ks.key1.size() == 16) { k1->setText(QString::fromLatin1(ks.key1.toHex())); }
    };
    // One loco for all of it. Key sets and randoms are per loco, and mixing
    // one loco's set with another's random derives a key that never existed
    // on either link.
    const int loco = selectedLoco();
    if (s.haveSet(0, loco)) { putSet(m_s1Start, m_s1End, m_s1Key0, m_s1Key1, s.keySet(0, loco)); }
    if (s.haveSet(1, loco)) { putSet(m_s2Start, m_s2End, m_s2Key0, m_s2Key1, s.keySet(1, loco)); }
    if (s.haveRandoms(loco)) {
        m_locoRnd->setText(QStringLiteral("0x%1").arg(s.locoRandom(loco), 4, 16, QChar('0')));
        m_stnRnd->setText(QStringLiteral("0x%1").arg(s.stnRandom(loco), 4, 16, QChar('0')));
    }
    if (s.locoId(loco) >= 0) { m_locoId->setValue(s.locoId(loco)); }
    if (s.stnId(loco)  >= 0) { m_stnId->setValue(s.stnId(loco)); }
    m_explain->setText(loco < 0
        ? tr("Loaded the live values captured from the log, for the active loco.")
        : tr("Loaded the live values captured from the log, for loco %1.").arg(loco));
    m_explain->setStyleSheet(QString());
}

void SessionKeyDialog::onUseNow()
{
    m_now->setDateTime(QDateTime::currentDateTime());
}

void SessionKeyDialog::onParseRandLine()
{
    const CaptureLine cl = CaptureDecoder::parseLine(m_randLine->text().trimmed());
    if (cl.type != CapType::Random || cl.bytes.size() < 4) {
        m_explain->setText(tr("That doesn't look like a @rand_num line (need 4 payload bytes)."));
        m_explain->setStyleSheet(UiColor::errorStyle());
        return;
    }
    const auto b = reinterpret_cast<const quint8 *>(cl.bytes.constData());
    const quint16 loco = quint16(b[0] | (b[1] << 8));   // little-endian
    const quint16 stn  = quint16(b[2] | (b[3] << 8));
    m_locoRnd->setText(QStringLiteral("0x%1").arg(loco, 4, 16, QChar('0')));
    m_stnRnd->setText(QStringLiteral("0x%1").arg(stn, 4, 16, QChar('0')));
    if (cl.locoId >= 0) { m_locoId->setValue(cl.locoId); }
    m_explain->setText(tr("Filled loco_random=0x%1, stn_random=0x%2 from the line.")
                           .arg(loco, 4, 16, QChar('0')).arg(stn, 4, 16, QChar('0')));
    m_explain->setStyleSheet(QString());
}

void SessionKeyDialog::onDerive()
{
    auto readKeySet = [](QDateTimeEdit *s, QDateTimeEdit *e,
                         QLineEdit *k0, QLineEdit *k1) {
        SessionKeyGen::KeySet ks;
        ks.start = s->dateTime(); ks.end = e->dateTime();
        ks.key0 = QByteArray::fromHex(k0->text().trimmed().toLatin1());
        ks.key1 = QByteArray::fromHex(k1->text().trimmed().toLatin1());
        return ks;
    };
    const auto set1 = readKeySet(m_s1Start, m_s1End, m_s1Key0, m_s1Key1);
    const auto set2 = readKeySet(m_s2Start, m_s2End, m_s2Key0, m_s2Key1);

    bool okL = false, okS = false;
    const qint64 loco = parseNum(m_locoRnd->text(), &okL);
    const qint64 stn  = parseNum(m_stnRnd->text(), &okS);
    if (!okL || !okS) {
        m_explain->setText(tr("Enter both loco_random and stn_random (dec or 0x)."));
        m_explain->setStyleSheet(UiColor::errorStyle());
        m_keyOut->clear(); m_copyBtn->setEnabled(false);
        return;
    }

    const auto r = SessionKeyGen::derive(set1, set2, quint16(loco), quint16(stn),
                                         m_locoId->value(), m_stnId->value(),
                                         m_now->dateTime());
    if (!r.ok) {
        m_keyOut->clear(); m_copyBtn->setEnabled(false);
        m_explain->setText(tr("Cannot derive: %1").arg(r.error));
        m_explain->setStyleSheet(UiColor::errorStyle());
        return;
    }

    m_keyOut->setText(QString::fromLatin1(r.sessionKey.toHex()));
    m_copyBtn->setEnabled(true);
    QString note = tr("Chose set %1, key %2.  %3")
                       .arg(r.setIndex + 1).arg(r.keyIndex + 1).arg(r.explanation);
    if (r.neitherWindowValid) {
        note += tr("\n⚠ current time is outside BOTH key sets' windows — using set 2 by the fallback rule.");
    }
    m_explain->setText(note);
    m_explain->setStyleSheet(QString());
}

void SessionKeyDialog::onCopyKey()
{
    if (!m_keyOut->text().isEmpty()) {
        QApplication::clipboard()->setText(m_keyOut->text());
        m_explain->setText(tr("Session key copied — paste it into the Packet Maker's "
                              "“Session key” field to sign a packet's MAC."));
        m_explain->setStyleSheet(QString());
    }
}
