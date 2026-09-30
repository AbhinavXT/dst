#include "decodeworkbench.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "capturedecoder.h"
#include "sessionkeystore.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTableWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Were the dark-theme values used in both themes: the green ran at 2.4:1
// on a white background.
QColor okColor()   { return UiColor::ok(); }
QColor failColor() { return UiColor::error(); }

// Canonical capture tokens, enum order. The token is what parseLine() keys on
// (note the NMS tokens differ from typeLabel(): "nmsflt", not "nms fault").
const char *const kTokens[] = {
    "aap", "arp", "arprecv", "slrp", "lsrp",
    "nmsflt", "nmshlth", "nmsrssi",
    "dlt", "dmi", "biu", "brk",
    "rfid",
    "dip1", "dip2", "dop1", "dop2",
    "ccsys", "dlsys", "aep", "linfo",
    "authkeys", "rand_num", "uba",
    "speed", "analog_top", "analog_bottom"
};

// Types that carry a verifiable frame CRC (see CaptureDecoder::verifyCrc).
// nmsrssi is intentionally excluded — its CRC always fails (firmware builder
// bug), so it can never be a positive auto-detect signal.
const char *const kCrcTokens[] = {
    "aap", "slrp", "aep", "arp", "arprecv", "lsrp",
    "nmsflt", "nmshlth", "rfid", "dmi", "ccsys", "dlsys"
};

// A stable ISO timestamp for synthetic lines. Value is irrelevant to decode/CRC
// (only the bytes and type matter), so a fixed string keeps results stable.
const char *const kSynthTs = "2000-01-01T00:00:00";

QString synthLine(const QString &token, const QString &hex)
{
    const QString tk = token.isEmpty() ? QStringLiteral("unknown") : token;
    return QStringLiteral("@%1_0_0 %2 0 %3").arg(tk, QLatin1String(kSynthTs), hex);
}

} // namespace

DecodeWorkbench::DecodeWorkbench(QWidget *parent, SessionKeyStore *keys)
    : QMainWindow(parent)
{
    m_keys = keys ? keys : new SessionKeyStore(this);
    setWindowTitle(tr("Decode Workbench"));
    WindowGeometry::makeResizableWindow(this);
    resize(760, 640);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("decodeWorkbench"));

    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // --- type row -------------------------------------------------------
    QHBoxLayout *typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel(tr("Type:"), central));
    m_typeBox = new QComboBox(central);
    m_typeBox->addItem(tr("Auto-detect (CRC)"), QString());      // sentinel: empty token
    for (const char *tk : kTokens) {
        m_typeBox->addItem(QString::fromLatin1(tk), QString::fromLatin1(tk));
    }
    m_typeBox->setToolTip(tr("Pick the packet type, or let CRC pick the family for you.\n"
                             "Auto-detect can only identify types that carry a verifiable CRC."));
    typeRow->addWidget(m_typeBox);
    // --- session key set picker -----------------------------------------
    // Which captured key material the MAC is checked against. "Auto" is the
    // log view's behaviour (whatever is in force for this frame's loco); the
    // named entries let a frame be tested against the material that was in
    // force when it was sent, which is the whole point of pasting an old
    // frame in here.
    typeRow->addWidget(new QLabel(tr("Session key:"), central));
    m_keyBox = new QComboBox(central);
    m_keyBox->setToolTip(tr("Which captured set of auth keys + randoms + ids to\n"
                            "derive the session key from when checking the MAC.\n"
                            "Sets appear here as the log produces them."));
    typeRow->addWidget(m_keyBox, 1);

    typeRow->addStretch(1);
    QPushButton *bClear = new QPushButton(tr("Clear"), central);
    typeRow->addWidget(bClear);
    root->addLayout(typeRow);

    // --- hex input ------------------------------------------------------
    m_input = new QPlainTextEdit(central);
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setPlaceholderText(
        tr("Paste a hex frame  (e.g.  91 99 C5 DE 04 1E …  or  9199C5DE041E…)\n"
           "or a whole capture line  (@slrp_1_1 2026-06-18T16:08:16 6933 91 99 …).\n"
           "Spaces, commas, colons and 0x prefixes are ignored."));
    m_input->setMaximumHeight(140);
    root->addWidget(m_input);

    // --- status line ----------------------------------------------------
    m_status = new StatusLine(central);
    m_status->state(tr("Paste a hex frame to decode."));
    m_status->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // --- decoded field table -------------------------------------------
    m_table = new QTableWidget(0, 2, central);
    m_table->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(m_table, 1);

    setCentralWidget(central);

    refreshKeySets();
    connect(m_keys, &SessionKeyStore::changed,
            this, &DecodeWorkbench::refreshKeySets);
    connect(m_keyBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ if (!m_updating) { decodeNow(); } });

    connect(m_input,   &QPlainTextEdit::textChanged, this, &DecodeWorkbench::decodeNow);
    connect(m_typeBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ decodeNow(); });
    connect(bClear, &QPushButton::clicked, this, [this]{
        m_input->clear();   // triggers decodeNow -> resets table/status
    });

    decodeNow();
}

void DecodeWorkbench::loadBuffer(const QString &text, const QString &typeToken)
{
    if (!m_input) { return; }

    if (!typeToken.isEmpty()) {
        const int idx = m_typeBox->findData(typeToken.toLower());
        if (idx >= 0) {
            m_updating = true;
            m_typeBox->setCurrentIndex(idx);
            m_updating = false;
        }
    }

    m_input->setPlainText(text.trimmed());
    m_input->moveCursor(QTextCursor::End);
    // setPlainText already fires textChanged -> decodeNow(); calling it again
    // costs one decode and guarantees the table matches the box even if that
    // ever stops being true.
    decodeNow();
}

void DecodeWorkbench::refreshKeySets()
{
    if (!m_keyBox) { return; }
    const int keep = m_keyBox->currentData().toInt();

    m_updating = true;                    // repopulating must not re-decode
    m_keyBox->clear();
    m_keyBox->addItem(tr("Auto (this frame's loco)"), 0);
    for (const KeySnapshot &k : (*m_keys).snapshots()) {
        m_keyBox->addItem(k.label(), k.id);
    }
    const int idx = m_keyBox->findData(keep);
    m_keyBox->setCurrentIndex(idx >= 0 ? idx : 0);
    m_updating = false;

    // A chosen set that has just disappeared changes the answer on screen, so
    // the table has to be redrawn; a repopulate that kept the choice does not.
    if (idx < 0 && keep != 0) { decodeNow(); }
}

QString DecodeWorkbench::cleanHex(const QString &raw, bool *ok, int *nbytes, QString *err) const
{
    if (ok)     { *ok = false; }
    if (nbytes) { *nbytes = 0; }
    if (err)    { err->clear(); }

    QString s = raw;
    s.remove(QRegularExpression(QStringLiteral("0[xX]")));              // 0x / 0X prefixes
    s.remove(QRegularExpression(QStringLiteral("[\\s,:;|_\\-]")));      // common separators
    if (s.isEmpty()) { if (ok) { *ok = true; } return QString(); }

    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f]+$"));
    if (!hexOnly.match(s).hasMatch()) {
        if (err) { *err = tr("Input has non-hex characters."); }
        return QString();
    }
    if (s.size() % 2 != 0) {
        if (err) { *err = tr("Odd number of hex digits (%1) \u2014 need whole bytes.").arg(s.size()); }
        return QString();
    }
    if (ok)     { *ok = true; }
    if (nbytes) { *nbytes = s.size() / 2; }
    return s.toUpper();
}

QString DecodeWorkbench::autoDetect(const QString &hex, QString *report) const
{
    QStringList hits;
    for (const char *tk : kCrcTokens) {
        const CaptureLine c = CaptureDecoder::parseLine(synthLine(QString::fromLatin1(tk), hex));
        if (c.crcChecked && c.crcOk) { hits << QString::fromLatin1(tk); }
    }
    if (report) {
        *report = hits.isEmpty()
            ? tr("no CRC match \u2014 a CRC-less type, or wrong/incomplete bytes")
            : tr("CRC verifies as: %1").arg(hits.join(QStringLiteral(", ")));
    }
    return hits.isEmpty() ? QString() : hits.first();
}

void DecodeWorkbench::decodeNow()
{
    if (m_updating) { return; }

    m_table->setRowCount(0);
    // The workbench describes what is in the box right now, so every line
    // it shows is a caption rather than news — nothing here expires. The
    // colour used to be passed in by the caller; the verb says what it means
    // instead, which is also how the CRC result stopped being "some colour".
    auto setStatus = [this](const QString &t, const QColor &c) {
        if (c.isValid() && c == UiColor::error()) { m_status->fail(t); }
        else if (c.isValid() && c == UiColor::ok()) { m_status->ok(t); }
        else { m_status->state(t); }
    };

    const QString raw = m_input->toPlainText();
    if (raw.trimmed().isEmpty()) {
        setStatus(tr("Paste a hex frame (or a full @\u2026 capture line) to decode."), QColor());
        return;
    }

    CaptureLine c;
    QString prefix;                                 // optional auto-detect note

    if (raw.trimmed().startsWith('@')) {
        // Whole capture line pasted: parse verbatim, sync the type box to it.
        c = CaptureDecoder::parseLine(raw.trimmed());
        if (!c.valid) {
            setStatus(tr("Not a valid @-capture line (expected "
                         "@type_loco_ctrl <ts> <seq> <hex\u2026>)."), failColor());
            return;
        }
        m_updating = true;
        const int idx = m_typeBox->findData(c.typeToken);
        m_typeBox->setCurrentIndex(idx >= 0 ? idx : 0);
        m_updating = false;
    } else {
        bool ok = false; int nb = 0; QString err;
        const QString hex = cleanHex(raw, &ok, &nb, &err);
        if (!ok)        { setStatus(err, failColor()); return; }
        if (nb == 0)    { setStatus(tr("Paste a hex frame to decode."), QColor()); return; }

        QString token = m_typeBox->currentData().toString();   // empty => Auto
        if (token.isEmpty()) {
            QString report;
            token = autoDetect(hex, &report);   // may be empty -> decodes as Unknown
            prefix = report + QStringLiteral("\n");
        }
        c = CaptureDecoder::parseLine(synthLine(token, hex));
        if (!c.valid) { setStatus(tr("Could not build a frame from those bytes."), failColor()); return; }
    }

    // ---- status: bytes, type, CRC verdict ----
    const QString typeStr = c.typeToken.isEmpty()
        ? tr("unknown") : c.typeToken;
    QString crcStr; QColor crcCol;
    if (c.crcChecked) {
        crcCol = c.crcOk ? okColor() : failColor();
        crcStr = c.crcOk ? tr("CRC \u2713 pass") : tr("CRC \u2717 FAIL");
    } else {
        crcCol = QColor();
        crcStr = tr("CRC \u2014 n/a (no recipe for this type)");
    }
    setStatus(prefix + tr("%1 bytes  \u00B7  type: %2  \u00B7  %3")
                          .arg(c.bytes.size()).arg(typeStr, crcStr), crcCol);

    // ---- field table ----
    const int keyId = m_keyBox ? m_keyBox->currentData().toInt() : 0;
    const QVector<FieldRow> rows = CaptureDecoder::describe(c, nullptr, keyId, nullptr, m_keys);
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        QTableWidgetItem *f = new QTableWidgetItem(rows.at(i).field);
        QTableWidgetItem *v = new QTableWidgetItem(rows.at(i).value);
        const QString val = rows.at(i).value;
        if (val == QLatin1String("FAIL"))      { v->setForeground(failColor()); }
        else if (val == QLatin1String("PASS")) { v->setForeground(okColor()); }
        m_table->setItem(i, 0, f);
        m_table->setItem(i, 1, v);
    }
}

void DecodeWorkbench::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("decodeWorkbench"));
    QMainWindow::closeEvent(event);
}
