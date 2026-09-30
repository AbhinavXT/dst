#include "packetmakerdialog.h"
#include "framenumberwatch.h"
#include "windowgeometry.h"
#include "statusline.h"
#include <QCloseEvent>
#include "sendguard.h"
#include "uicolors.h"
#include "uistyle.h"

#include "messageheader.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "subpacketwindow.h"

#include <QPalette>
#include "packetvariation.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QSizePolicy>
#include <QDateTime>
#include <QStringList>
#include <QRegularExpression>

// ---- small helpers ---------------------------------------------------------

namespace {

// Parse a decimal or 0x-hex integer, allowing a leading '-' for signed fields.
qint64 parseNum(const QString &s, bool *ok)
{
    QString t = s.trimmed();
    if (t.isEmpty()) { if (ok) { *ok = true; } return 0; }
    bool neg = false;
    if (t.startsWith('-')) { neg = true; t = t.mid(1).trimmed(); }
    qint64 v = t.startsWith("0x", Qt::CaseInsensitive) ? t.mid(2).toLongLong(ok, 16)
                                                       : t.toLongLong(ok, 10);
    return neg ? -v : v;
}

}  // namespace

// Accepts anything the operator is likely to have on the clipboard:
//
//    91 B9 AA E6 04 1E ...                      bare space-separated hex
//    @slrp_2_1 2026-08-26T15:10:42 1552 91 B9…  a whole capture line
//    0x91, 0xB9, 0xAA, …                        C array text
//    91B9AAE6041E…                              one unbroken hex string
//
// The rule is deliberately blunt: keep tokens that are exactly two hex digits
// and drop everything else. That throws away the @type tag, the ISO timestamp
// and the sequence number without needing to know the capture-line grammar —
// and none of those can be mistaken for a byte, since a timestamp is one long
// token and a sequence number is not two characters wide. The `@xxx_` tag is
// still worth reading first, so it comes back through captypeHint.
QByteArray PacketMakerDialog::parseBuffer(const QString &text, QString *captypeHint)
{
    if (captypeHint) {
        const QRegularExpression tag(QStringLiteral("@([A-Za-z][A-Za-z0-9]*)"));
        const auto m = tag.match(text);
        if (m.hasMatch()) { *captypeHint = m.captured(1).toLower(); }
    }

    QString t = text;

    // A whole capture line is "@slrp_1_1 2026-06-18T16:08:16 6933 91 99 …":
    // tag, timestamp, sequence number, then the bytes. The tag and the
    // timestamp are not hex and fall out on their own, but a decimal
    // sequence number like 6933 is perfectly good hex, and the run-splitting
    // branch below would prepend it to the frame as 0x69 0x33 — a two-byte
    // shift that decodes into plausible-looking nonsense.
    //
    // The preamble is recognised the same way CaptureDecoder::parseLine()
    // recognises it — @tag, ISO timestamp, numeric sequence — rather than by
    // position, so a line that merely starts with an @tag ("@slrp 91 99 C5")
    // keeps all of its bytes.
    {
        const QString trimmed = t.trimmed();
        if (trimmed.startsWith(QLatin1Char('@'))) {
            const QStringList tok = trimmed.split(
                QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (tok.size() > 3) {
                bool seqOk = false;
                tok.at(2).toUInt(&seqOk);
                if (seqOk
                    && QDateTime::fromString(tok.at(1), Qt::ISODate).isValid()) {
                    t = QStringList(tok.mid(3)).join(QLatin1Char(' '));
                }
            }
        }
    }

    t.replace(QLatin1Char(','), QLatin1Char(' '));
    t.replace(QLatin1Char(';'), QLatin1Char(' '));
    t.replace(QRegularExpression(QStringLiteral("0[xX]")), QStringLiteral(" "));

    const QStringList toks = t.split(QRegularExpression(QStringLiteral("\\s+")),
                                     Qt::SkipEmptyParts);
    static const QRegularExpression pair(QStringLiteral("^[0-9A-Fa-f]{2}$"));
    static const QRegularExpression runOfHex(QStringLiteral("^[0-9A-Fa-f]+$"));

    QByteArray out;
    for (const QString &tok : toks) {
        if (pair.match(tok).hasMatch()) {
            out.append(char(tok.toUShort(nullptr, 16)));
            continue;
        }
        // One long unbroken hex run (and nothing else useful found yet): split
        // it into pairs. Guarded on even length so a stray word is not eaten.
        if (out.isEmpty() && tok.size() > 2 && (tok.size() % 2) == 0
            && runOfHex.match(tok).hasMatch()) {
            for (int i = 0; i + 1 < tok.size(); i += 2) {
                out.append(char(tok.mid(i, 2).toUShort(nullptr, 16)));
            }
        }
    }
    return out;
}

QWidget *PacketMakerDialog::makeEditor(const Schema::FieldInfo &f,
                                       const Schema::Encoder &enc, QWidget *parent)
{
    if (!f.enumName.isEmpty()) {
        const auto choices = enc.enumChoices(f.enumName);
        if (!choices.isEmpty()) {
            auto *cb = new QComboBox(parent);
            cb->setEditable(true);                 // still allow a raw number
            for (const auto &c : choices) {
                cb->addItem(QStringLiteral("%1 — %2").arg(c.first).arg(c.second),
                            QVariant(qlonglong(c.first)));
            }
            cb->setCurrentIndex(0);
            return cb;
        }
    }
    auto *le = new QLineEdit(parent);
    le->setText(QStringLiteral("0"));
    le->setToolTip(QStringLiteral("%1 bits%2 — decimal or 0x-hex")
                       .arg(f.bits).arg(f.isSigned ? QStringLiteral(", signed") : QString()));
    return le;
}

qint64 PacketMakerDialog::secondsSinceMidnight(int bits)
{
    // Local time, matching the RTC the equipment stamps its own frames with
    // and the clock the log shows by default. If the console is switched to
    // UTC display this value does not follow it — the frame number is a
    // number in a packet, not a rendering of a time.
    const qint64 secs = QTime::currentTime().msecsSinceStartOfDay() / 1000;
    if (bits <= 0 || bits >= 63) { return secs; }
    const qint64 span = qint64(1) << bits;
    return secs % span;          // a narrow field wraps rather than saturates
}

qint64 PacketMakerDialog::seedFrameNumber(const FrameNumberWatch *watch, int bits, bool *fromLive)
{
    const FrameNumberWatch::Seen s = watch ? watch->latest() : FrameNumberWatch::Seen();
    if (s.valid()) {
        if (fromLive) { *fromLive = true; }
        if (bits > 0 && bits < 63) { return s.value & ((qint64(1) << bits) - 1); }
        return s.value;
    }
    // Nothing seen yet — a cold start, or a console pointed at nothing. The
    // clock is a plausible SHAPE for the field and nothing more, so the
    // caller is told which it got rather than being left to assume.
    if (fromLive) { *fromLive = false; }
    return secondsSinceMidnight(bits);
}

qint64 PacketMakerDialog::readEditor(QWidget *w)
{
    if (auto *cb = qobject_cast<QComboBox *>(w)) {
        const int i = cb->currentIndex();
        const QString typed = cb->currentText().trimmed();
        // If the text matches a known item, use its data; else parse a raw number.
        if (i >= 0 && cb->itemText(i) == cb->currentText()) {
            return cb->itemData(i).toLongLong();
        }
        // maybe the user picked an item then it matches; else parse the leading number
        const QRegularExpression re(QStringLiteral("^-?(?:0x[0-9A-Fa-f]+|\\d+)"));
        const auto m = re.match(typed);
        bool ok = false;
        const qint64 v = parseNum(m.hasMatch() ? m.captured(0) : typed, &ok);
        return ok ? v : (i >= 0 ? cb->itemData(i).toLongLong() : 0);
    }
    if (auto *le = qobject_cast<QLineEdit *>(w)) {
        bool ok = false; const qint64 v = parseNum(le->text(), &ok); return ok ? v : 0;
    }
    return 0;
}

void PacketMakerDialog::writeEditor(QWidget *w, qint64 v)
{
    if (auto *cb = qobject_cast<QComboBox *>(w)) {
        const int idx = cb->findData(QVariant(qlonglong(v)));
        if (idx >= 0) { cb->setCurrentIndex(idx); } else { cb->setEditText(QString::number(v)); }
        return;
    }
    if (auto *le = qobject_cast<QLineEdit *>(w)) { le->setText(QString::number(v)); }
}

// ---- construction ----------------------------------------------------------

PacketMakerDialog::PacketMakerDialog(QWidget *parent, FrameNumberWatch *frameWatch, SessionKeyStore *keys)
    : QDialog(parent), m_frameWatch(frameWatch)
{
    m_keys = keys ? keys : new SessionKeyStore(this);
    setWindowTitle(tr("Packet Maker"));
    WindowGeometry::makeResizableWindow(this);
    resize(1080, 860);

    auto *root = new QVBoxLayout(this);

    // top row: packet type + destination + session key
    auto *top = new QHBoxLayout;
    m_packetCombo = new QComboBox(this);
    if (m_builder.ready()) {
        for (const QString &n : m_builder.encoder().packetNames()) { m_packetCombo->addItem(n); }
        const int slrp = m_packetCombo->findText(QStringLiteral("slrp"));
        if (slrp >= 0) { m_packetCombo->setCurrentIndex(slrp); }
    }
    top->addWidget(new QLabel(tr("Packet:"), this));
    top->addWidget(m_packetCombo);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Dest:"), this));
    m_destEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_destEdit->setMaximumWidth(140);
    top->addWidget(m_destEdit);
    top->addWidget(new QLabel(tr("Port:"), this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(20000);
    top->addWidget(m_portSpin);
    top->addStretch();
    root->addLayout(top);

    // The first destination is the pair above; three more live in a group
    // that starts folded, so a single-peer send looks exactly as it did.
    m_destHost[0] = m_destEdit;
    m_destPort[0] = m_portSpin;

    auto *destBox = new QGroupBox(tr("Also send to"), this);
    destBox->setCheckable(true);
    destBox->setChecked(false);
    auto *destOuter = new QVBoxLayout(destBox);
    auto *destBody  = new QWidget(destBox);
    auto *destGrid  = new QGridLayout(destBody);
    destGrid->setContentsMargins(0, 0, 0, 0);
    for (int i = 1; i < kMaxDests; ++i) {
        m_destOn[i] = new QCheckBox(tr("Destination %1").arg(i + 1), destBody);
        m_destHost[i] = new QLineEdit(destBody);
        m_destHost[i]->setPlaceholderText(tr("host or address"));
        m_destHost[i]->setMaximumWidth(160);
        m_destPort[i] = new QSpinBox(destBody);
        m_destPort[i]->setRange(1, 65535);
        m_destPort[i]->setValue(20000);
        destGrid->addWidget(m_destOn[i],   i - 1, 0);
        destGrid->addWidget(m_destHost[i], i - 1, 1);
        destGrid->addWidget(new QLabel(tr("Port:"), destBody), i - 1, 2);
        destGrid->addWidget(m_destPort[i], i - 1, 3);
    }
    destGrid->setColumnStretch(4, 1);
    destOuter->addWidget(destBody);
    destBody->setVisible(false);
    connect(destBox, &QGroupBox::toggled, destBody, &QWidget::setVisible);
    root->addWidget(destBox);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Session key (32 hex, optional):"), this));
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setPlaceholderText(tr("leave blank for a zero MAC placeholder"));
    m_keyEdit->setMaxLength(32);
    keyRow->addWidget(m_keyEdit, 1);

    // Captured key sets from the log. Picking one FILLS the field above rather
    // than replacing it as the source of truth: the built frame is still signed
    // with whatever the field says, so the operator can take a captured key and
    // then edit it, and there is one path into the builder rather than two.
    keyRow->addWidget(new QLabel(tr("from log:"), this));
    m_keySetBox = new QComboBox(this);
    m_keySetBox->setToolTip(tr("Session keys derived from key sets seen in the log.\n"
                               "Choosing one copies it into the field on the left."));
    keyRow->addWidget(m_keySetBox, 1);
    root->addLayout(keyRow);

    refreshKeySets();
    connect(m_keys, &SessionKeyStore::changed,
            this, &PacketMakerDialog::refreshKeySets);
    connect(m_keySetBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const int id = m_keySetBox->currentData().toInt();
        if (id <= 0) { return; }
        const KeySnapshot *k = (*m_keys).snapshot(id);
        if (k && k->result.ok) {
            m_keyEdit->setText(QString::fromLatin1(k->result.sessionKey.toHex()));
        }
    });

    // Message header (prepended on send; excluded from CRC/MAC).
    m_hdrBox = new QGroupBox(tr("Message header (prepended on send, little-endian)"), this);
    auto *hh = new QHBoxLayout(m_hdrBox);
    hh->addWidget(new QLabel(tr("src_id:"), m_hdrBox));
    m_srcSpin = new QSpinBox(m_hdrBox); m_srcSpin->setRange(0, 255); m_srcSpin->setValue(7);
    hh->addWidget(m_srcSpin);
    hh->addWidget(new QLabel(tr("dest_id:"), m_hdrBox));
    m_destSpin = new QSpinBox(m_hdrBox); m_destSpin->setRange(0, 255); m_destSpin->setValue(2);
    hh->addWidget(m_destSpin);
    m_msgIdLabel = new QLabel(tr("message_id: —"), m_hdrBox);
    hh->addWidget(m_msgIdLabel);
    hh->addWidget(new QLabel(tr("seq_num:"), m_hdrBox));
    m_seqSpin = new QSpinBox(m_hdrBox); m_seqSpin->setRange(0, 65535); m_seqSpin->setValue(1);
    m_seqSpin->setToolTip(tr("advances by one per datagram sent"));
    hh->addWidget(m_seqSpin);
    hh->addStretch();
    root->addWidget(m_hdrBox);

    // Extra header fields: operator-chosen values placed after the 8-byte
    // header and before the packet — station_id, loco_id, anything else the
    // receiver expects in its envelope. Collapsed by default like the other
    // occasional panels, but the title always states what is enabled, so a
    // field cannot be going out on the wire from behind a closed box.
    m_extraBox = new QGroupBox(this);
    m_extraBox->setCheckable(true);
    m_extraBox->setChecked(false);
    auto *exOuter = new QVBoxLayout(m_extraBox);
    auto *exBody  = new QWidget(m_extraBox);
    auto *exLay   = new QVBoxLayout(exBody);
    exLay->setContentsMargins(0, 0, 0, 0);
    // The explanation is a tooltip, not a label: a wrapped paragraph cost two
    // lines of height the field editors below need more.
    m_extraBox->setToolTip(
        tr("Ticked rows are inserted after the message header and before the packet, "
           "in table order.\nCounted in message_length; not covered by CRC or MAC.\n"
           "Values: decimal or 0x-hex, unsigned; a value that does not fit is refused."));
    m_extraTable = new QTableWidget(0, 5, exBody);
    m_extraTable->setHorizontalHeaderLabels(
        { tr("Send"), tr("Name"), tr("Type"), tr("Value"), tr("Byte order") });
    // Name takes the slack; the rest are sized to what they hold, so the type
    // combo is never clipped to "uint16 ↕".
    m_extraTable->horizontalHeader()->setStretchLastSection(false);
    // Name and Value share the slack. Type and Byte order hold combo boxes,
    // which ResizeToContents cannot see, so appendExtraRow() sizes them from
    // the combos themselves.
    m_extraTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_extraTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_extraTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_extraTable->verticalHeader()->setVisible(false);
    m_extraTable->verticalHeader()->setDefaultSectionSize(
        m_extraTable->fontMetrics().height() + 12);
    exLay->addWidget(m_extraTable);
    auto *exBtns = new QHBoxLayout;
    auto *exAdd = new QPushButton(tr("Add field"), exBody);
    auto *exDel = new QPushButton(tr("Remove field"), exBody);
    exBtns->addWidget(exAdd);
    exBtns->addWidget(exDel);
    exBtns->addStretch(1);
    exLay->addLayout(exBtns);
    exOuter->addWidget(exBody);
    exBody->setVisible(false);
    connect(m_extraBox, &QGroupBox::toggled, exBody, &QWidget::setVisible);
    connect(exAdd, &QPushButton::clicked, this, &PacketMakerDialog::onExtraAdd);
    connect(exDel, &QPushButton::clicked, this, &PacketMakerDialog::onExtraRemove);
    connect(m_extraTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *) { onExtrasEdited(); });
    root->addWidget(m_extraBox);
    setExtras(MessageHeader::defaultExtras());

    // Fill from buffer: paste a captured frame, get every field populated.
    //
    // Collapsed by default. It is used once at the start of a session and
    // then never again, and it was costing about 140 px of the window
    // permanently — spent on a paste box, taken from the field editors that
    // are the reason the dialog is open.
    auto *bufBox = new QGroupBox(tr("Fill from buffer"), this);
    bufBox->setCheckable(true);
    bufBox->setChecked(false);
    auto *bufLay = new QVBoxLayout(bufBox);
    auto *bufBody = new QWidget(bufBox);
    auto *bufBodyLay = new QVBoxLayout(bufBody);
    bufBodyLay->setContentsMargins(0, 0, 0, 0);
    m_bufEdit = new QPlainTextEdit(bufBox);
    m_bufEdit->setPlaceholderText(
        tr("Paste space-separated bytes — e.g.  91 B9 AA E6 04 1E 80 …\n"
           "A whole @slrp capture line works too; the tag, timestamp and "
           "sequence number are ignored."));
    m_bufEdit->setFont(UiStyle::monoFont());
    m_bufEdit->setMaximumHeight(74);
    bufBodyLay->addWidget(m_bufEdit);
    auto *bufBtns = new QHBoxLayout;
    auto *loadBtn  = new QPushButton(tr("Fill fields from buffer"), bufBox);
    auto *clearBtn = new QPushButton(tr("Clear"), bufBox);
    m_bufStatus = new QLabel(QString(), bufBox);
    m_bufStatus->setWordWrap(true);
    bufBtns->addWidget(loadBtn);
    bufBtns->addWidget(clearBtn);
    bufBtns->addWidget(m_bufStatus, 1);
    bufBodyLay->addLayout(bufBtns);
    bufLay->addWidget(bufBody);
    // A checkable QGroupBox only DISABLES its children; folding it away is
    // what buys the space back.
    bufBody->setVisible(false);
    connect(bufBox, &QGroupBox::toggled, bufBody, &QWidget::setVisible);
    root->addWidget(bufBox);

    // middle: [header form] | [sub-packets], resizable via a splitter
    auto *mid = new QSplitter(Qt::Horizontal, this);
    mid->setChildrenCollapsible(false);

    auto *hdrBox = new QGroupBox(tr("Header fields"), this);
    auto *hdrBoxLay = new QVBoxLayout(hdrBox);

    // Filter row. linfo carries 171 header fields, dmi 88, ccsys 56; a flat
    // form that long is not searchable by eye, and the field you want is
    // usually one you can already name.
    auto *filtRow = new QHBoxLayout;
    m_fieldFilter = new QLineEdit(hdrBox);
    m_fieldFilter->setPlaceholderText(tr("Filter fields…"));
    m_fieldFilter->setClearButtonEnabled(true);
    m_onlyChanged = new QCheckBox(tr("Changed only"), hdrBox);
    m_onlyChanged->setEnabled(false);
    m_onlyChanged->setToolTip(tr("Show only fields that differ from the frame "
                                 "loaded via Fill from buffer."));
    m_diffBtn = new QPushButton(tr("Diff…"), hdrBox);
    m_diffBtn->setEnabled(false);
    m_diffBtn->setToolTip(tr("List every field that differs from the loaded "
                             "frame, with both values."));
    filtRow->addWidget(m_fieldFilter, 1);
    filtRow->addWidget(m_onlyChanged);
    filtRow->addWidget(m_diffBtn);
    hdrBoxLay->addLayout(filtRow);

    auto *hdrScroll = new QScrollArea(hdrBox);
    hdrScroll->setWidgetResizable(true);
    m_headerHost = new QWidget;
    m_headerForm = new QFormLayout(m_headerHost);
    hdrScroll->setWidget(m_headerHost);
    hdrBoxLay->addWidget(hdrScroll);
    mid->addWidget(hdrBox);

    m_subBox = new QGroupBox(tr("Sub-packets"), this);
    QGroupBox *subBox = m_subBox;
    auto *subLay = new QVBoxLayout(subBox);

    m_subList = new QListWidget(subBox);
    m_subList->setToolTip(tr("Click a sub-packet to edit its fields in its own window."));
    subLay->addWidget(m_subList, 1);

    auto *subBtns = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add…"), subBox);
    auto *rmBtn  = new QPushButton(tr("Remove"), subBox);
    m_editSubBtn = new QPushButton(tr("Edit fields…"), subBox);
    m_editSubBtn->setEnabled(false);
    subBtns->addWidget(addBtn);
    subBtns->addWidget(rmBtn);
    subBtns->addWidget(m_editSubBtn);
    subBtns->addStretch(1);
    subLay->addLayout(subBtns);

    mid->addWidget(subBox);

    // give the sub-packet side the larger share (its repeat tables are wide)
    m_mid = mid;
    mid->setStretchFactor(0, 3);
    mid->setStretchFactor(1, 5);
    mid->setSizes({ 400, 660 });

    // The middle and the preview share the remaining height through a
    // splitter rather than the preview taking a fixed slice. On a packet
    // with 171 header fields the preview does not need 200 px and the form
    // does.
    auto *vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->setChildrenCollapsible(false);
    vsplit->addWidget(mid);
    root->addWidget(vsplit, 1);
    m_vsplit = vsplit;

    // preview + status
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setFont(UiStyle::monoFont());
    m_preview->setMinimumHeight(90);
    vsplit->addWidget(m_preview);
    vsplit->setStretchFactor(0, 4);
    vsplit->setStretchFactor(1, 1);
    m_status = new StatusLine(this);
    m_status->say(tr("Not built yet."));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // Vary per send. Interval sending rebuilds the frame every tick and
    // applies these rules, so a repeated send is a stream rather than the
    // same bytes with a new sequence number on the envelope.
    auto *varyBox = new QGroupBox(tr("Vary per send (interval mode)"), this);
    auto *varyLay = new QVBoxLayout(varyBox);
    m_varyTable = new QTableWidget(0, 5, varyBox);
    m_varyTable->setHorizontalHeaderLabels(
        { tr("Field"), tr("Mode"), tr("Start / min"), tr("Step"), tr("Max (0 = field width)") });
    m_varyTable->horizontalHeader()->setStretchLastSection(true);
    m_varyTable->verticalHeader()->setVisible(false);
    m_varyTable->setMaximumHeight(120);
    varyLay->addWidget(m_varyTable);
    auto *varyBtns = new QHBoxLayout;
    m_varyAddBtn = new QPushButton(tr("Add rule"), varyBox);
    m_varyDelBtn = new QPushButton(tr("Remove rule"), varyBox);
    varyBtns->addWidget(m_varyAddBtn);
    varyBtns->addWidget(m_varyDelBtn);
    varyBtns->addStretch(1);
    varyLay->addLayout(varyBtns);
    root->addWidget(varyBox);

    // bottom buttons
    auto *btns = new QHBoxLayout;
    m_buildBtn = new QPushButton(tr("Build && Verify"), this);
    m_sendOnceBtn = new QPushButton(tr("Send Once"), this);
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(10, 600000);
    m_intervalSpin->setValue(1000);
    m_intervalSpin->setSuffix(tr(" ms"));
    m_intervalBtn = new QPushButton(tr("Start Interval"), this);
    auto *saveBtn = new QPushButton(tr("Save preset…"), this);
    auto *loadBtn2 = new QPushButton(tr("Load preset…"), this);
    btns->addWidget(m_buildBtn);
    btns->addStretch();

    m_armed = new QCheckBox(tr("Arm to enable sending"), this);
    m_armed->setToolTip(tr("Sending is disabled until this is ticked.\n"
                           "It clears when the packet type, a buffer or a "
                           "preset replaces the frame."));
    connect(m_armed, &QCheckBox::toggled, this, [this](bool on) {
        refreshArmState();
        m_status->say(on ? tr("Armed. Send and Start Interval go out with no "
                              "further confirmation.")
                         : tr("Disarmed."));
    });
    btns->addWidget(m_armed);

    btns->addWidget(m_sendOnceBtn);
    btns->addWidget(new QLabel(tr("every"), this));
    btns->addWidget(m_intervalSpin);
    btns->addWidget(m_intervalBtn);
    btns->addSpacing(16);
    btns->addWidget(saveBtn);
    btns->addWidget(loadBtn2);
    root->addLayout(btns);

    setSendEnabled(false);

    connect(m_packetCombo, &QComboBox::currentTextChanged, this, &PacketMakerDialog::onPacketChanged);
    connect(addBtn, &QPushButton::clicked, this, &PacketMakerDialog::onAddSub);
    connect(rmBtn,  &QPushButton::clicked, this, &PacketMakerDialog::onRemoveSub);
    connect(m_subList, &QListWidget::itemClicked, this, [this](QListWidgetItem *){ onSubActivated(); });
    connect(m_editSubBtn, &QPushButton::clicked, this, &PacketMakerDialog::onSubActivated);
    connect(m_subList, &QListWidget::currentRowChanged, this, &PacketMakerDialog::onSubSelectionChanged);
    connect(m_buildBtn, &QPushButton::clicked, this, &PacketMakerDialog::onBuild);
    connect(loadBtn,  &QPushButton::clicked, this, &PacketMakerDialog::onLoadBuffer);
    connect(clearBtn, &QPushButton::clicked, this, &PacketMakerDialog::onClearBuffer);
    connect(m_sendOnceBtn, &QPushButton::clicked, this, &PacketMakerDialog::onSendOnce);
    connect(m_intervalBtn, &QPushButton::clicked, this, &PacketMakerDialog::onToggleInterval);
    connect(m_varyAddBtn,  &QPushButton::clicked, this, &PacketMakerDialog::onVaryAdd);
    connect(m_varyDelBtn,  &QPushButton::clicked, this, &PacketMakerDialog::onVaryRemove);
    connect(m_fieldFilter, &QLineEdit::textChanged, this, &PacketMakerDialog::onFieldFilterChanged);
    connect(m_onlyChanged, &QCheckBox::toggled, this, [this](bool){ applyFieldFilter(); });
    connect(m_diffBtn,     &QPushButton::clicked, this, &PacketMakerDialog::onShowDiff);
    connect(saveBtn,       &QPushButton::clicked, this, &PacketMakerDialog::onSavePreset);
    connect(loadBtn2,      &QPushButton::clicked, this, &PacketMakerDialog::onLoadPreset);
    connect(&m_sender, &UdpSender::error, this, &PacketMakerDialog::onSenderError);
    connect(&m_sender, &UdpSender::sent, this, [this](int n) {
        // Frames, not datagrams: with four destinations one send is one frame
        // that went to four places, and reporting it as four would suggest
        // four different frames — which is exactly what does NOT happen, since
        // the send index advances once.
        const int d = m_sender.targetCount();
        m_status->ok(d > 1 ? tr("Sent %1 frame(s) to %2 destinations: %3.")
                                 .arg(n).arg(d).arg(destinationSummary())
                           : tr("Sent %1 datagram(s) to %2.")
                                 .arg(n).arg(destinationSummary()));
    });
    connect(&m_sender, &UdpSender::runningChanged, this, [this](bool run) {
        m_intervalBtn->setText(run ? tr("Stop") : tr("Start Interval"));
        // A running stream keeps the envelope it started with; locking the
        // table says so, instead of letting an edit look like it took effect.
        if (m_extraTable) { m_extraTable->setEnabled(!run); }
    });

    if (!m_builder.ready()) {
        m_status->fail(tr("Schema failed to load — cannot build packets."));
        m_buildBtn->setEnabled(false);
    } else {
        onPacketChanged();
    }
}

// ---- header form -----------------------------------------------------------

void PacketMakerDialog::rebuildHeaderForm()
{
    // clear
    m_headerEditors.clear();
    while (m_headerForm->count() > 0) {
        QLayoutItem *it = m_headerForm->takeAt(0);
        if (!it) { break; }
        if (it->widget()) { it->widget()->deleteLater(); }
        delete it;
    }

    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    if (!pi.ok) { return; }

    // Only SLRP and AEP carry sub-packets. For every other packet that pane
    // was an empty list taking a third of the width, while the header form —
    // the thing actually being edited — was squeezed into what was left.
    // LSRP has 25 fields; LINFO has 171.
    if (m_subBox) {
        m_subBox->setVisible(pi.hasSub);
        if (m_mid) {
            m_mid->setSizes(pi.hasSub ? QList<int>{ 400, 660 }
                                      : QList<int>{ qMax(400, width()), 0 });
        }
    }
    for (const Schema::FieldInfo &f : pi.header) {
        if (f.isPad || f.isCrc) { continue; }   // a <crc> is computed, not asked for (session 92)
        // PKT_LENGTH is computed by the builder; show it read-only.
        QWidget *ed = makeEditor(f, m_builder.encoder(), m_headerHost);
        // PKT_LENGTH and MAC_CODE are computed by the builder — show read-only.
        if (f.name == QLatin1String("PKT_LENGTH") || f.name == QLatin1String("MAC_CODE")) {
            ed->setEnabled(false);
            if (auto *le = qobject_cast<QLineEdit *>(ed)) { le->setText(tr("auto")); }
        }

        if (f.name == QLatin1String("FRAME_NUM")) {
            // Seeded from the LIVE frame number — the one ARP and LSRP are
            // carrying right now — falling back to seconds since midnight
            // when no traffic has been seen. The clock has the right shape
            // but not the right number: it is whatever this laptop says,
            // while the counter a peer checks against is whatever the
            // equipment says.
            bool live = false;
            const int bits = f.bits;
            writeEditor(ed, seedFrameNumber(m_frameWatch, bits, &live));

            auto *row  = new QWidget(m_headerHost);
            auto *hb   = new QHBoxLayout(row);
            hb->setContentsMargins(0, 0, 0, 0);
            hb->setSpacing(UiStyle::gap());
            hb->addWidget(ed, 1);

            // Following is the default because the live number is the useful
            // one, and it stops the moment the operator types in the field:
            // an edit is a decision, and quietly overwriting it a second
            // later would be the worst behaviour available here.
            m_followFrameNum = new QCheckBox(tr("Follow live"), row);
            m_followFrameNum->setChecked(true);
            m_followFrameNum->setToolTip(
                tr("Track the frame number seen in live ARP/LSRP traffic.\n"
                   "Editing the field turns this off."));
            hb->addWidget(m_followFrameNum);

            auto *now = new QPushButton(tr("Now"), row);
            now->setToolTip(tr("Take the current frame number from live traffic, "
                               "or the clock if none has been seen."));
            connect(now, &QPushButton::clicked, this, [this, ed, bits] {
                bool fromLive = false;
                writeEditor(ed, seedFrameNumber(m_frameWatch, bits, &fromLive));
                m_lastFrame.clear();
                setSendEnabled(false);
                m_status->say(fromLive
                    ? tr("Frame number taken from live traffic — Build && Verify again.")
                    : tr("No ARP/LSRP seen yet; used the clock instead — "
                         "Build && Verify again."));
            });
            hb->addWidget(now);

            // Live updates. Only while following, and only into the editor —
            // the built frame is deliberately left stale so Send stays
            // disabled until the operator rebuilds with the new number.
            if (m_frameWatch) connect(m_frameWatch, &FrameNumberWatch::observed,
                    ed, [this, ed, bits](qint64 v, int) {
                        if (!m_followFrameNum || !m_followFrameNum->isChecked()) { return; }
                        if (m_sender.isRunning()) { return; }   // don't fight a live send
                        m_writingFrameNum = true;
                        writeEditor(ed, bits > 0 && bits < 63
                                            ? (v & ((qint64(1) << bits) - 1)) : v);
                        m_writingFrameNum = false;

                        // And rebuild, so the frame sitting behind the Send
                        // button is the one for THIS number, with its own MAC
                        // and CRC. Showing a new number next to a frame built
                        // for the old one is the worst of both.
                        rebuildForNewFrameNumber();
                    });

            // Typing in the field is a decision; stop following. The guard
            // matters for the spin-box editor, whose valueChanged fires for
            // our own writes as well as the operator's — without it, the
            // first live update would switch following off and the feature
            // would work exactly once.
            auto stopFollowing = [this] {
                if (m_writingFrameNum) { return; }
                if (m_followFrameNum) { m_followFrameNum->setChecked(false); }
            };
            if (auto *le = qobject_cast<QLineEdit *>(ed)) {
                connect(le, &QLineEdit::textEdited, this,
                        [stopFollowing](const QString &) { stopFollowing(); });
            }
            if (auto *sb = qobject_cast<QSpinBox *>(ed)) {
                connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
                        [stopFollowing](int) { stopFollowing(); });
            }

            ed->setToolTip(live
                ? tr("Taken from live ARP/LSRP traffic. Edit freely — editing "
                     "stops it following.")
                : tr("No ARP/LSRP seen yet, so this is seconds since local "
                     "midnight. It will follow live traffic once some arrives."));

            m_headerForm->addRow(f.name, row);
            m_headerEditors.insert(f.name, ed);   // the EDITOR, not the row
            continue;
        }

        m_headerForm->addRow(f.name, ed);
        m_headerEditors.insert(f.name, ed);
    }
    if (!pi.unsupported.isEmpty()) {
        auto *warn = new QLabel(tr("⚠ header uses %1 — not buildable yet")
                                    .arg(pi.unsupported.join(", ")), m_headerHost);
        warn->setStyleSheet(UiColor::errorStyle());
        m_headerForm->addRow(warn);
    }
}

// ---- subpackets ------------------------------------------------------------

PacketMakerDialog::~PacketMakerDialog() = default;

// The three field-editor entry points are thin forwarders to the existing
// private statics, so SubPacketWindow builds exactly the same widgets from
// exactly the same schema information.
QWidget *PacketMakerDialog::makeFieldEditor(const Schema::FieldInfo &f,
                                            const Schema::Encoder &enc, QWidget *parent)
{ return makeEditor(f, enc, parent); }

qint64 PacketMakerDialog::readFieldEditor(QWidget *w)          { return readEditor(w); }
void   PacketMakerDialog::writeFieldEditor(QWidget *w, qint64 v) { writeEditor(w, v); }

void PacketMakerDialog::rebuildSubEditor()
{
    const bool have = (m_curSub >= 0 && m_curSub < m_subs.size());
    if (m_editSubBtn) { m_editSubBtn->setEnabled(have); }

    // Retarget an already-open window as the selection moves, but do not
    // conjure one: the window opens on a click or the Edit button, so
    // programmatic selection (loading a preset, filling from a buffer) does
    // not throw a window in the operator's face.
    if (!m_subWindow) { return; }
    if (!have) { m_subWindow->clearTarget(); return; }
    m_subWindow->setTarget(&m_builder.encoder(), m_packetCombo->currentText(),
                           &m_subs, m_curSub);
}

void PacketMakerDialog::commitCurrentSub()
{
    if (m_subWindow) { m_subWindow->commit(); }
}

void PacketMakerDialog::onSubActivated()
{
    const int row = m_subList->currentRow();
    if (row < 0 || row >= m_subs.size()) { return; }

    if (!m_subWindow) {
        m_subWindow = new SubPacketWindow(this);
        connect(m_subWindow, &SubPacketWindow::edited, this, [this]() {
            // Any edit invalidates the verified frame: what was built is no
            // longer what the form says, and sending it would send the old
            // bytes under the new intent.
            m_lastFrame.clear();
            setSendEnabled(false);
            m_status->say(tr("Sub-packet edited — Build && Verify again."));
        });
    }
    m_subWindow->setTarget(&m_builder.encoder(), m_packetCombo->currentText(),
                           &m_subs, row);
    m_subWindow->show();
    m_subWindow->raise();
    m_subWindow->activateWindow();
}

void PacketMakerDialog::onAddSub()
{
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    if (!pi.hasSub) {
        QMessageBox::information(this, windowTitle(), tr("This packet has no sub-packets."));
        return;
    }
    QStringList labels; QVector<int> types;
    for (const Schema::CaseInfo &c : pi.cases) {
        labels << QStringLiteral("type %1 — %2").arg(c.type).arg(c.structName);
        types << c.type;
    }
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, tr("Add sub-packet"),
                                               tr("Type:"), labels, 0, false, &ok);
    if (!ok) { return; }
    const int idx = labels.indexOf(pick);
    if (idx < 0) { return; }

    commitCurrentSub();
    Schema::SubEntry se; se.type = types[idx];
    m_subs.push_back(se);
    m_subList->addItem(pick);
    m_subList->setCurrentRow(m_subs.size() - 1);
    // A sub-packet you just added is one you are about to fill in, so open
    // the editor rather than making the operator click the row they are
    // already standing on.
    onSubActivated();
}

void PacketMakerDialog::onRemoveSub()
{
    const int row = m_subList->currentRow();
    if (row < 0 || row >= m_subs.size()) { return; }
    // Drop the window's target BEFORE the vector shrinks: it edits through a
    // pointer and an index, and committing into a row that is going away
    // would write into the wrong sub-packet.
    if (m_subWindow) { m_subWindow->clearTarget(); }
    m_curSub = -1;
    m_subs.remove(row);
    delete m_subList->takeItem(row);
    onSubSelectionChanged();
}

void PacketMakerDialog::onSubSelectionChanged()
{
    commitCurrentSub();
    m_curSub = m_subList->currentRow();
    rebuildSubEditor();
}

// ---- build / send ----------------------------------------------------------

QHash<QString, qint64> PacketMakerDialog::readHeader() const
{
    QHash<QString, qint64> h;
    for (auto it = m_headerEditors.constBegin(); it != m_headerEditors.constEnd(); ++it) {
        if (it.key() == QLatin1String("PKT_LENGTH")) { continue; }   // auto
        if (it.key() == QLatin1String("MAC_CODE"))   { continue; }   // computed by builder
        h.insert(it.key(), readEditor(it.value()));
    }

    // While following, FRAME_NUM is read from the watch rather than from the
    // editor. The editor is only a display of it, and it is deliberately not
    // updated during a send — so reading the widget would send whatever was
    // on screen when the run started. The loco rejects a frame more than a
    // few seconds behind its own counter, which is exactly the failure this
    // closes.
    if (m_followFrameNum && m_followFrameNum->isChecked()
        && h.contains(QLatin1String("FRAME_NUM"))) {
        const FrameNumberWatch::Seen seen = m_frameWatch ? m_frameWatch->latest() : FrameNumberWatch::Seen();
        if (seen.valid()) {
            const int bits = headerFieldBits(QStringLiteral("FRAME_NUM"));
            h.insert(QStringLiteral("FRAME_NUM"),
                     (bits > 0 && bits < 63) ? (seen.value & ((qint64(1) << bits) - 1))
                                             : seen.value);
        }
    }
    return h;
}

void PacketMakerDialog::refreshKeySets()
{
    if (!m_keySetBox) { return; }
    const int keep = m_keySetBox->currentData().toInt();
    const QSignalBlocker block(m_keySetBox);   // repopulating must not overwrite the field
    m_keySetBox->clear();
    m_keySetBox->addItem(tr("(none — type a key)"), 0);
    for (const KeySnapshot &k : (*m_keys).snapshots()) {
        m_keySetBox->addItem(k.label(), k.id);
    }
    const int idx = m_keySetBox->findData(keep);
    m_keySetBox->setCurrentIndex(idx >= 0 ? idx : 0);
}

QByteArray PacketMakerDialog::sessionKey() const
{
    const QString hex = m_keyEdit->text().trimmed();
    if (hex.isEmpty()) { return {}; }
    const QByteArray k = QByteArray::fromHex(hex.toLatin1());
    return k;   // size validated downstream (must be 16)
}

PacketBuilder::Result PacketMakerDialog::buildNow(int sendIndex)
{
    commitCurrentSub();
    QHash<QString, qint64> header = readHeader();

    // Variation is applied to a COPY of what the form says, never written
    // back into the editors. Writing back would make the form show send N's
    // values while the operator is trying to edit send N+1, and would lose
    // the base value the rules are computed from.
    if (sendIndex >= 0) {
        // The live frame number is read HERE, once per send, rather than
        // inside the rule: packetvariation stays a pure value calculation
        // and the ingest-facing watch stays out of it.
        const FrameNumberWatch::Seen seen = m_frameWatch ? m_frameWatch->latest() : FrameNumberWatch::Seen();

        PacketVary::LiveFrame live;
        live.value = seen.valid() ? seen.value : -1;
        if (live.value != m_lastLiveFrameNum) {
            // The equipment moved on: re-anchor. A run that has been adding
            // to a stale number starts again from the new one, which is the
            // whole reason to follow the traffic rather than count.
            m_lastLiveFrameNum   = live.value;
            m_sendsAtSameLiveNum = 0;
        }
        live.sendsSinceChange = m_sendsAtSameLiveNum;
        ++m_sendsAtSameLiveNum;

        for (const PacketVary::Rule &r : readVaryRules()) {
            if (!r.enabled || r.field.isEmpty()) { continue; }
            if (!header.contains(r.field))       { continue; }
            header.insert(r.field,
                          r.valueFor(sendIndex, headerFieldBits(r.field), live));
        }
    }
    return m_builder.build(m_packetCombo->currentText(), header, m_subs, sessionKey());
}

int PacketMakerDialog::headerFieldBits(const QString &name) const
{
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    if (!pi.ok) { return 0; }
    for (const Schema::FieldInfo &f : pi.header) {
        if (!f.isPad && f.name == name) { return f.bits; }
    }
    return 0;
}

// ---- vary rules ------------------------------------------------------------

QVector<PacketVary::Rule> PacketMakerDialog::readVaryRules() const
{
    QVector<PacketVary::Rule> out;
    if (!m_varyTable) { return out; }
    for (int row = 0; row < m_varyTable->rowCount(); ++row) {
        auto *fieldCombo = qobject_cast<QComboBox *>(m_varyTable->cellWidget(row, 0));
        auto *modeCombo  = qobject_cast<QComboBox *>(m_varyTable->cellWidget(row, 1));
        if (!fieldCombo || !modeCombo) { continue; }
        PacketVary::Rule r;
        r.field = fieldCombo->currentText();
        r.mode  = PacketVary::modeFromName(modeCombo->currentData().toString());
        auto num = [this, row](int col) -> qint64 {
            QTableWidgetItem *it = m_varyTable->item(row, col);
            return it ? it->text().trimmed().toLongLong() : 0;
        };
        r.start = num(2);
        r.step  = num(3);
        r.max   = num(4);
        out.push_back(r);
    }
    return out;
}

void PacketMakerDialog::setVaryRules(const QVector<PacketVary::Rule> &rules)
{
    if (!m_varyTable) { return; }
    m_varyTable->setRowCount(0);
    for (const PacketVary::Rule &r : rules) {
        onVaryAdd();
        const int row = m_varyTable->rowCount() - 1;
        if (row < 0) { break; }
        if (auto *fc = qobject_cast<QComboBox *>(m_varyTable->cellWidget(row, 0))) {
            const int idx = fc->findText(r.field);
            if (idx >= 0) { fc->setCurrentIndex(idx); }
        }
        if (auto *mc = qobject_cast<QComboBox *>(m_varyTable->cellWidget(row, 1))) {
            const int idx = mc->findData(PacketVary::modeName(r.mode));
            if (idx >= 0) { mc->setCurrentIndex(idx); }
        }
        m_varyTable->setItem(row, 2, new QTableWidgetItem(QString::number(r.start)));
        m_varyTable->setItem(row, 3, new QTableWidgetItem(QString::number(r.step)));
        m_varyTable->setItem(row, 4, new QTableWidgetItem(QString::number(r.max)));
    }
}

void PacketMakerDialog::onVaryAdd()
{
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    if (!pi.ok) { return; }

    auto *fieldCombo = new QComboBox(m_varyTable);
    for (const Schema::FieldInfo &f : pi.header) {
        // PKT_LENGTH and MAC_CODE are recomputed by the builder on every
        // send, so varying them would be overwritten and misleading.
        if (f.isPad || f.isCrc) { continue; }
        if (f.name == QLatin1String("PKT_LENGTH") || f.name == QLatin1String("MAC_CODE")) { continue; }
        fieldCombo->addItem(f.name);
    }
    if (fieldCombo->count() == 0) { delete fieldCombo; return; }

    auto *modeCombo = new QComboBox(m_varyTable);
    modeCombo->addItem(tr("increment"), QStringLiteral("increment"));
    modeCombo->addItem(tr("sweep"),     QStringLiteral("sweep"));
    modeCombo->addItem(tr("random"),    QStringLiteral("random"));
    modeCombo->addItem(tr("from live"), QStringLiteral("live"));

    const int row = m_varyTable->rowCount();
    m_varyTable->insertRow(row);
    m_varyTable->setCellWidget(row, 0, fieldCombo);
    m_varyTable->setCellWidget(row, 1, modeCombo);
    m_varyTable->setItem(row, 2, new QTableWidgetItem(QStringLiteral("0")));
    m_varyTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("1")));
    m_varyTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("0")));
}

void PacketMakerDialog::onVaryRemove()
{
    const int row = m_varyTable->currentRow();
    if (row >= 0) { m_varyTable->removeRow(row); }
}

void PacketMakerDialog::rebuildVaryTable()
{
    if (m_varyTable) { m_varyTable->setRowCount(0); }
    seedDefaultVaryRule();
}

void PacketMakerDialog::seedDefaultVaryRule()
{
    // Pre-seed the rule that fixes the original defect, so the common case
    // needs no setup and the mechanism explains itself the first time the
    // table is seen. Anything else is opt-in.
    if (!m_varyTable || m_varyTable->rowCount() > 0) { return; }
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    if (!pi.ok) { return; }
    bool hasFrameNum = false;
    for (const Schema::FieldInfo &f : pi.header) {
        if (!f.isPad && f.name == QLatin1String("FRAME_NUM")) { hasFrameNum = true; break; }
    }
    if (!hasFrameNum) { return; }

    PacketVary::Rule r;
    r.field = QStringLiteral("FRAME_NUM");
    // Was "increment from 0". That advances a counter of OUR own, which
    // drifts away from the equipment's the moment it starts — the frame
    // number a peer checks against is the one in the ARP and LSRP arriving
    // now, not one this program invented. "From live" takes it from the
    // traffic on every send; with nothing observed it falls back to `start`,
    // which is seeded from the same place the form's field is.
    r.mode  = PacketVary::Rule::Live;
    bool live = false;
    r.start = seedFrameNumber(m_frameWatch, headerFieldBits(QStringLiteral("FRAME_NUM")), &live);
    // step 0: repeat the observed number for as long as it stands. Whether a
    // peer requires every frame to advance is a question about SIF 0533 and
    // not one to answer by guessing, so the default is the literal reading —
    // send what the equipment is sending — and step 1 is one cell away.
    r.step  = 0;
    r.max   = 0;                 // wrap at the field's own width
    setVaryRules({ r });
}

// ---- field filter and diff -------------------------------------------------

void PacketMakerDialog::onFieldFilterChanged() { applyFieldFilter(); }

void PacketMakerDialog::applyFieldFilter()
{
    const QString needle = m_fieldFilter ? m_fieldFilter->text().trimmed() : QString();
    const bool changedOnly = m_onlyChanged && m_onlyChanged->isChecked() && m_haveRef;

    for (auto it = m_headerEditors.constBegin(); it != m_headerEditors.constEnd(); ++it) {
        QWidget *ed = it.value();
        if (!ed) { continue; }

        bool show = true;
        if (!needle.isEmpty()) {
            show = it.key().contains(needle, Qt::CaseInsensitive);
        }
        if (show && changedOnly) {
            // PKT_LENGTH and MAC_CODE read "auto" and have no comparable
            // value, so they are never "changed".
            const bool computed = (it.key() == QLatin1String("PKT_LENGTH")
                                   || it.key() == QLatin1String("MAC_CODE"));
            show = !computed && m_refHeader.contains(it.key())
                   && m_refHeader.value(it.key()) != readEditor(ed);
        }

        ed->setVisible(show);
        if (QWidget *lab = m_headerForm->labelForField(ed)) { lab->setVisible(show); }
    }
}

// A "changed" tint that is readable on whichever palette is in force.
//
// The first attempt hardcoded a dark amber background and left the text
// colour alone. Under a light palette that is dark-on-dark: the field was
// marked and simultaneously made unreadable, which is worse than not marking
// it. Deriving both colours from the palette — and always setting the
// foreground alongside the background, never one without the other — is what
// makes that impossible rather than merely unlikely.
static QString changedTint(const QWidget *)
{
    // Both colours together, from the active palette. The branch that used
    // to be here picked the pair by hand; UiColor does that now, and the
    // foreground can no longer be left behind when the background changes.
    return QStringLiteral("background:%1; color:%2;")
        .arg(UiColor::bannerBg().name(), UiColor::bannerFg().name());
}

void PacketMakerDialog::markChangedFields()
{
    if (!m_haveRef) { return; }
    for (auto it = m_headerEditors.constBegin(); it != m_headerEditors.constEnd(); ++it) {
        QWidget *ed = it.value();
        if (!ed || !ed->isEnabled()) { continue; }
        const bool changed = m_refHeader.contains(it.key())
                             && m_refHeader.value(it.key()) != readEditor(ed);

        // Clearing back to an empty stylesheet restores the palette default
        // rather than guessing at "normal" colours, so a field that stops
        // being changed looks exactly like one that never was.
        ed->setStyleSheet(changed ? changedTint(ed) : QString());

        // The label carries the mark too. Colour alone fails for anyone who
        // cannot distinguish it, and the marker survives a theme this tint
        // was not anticipated for.
        if (QWidget *lab = m_headerForm->labelForField(ed)) {
            if (auto *l = qobject_cast<QLabel *>(lab)) {
                const QString plain = it.key();
                l->setText(changed ? QStringLiteral("● %1").arg(plain) : plain);
            }
        }
    }
}

void PacketMakerDialog::onShowDiff()
{
    if (!m_haveRef) { return; }

    QStringList lines;
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetCombo->currentText());
    // Walked in spec order, not hash order, so the list reads like the packet.
    for (const Schema::FieldInfo &f : pi.header) {
        if (f.isPad || f.isCrc) { continue; }
        if (f.name == QLatin1String("PKT_LENGTH") || f.name == QLatin1String("MAC_CODE")) { continue; }
        QWidget *ed = m_headerEditors.value(f.name);
        if (!ed || !m_refHeader.contains(f.name)) { continue; }
        const qint64 was = m_refHeader.value(f.name);
        const qint64 now = readEditor(ed);
        if (was != now) {
            lines << tr("%1: %2 → %3").arg(f.name).arg(was).arg(now);
        }
    }

    // Sub-packets are compared by shape first: a different set of types is a
    // more useful thing to say than a hundred field diffs against the wrong
    // sub-packet.
    if (m_subs.size() != m_refSubs.size()) {
        lines << tr("sub-packets: %1 loaded → %2 now")
                     .arg(m_refSubs.size()).arg(m_subs.size());
    } else {
        for (int i = 0; i < m_subs.size(); ++i) {
            if (m_subs[i].type != m_refSubs[i].type) {
                lines << tr("sub-packet %1: type %2 → %3")
                             .arg(i).arg(m_refSubs[i].type).arg(m_subs[i].type);
                continue;
            }
            const auto &nowV = m_subs[i].values;
            const auto &wasV = m_refSubs[i].values;
            for (auto vt = nowV.constBegin(); vt != nowV.constEnd(); ++vt) {
                if (wasV.contains(vt.key()) && wasV.value(vt.key()) != vt.value()) {
                    lines << tr("sub-packet %1 %2: %3 → %4")
                                 .arg(i).arg(vt.key())
                                 .arg(wasV.value(vt.key())).arg(vt.value());
                }
            }
        }
    }

    QMessageBox::information(this, tr("Changes since the buffer was loaded"),
                             lines.isEmpty()
                                 ? tr("Nothing differs from the loaded frame.")
                                 : lines.join(QLatin1Char('\n')));
}

void PacketMakerDialog::refreshPreview(const PacketBuilder::Result &r)
{
    const QString captype = m_packetCombo->currentText();
    const QByteArray hdr = currentHeaderPreview(captype, r.frame.size());

    QString out;
    if (!hdr.isEmpty()) {
        const QVector<MessageHeader::Extra> extras = readExtras();
        const int exLen = MessageHeader::extrasSize(extras);
        out += tr("message header (8 B, LSB): %1\n")
                   .arg(QString::fromLatin1(hdr.left(MessageHeader::SIZE).toHex(' ')));
        out += tr("  src=%1 dest=%2 msg_id=%3 msg_len=%4 seq=%5\n")
                   .arg(m_srcSpin->value()).arg(m_destSpin->value())
                   .arg(MessageHeader::messageId(captype))
                   .arg(r.frame.size() + hdr.size())
                   .arg(m_seqSpin->value());
        if (exLen > 0) {
            out += tr("extra fields (%1 B): %2\n")
                       .arg(exLen)
                       .arg(QString::fromLatin1(hdr.mid(MessageHeader::SIZE).toHex(' ')));
            out += QStringLiteral("  %1\n").arg(MessageHeader::describeExtras(extras));
            out += tr("  envelope is %1 bytes in front of the packet\n\n").arg(hdr.size());
        } else {
            // The shape note describes the plain 8-byte form; with extras the
            // operator has chosen a different shape and the note would be wrong.
            const QString note = MessageHeader::shapeNote(captype);
            out += note.isEmpty() ? QStringLiteral("\n")
                                  : QStringLiteral("  %1\n\n").arg(note);
        }
    }
    out += tr("packet: %1 bytes%2\n")
               .arg(r.frame.size())
               .arg(hdr.isEmpty() ? QString() : tr("  (datagram %1 with header)")
                                                    .arg(r.frame.size() + hdr.size()));
    out += QString::fromLatin1(r.frame.toHex(' ')) + "\n\n";
    out += tr("PKT_LENGTH = %1   CRC = %2   MAC = %3%4\n\n")
               .arg(r.pktLength).arg(r.crcHex)
               .arg(r.hasMac ? r.macHex : tr("(none)"))
               .arg(r.macKeyed ? QString() : tr(" [placeholder]"));
    out += tr("decoded back:\n");
    for (const FieldRow &fr : r.decoded) {
        out += QStringLiteral("  %1  %2\n").arg(fr.field, -28).arg(fr.value);
    }
    m_preview->setPlainText(out);
}

void PacketMakerDialog::rebuildForNewFrameNumber()
{
    // Only once the operator has built at least once. Before that, Send is
    // disabled and rebuilding on its own would be answering a question
    // nobody asked — and it would overwrite whatever they are part-way
    // through typing with a preview.
    if (m_lastFrame.isEmpty()) { return; }
    if (m_sender.isRunning())  { return; }

    const PacketBuilder::Result r = buildNow();
    if (!r.ok) {
        // The form has stopped being buildable — usually because a field was
        // edited after the last successful build. Disarm rather than leave a
        // stale frame armed behind a fresh-looking number.
        m_lastFrame.clear();
        setSendEnabled(false);
        m_status->fail(tr("Rebuild failed: %1").arg(r.error));
        return;
    }

    m_lastFrame = r.frame;
    refreshPreview(r);
    setSendEnabled(true);

    // Said quietly and transiently: this happens once a second or so, and a
    // sticky message would bury whatever the operator was actually reading.
    m_status->say(tr("Rebuilt for frame %1 — CRC %2, MAC %3")
                      .arg(readHeader().value(QStringLiteral("FRAME_NUM")))
                      .arg(r.crcHex)
                      .arg(r.hasMac ? r.macHex : tr("(none)")));
}

void PacketMakerDialog::onBuild()
{
    // Refresh the diff tint here rather than per keystroke: 171 editors is
    // too many to re-scan on every character, and Build is the point at
    // which the operator is asking what they have got.
    markChangedFields();
    applyFieldFilter();

    // Extra header fields are checked here, not only at send: Build is what
    // stands behind the Send button, so a value that cannot go out must stop
    // it here rather than surface as a refusal after the operator has armed.
    {
        QString exErr;
        readExtras(&exErr);
        if (!exErr.isEmpty() && MessageHeader::applies(m_packetCombo->currentText())) {
            m_lastFrame.clear();
            setSendEnabled(false);
            m_preview->setPlainText(QString());
            m_status->fail(tr("Build failed: %1").arg(exErr));
            return;
        }
    }

    const PacketBuilder::Result r = buildNow();
    if (!r.ok) {
        m_lastFrame.clear();
        setSendEnabled(false);
        m_preview->setPlainText(QString());
        m_status->fail(tr("Build failed: %1").arg(r.error));
        return;
    }
    m_lastFrame = r.frame;
    refreshPreview(r);

    QStringList st;
    st << tr("Built OK.");
    st << (r.roundTripped ? tr("round-trips ✓") : tr("round-trip ✗"));
    st << (r.crcVerified ? tr("CRC ✓") : tr("CRC ✗"));
    if (!r.macKeyed && r.hasMac) { st << tr("MAC is a placeholder (no key)"); }
    if (MessageHeader::applies(m_packetCombo->currentText())) {
        const int exLen = MessageHeader::extrasSize(readExtras());
        if (exLen > 0) { st << tr("+%1 B extra header fields").arg(exLen); }
    }
    for (const QString &n : r.notes) { st << n; }
    m_status->ok(st.join(QStringLiteral("  ·  ")));
    setSendEnabled(true);
}

void PacketMakerDialog::setSendEnabled(bool on)
{
    m_builtOk = on;
    refreshArmState();
}

void PacketMakerDialog::refreshArmState()
{
    const bool armed = m_armed && m_armed->isChecked();
    const bool live  = m_builtOk && armed;

    // Stop stays reachable whatever the arm switch says: disarming must
    // never be able to strand a run that is already transmitting.
    m_sendOnceBtn->setEnabled(live);
    m_intervalBtn->setEnabled(live || m_sender.isRunning());

    if (m_armed) {
        m_armed->setText(armed ? tr("ARMED — sends go out immediately")
                               : tr("Arm to enable sending"));
        m_armed->setStyleSheet(armed ? UiColor::warningStyle() : UiColor::mutedStyle());
    }
}

void PacketMakerDialog::disarm(const QString &why)
{
    if (!m_armed || !m_armed->isChecked()) { return; }
    if (m_sender.isRunning()) { return; }   // a run in flight is not disarmed under
    m_armed->setChecked(false);
    refreshArmState();
    if (!why.isEmpty()) { m_status->say(why); }
}

QString PacketMakerDialog::currentCaptype() const
{
    return m_packetCombo ? m_packetCombo->currentText() : QString();
}

QByteArray PacketMakerDialog::currentHeaderPreview(const QString &captype, int packetLen) const
{
    if (!MessageHeader::applies(captype)) { return QByteArray(); }
    // Header + enabled extras. Empty when an enabled extra is invalid, which
    // callers treat as "cannot send" — never as "send without a header".
    return MessageHeader::buildWithExtras(captype, packetLen, quint16(m_seqSpin->value()),
                                          quint8(m_srcSpin->value()),
                                          quint8(m_destSpin->value()), readExtras());
}

UdpSender::PrefixFn PacketMakerDialog::headerPrefix(const QString &captype, int packetLen)
{
    if (!MessageHeader::applies(captype)) { return {}; }
    const quint8 src = quint8(m_srcSpin->value());
    const quint8 dst = quint8(m_destSpin->value());
    // Snapshotted with src/dst: an interval run keeps the envelope it was
    // started with, and editing the table mid-run changes nothing on the wire.
    // The caller has already refused to send if these do not validate.
    const QVector<MessageHeader::Extra> extras = readExtras();
    // Regenerated per datagram: read the seq spin, build, then advance it so
    // the operator sees the sequence climb and the next send continues it.
    return [this, captype, packetLen, src, dst, extras](int) -> QByteArray {
        const quint16 s = quint16(m_seqSpin->value());
        QByteArray h = MessageHeader::buildWithExtras(captype, packetLen, s, src, dst, extras);
        m_seqSpin->setValue(int((s + 1) & 0xFFFF));
        return h;
    };
}

QVector<UdpSender::Target> PacketMakerDialog::destinations() const
{
    QVector<UdpSender::Target> out;
    for (int i = 0; i < kMaxDests; ++i) {
        if (!m_destHost[i]) { continue; }
        // The first is always on; the rest need their box ticked. A host
        // typed and left unticked is a destination the operator prepared and
        // did not choose, and sending to it anyway would be the worst kind of
        // surprise in a tool that transmits.
        if (i > 0 && (!m_destOn[i] || !m_destOn[i]->isChecked())) { continue; }
        const QString host = m_destHost[i]->text().trimmed();
        if (host.isEmpty()) { continue; }
        out.push_back(UdpSender::Target{ host, quint16(m_destPort[i]->value()) });
    }
    return out;
}

QString PacketMakerDialog::destinationSummary() const
{
    QStringList parts;
    for (const UdpSender::Target &t : destinations()) {
        parts << QStringLiteral("%1:%2").arg(t.host).arg(t.port);
    }
    return parts.isEmpty() ? tr("(no destination)")
                           : parts.join(QStringLiteral(", "));
}

void PacketMakerDialog::onSendOnce()
{
    if (m_lastFrame.isEmpty()) { return; }
    const QString captype = m_packetCombo->currentText();
    const QVector<UdpSender::Target> dests = destinations();
    if (dests.isEmpty()) {
        m_status->fail(tr("No destination — fill in an address first."));
        return;
    }

    // Rebuilt at the moment of sending, not sent from what Build left behind.
    // The frame number moves while the dialog sits there, and the MAC and CRC
    // are computed over it — so a frame built even a few seconds earlier is
    // rejected by the loco. Nothing else in the form can have changed since
    // the last build without disabling Send, so this only ever refreshes what
    // moves on its own.
    //
    // There was a confirmation dialog here. It was part of the same problem:
    // a modal between the click and the datagram is dead time during which
    // the number goes stale, and the operator had already decided by
    // pressing Send.
    const PacketBuilder::Result r = buildNow();
    if (!r.ok) {
        m_status->fail(tr("Not sent — rebuild failed: %1").arg(r.error));
        setSendEnabled(false);
        return;
    }
    m_lastFrame = r.frame;
    refreshPreview(r);

    if (MessageHeader::applies(captype)) {
        QString exErr;
        readExtras(&exErr);
        if (!exErr.isEmpty()) {
            m_status->fail(tr("Not sent — %1").arg(exErr));
            setSendEnabled(false);
            return;
        }
    }
    m_sender.sendOnce(m_lastFrame, dests, headerPrefix(captype, m_lastFrame.size()));
}

void PacketMakerDialog::onToggleInterval()
{
    if (m_sender.isRunning()) { m_sender.stop(); return; }
    if (m_lastFrame.isEmpty()) { return; }
    const QString captype = m_packetCombo->currentText();
    const QVector<UdpSender::Target> dests = destinations();
    if (dests.isEmpty()) {
        m_status->fail(tr("No destination — fill in an address first."));
        return;
    }
    if (MessageHeader::applies(captype)) {
        QString exErr;
        readExtras(&exErr);
        if (!exErr.isEmpty()) {
            m_status->fail(tr("Not started — %1").arg(exErr));
            setSendEnabled(false);
            return;
        }
    }
    // No confirmation here either. Send Once lost its because the modal was
    // dead time in which the frame number went stale; this one loses its for
    // consistency — two send buttons that ask differently is a worse trap
    // than either answer. What replaces it is the status line below, which
    // states plainly what is going out and where, and the button that now
    // reads "Stop".
    m_status->warn(tr("Sending %1 to %2 every %3 ms.%4")
                       .arg(captype, destinationSummary())
                       .arg(m_intervalSpin->value())
                       .arg(varySummary().isEmpty() ? QString()
                                                    : QStringLiteral("  ") + varySummary()));
    // Rebuild per tick rather than repeating m_lastFrame. Without this the
    // body — and therefore FRAME_NUM, the MAC and the CRC — is identical on
    // every datagram, and a peer using frame number for freshness sees a
    // stream that never moves.
    m_sender.startInterval(
        [this, captype](int sendIndex) -> QByteArray {
            const PacketBuilder::Result r = buildNow(sendIndex);
            if (!r.ok) {
                m_status->fail(tr("Interval stopped: build failed at send %1 — %2")
                                      .arg(sendIndex).arg(r.error));
                return QByteArray();
            }
            m_lastFrame = r.frame;
            return r.frame;
        },
        dests, m_intervalSpin->value(), headerPrefix(captype, m_lastFrame.size()));
}

// ---- presets ---------------------------------------------------------------

QJsonObject PacketMakerDialog::toPreset() const
{
    PacketPreset p;
    p.captype    = m_packetCombo->currentText();
    p.dest       = m_destEdit->text();
    p.port       = m_portSpin->value();
    p.intervalMs = m_intervalSpin->value();
    p.msgSrc     = m_srcSpin->value();
    p.msgDest    = m_destSpin->value();
    p.msgSeq     = m_seqSpin->value();
    p.extras     = readExtras();

    for (auto it = m_headerEditors.constBegin(); it != m_headerEditors.constEnd(); ++it) {
        if (it.key() == QLatin1String("PKT_LENGTH")) { continue; }   // recomputed
        if (it.key() == QLatin1String("MAC_CODE"))   { continue; }   // recomputed
        p.header.insert(it.key(), readEditor(it.value()));
    }
    p.subs = m_subs;
    p.vary = readVaryRules();
    return p.toJson();
}

bool PacketMakerDialog::applyPreset(const QJsonObject &o, QString *err)
{
    PacketPreset p;
    if (!p.fromJson(o, err)) { return false; }

    const int idx = m_packetCombo->findText(p.captype);
    if (idx < 0) {
        if (err) { *err = tr("preset is for packet '%1', which this schema does not define")
                              .arg(p.captype); }
        return false;
    }

    // Changing the combo resets the form, the sub-packet list and the vary
    // table, so everything below lands on a clean slate rather than being
    // merged onto whatever happened to be there.
    m_packetCombo->setCurrentIndex(idx);

    m_destEdit->setText(p.dest);
    if (p.port > 0)       { m_portSpin->setValue(p.port); }
    if (p.intervalMs > 0) { m_intervalSpin->setValue(p.intervalMs); }
    m_srcSpin->setValue(p.msgSrc);
    m_destSpin->setValue(p.msgDest);
    m_seqSpin->setValue(p.msgSeq);
    // A preset from before extras existed carries none; it gets the default
    // rows, unticked, so it sends exactly what it always did.
    setExtras(p.extras.isEmpty() ? MessageHeader::defaultExtras() : p.extras);

    QStringList unknown;
    for (auto it = p.header.constBegin(); it != p.header.constEnd(); ++it) {
        QWidget *ed = m_headerEditors.value(it.key(), nullptr);
        if (ed) { writeEditor(ed, it.value()); }
        else    { unknown << it.key(); }
    }

    if (m_subWindow) { m_subWindow->clearTarget(); }
    m_curSub = -1;
    m_subs   = p.subs;
    m_subList->clear();
    const Schema::PacketInfo pi = m_builder.encoder().packet(p.captype);
    for (const Schema::SubEntry &se : m_subs) {
        QString sname;
        for (const Schema::CaseInfo &c : pi.cases) {
            if (c.type == se.type) { sname = c.structName; break; }
        }
        m_subList->addItem(tr("type %1 — %2").arg(se.type).arg(sname));
    }
    if (!m_subs.isEmpty()) { m_subList->setCurrentRow(0); }
    m_curSub = m_subList->currentRow();
    rebuildSubEditor();

    setVaryRules(p.vary);

    m_lastFrame.clear();
    setSendEnabled(false);

    // Unknown fields are reported rather than dropped in silence: a preset
    // written against an older schema that quietly lost half its values would
    // look like it loaded fine and then send something else.
    if (!unknown.isEmpty() && err) {
        *err = tr("loaded, but %1 field(s) are not in this schema: %2")
                   .arg(unknown.size()).arg(unknown.join(QStringLiteral(", ")));
    }
    return true;
}

void PacketMakerDialog::onSavePreset()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save packet preset"),
        m_packetCombo->currentText() + QStringLiteral(".packet.json"),
        tr("Packet preset (*.packet.json *.json);;All files (*)"));
    if (path.isEmpty()) { return; }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1").arg(path));
        return;
    }
    f.write(QJsonDocument(toPreset()).toJson(QJsonDocument::Indented));
    f.close();
    m_status->ok(tr("Saved preset to %1 (the session key is not stored).").arg(path));
}

void PacketMakerDialog::onLoadPreset()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load packet preset"), QString(),
        tr("Packet preset (*.packet.json *.json);;All files (*)"));
    if (path.isEmpty()) { return; }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read %1").arg(path));
        return;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("%1 is not valid JSON: %2").arg(path, pe.errorString()));
        return;
    }

    QString err;
    if (!applyPreset(doc.object(), &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    disarm(QString());     // a preset replaces the frame; the message below says so
    if (err.isEmpty()) {
        m_status->ok(tr("Loaded %1. Build && Verify before sending.").arg(path));
    } else {
        m_status->warn(err);
    }
}

// ---- fill from buffer ------------------------------------------------------

void PacketMakerDialog::loadBuffer(const QString &text)
{
    if (!m_bufEdit) { return; }
    m_bufEdit->setPlainText(text.trimmed());
    onLoadBuffer();
}

PacketMakerDialog::BufferSplit
PacketMakerDialog::splitBuffer(const Schema::Encoder &enc, const QString &captype,
                               const QByteArray &frame)
{
    BufferSplit out;
    out.body = frame;

    const Schema::PacketInfo pi = enc.packet(captype);
    // body_offset is measured from the start of the CAPTURED datagram, so for
    // arp/lsrp it already accounts for the message header and the two bytes
    // after it. Where it is zero the capture carries no envelope at all
    // (a captured @slrp line is the body).
    const int bodyStart = (pi.ok && pi.bodyOffsetBits > 0) ? pi.bodyOffsetBits / 8 : 0;

    // Is there really a message header on the front? Right message_id for this
    // captype and a message_length that accounts for the whole buffer. Guessing
    // from length alone would strip eight bytes off a bare body.
    bool header = false;
    if (MessageHeader::applies(captype) && frame.size() > MessageHeader::SIZE) {
        const uchar *p = reinterpret_cast<const uchar *>(frame.constData());
        const int msgId  = int(p[2]) | (int(p[3]) << 8);
        const int msgLen = int(p[4]) | (int(p[5]) << 8);
        if (msgId == MessageHeader::messageId(captype) && msgLen == frame.size()) {
            header       = true;
            out.hadHeader = true;
            out.srcId    = int(p[0]);
            out.destId   = int(p[1]);
            out.seq      = int(p[6]) | (int(p[7]) << 8);
        }
    }

    int strip = 0;
    if (bodyStart > 0 && header) {
        strip = bodyStart;
        if (bodyStart < MessageHeader::SIZE) {
            // The schema puts the body inside the message header. Follow the
            // schema — the decoder does — but say so, because one of the two
            // is wrong and the operator is the only one who can tell which.
            out.notes << QObject::tr("schema body_offset (%1 B) is inside the "
                                     "8-byte message header — using the schema")
                             .arg(bodyStart);
        } else if (bodyStart == MessageHeader::SIZE) {
            out.notes << QObject::tr("stripped the 8-byte message header "
                                     "(src=%1 dest=%2 seq=%3)")
                             .arg(out.srcId).arg(out.destId).arg(out.seq);
        } else {
            out.notes << QObject::tr("stripped the %1-byte capture envelope: "
                                     "8-byte message header (src=%2 dest=%3 seq=%4) "
                                     "+ %5 B before the body")
                             .arg(bodyStart).arg(out.srcId).arg(out.destId)
                             .arg(out.seq).arg(bodyStart - MessageHeader::SIZE);
        }
    } else if (header) {
        strip = MessageHeader::SIZE;
        out.notes << QObject::tr("stripped the 8-byte message header "
                                 "(src=%1 dest=%2 seq=%3)")
                         .arg(out.srcId).arg(out.destId).arg(out.seq);
    } else if (bodyStart > 0) {
        // A captured frame of this type would have an envelope and this one
        // does not look like it has one. Read it as a bare body, but do not
        // do that quietly: if the paste was truncated, every field below is
        // going to be wrong by the same amount and nothing else will say so.
        out.notes << QObject::tr("this packet carries a %1-byte envelope when "
                                 "captured; this buffer does not look like one, "
                                 "so it is being read as a bare body")
                         .arg(bodyStart);
    }

    if (strip > 0 && strip < frame.size()) {
        out.body     = frame.mid(strip);
        out.stripped = strip;
    } else if (strip >= frame.size()) {
        out.notes << QObject::tr("buffer is %1 B, shorter than the %2 B that come "
                                 "before the body — nothing to read")
                         .arg(frame.size()).arg(strip);
        out.body.clear();
        out.stripped = 0;
    }
    return out;
}

void PacketMakerDialog::onLoadBuffer()
{
    if (!m_builder.ready()) { return; }

    QString hint;
    QByteArray frame = parseBuffer(m_bufEdit->toPlainText(), &hint);
    if (frame.isEmpty()) {
        m_bufStatus->setText(tr("No bytes found in that text."));
        m_bufStatus->setStyleSheet(UiColor::errorStyle());
        return;
    }

    const Schema::Encoder &enc = m_builder.encoder();
    QStringList pre;

    // Which packet? The @tag wins, then a shape-based guess, then whatever the
    // combo already shows — in that order, because the tag is the only one of
    // the three the operator actually stated.
    QString captype;
    if (!hint.isEmpty() && m_packetCombo->findText(hint) >= 0) {
        captype = hint;
    } else {
        const QString guess = enc.detectCaptype(frame);
        if (!guess.isEmpty() && m_packetCombo->findText(guess) >= 0) {
            captype = guess;
            pre << tr("no @tag — read as '%1' from the byte layout").arg(guess);
        } else {
            captype = m_packetCombo->currentText();
            pre << tr("could not identify the packet — read as '%1'").arg(captype);
        }
    }

    // Find the body. Everything before it — message header, and for arp/lsrp
    // two more bytes the schema accounts for in body_offset — has to come off,
    // or the whole form is filled from bits read at the wrong offset.
    const BufferSplit split = splitBuffer(enc, captype, frame);
    pre += split.notes;
    if (split.hadHeader) {
        m_srcSpin->setValue(split.srcId);
        m_destSpin->setValue(split.destId);
        m_seqSpin->setValue(split.seq);
    }
    frame = split.body;
    if (frame.isEmpty()) {
        m_bufStatus->setText(pre.join(QStringLiteral("  ·  ")));
        m_bufStatus->setStyleSheet(UiColor::errorStyle());
        return;
    }

    const Schema::ParsedPacket pp = enc.parseBody(captype, frame);
    if (!pp.ok) {
        m_bufStatus->setText(tr("%1 B parsed as %2 — failed: %3")
                                 .arg(frame.size()).arg(captype, pp.error));
        m_bufStatus->setStyleSheet(UiColor::errorStyle());
        return;
    }

    // Switching the combo resets the form (onPacketChanged), so do that first
    // and only then write the parsed values in.
    if (m_packetCombo->currentText() != captype) {
        m_packetCombo->setCurrentIndex(m_packetCombo->findText(captype));
    } else {
        onPacketChanged();
    }
    applyParsed(pp);

    QStringList st = pre;
    st << tr("%1: %2 header field(s), %3 sub-packet(s), %4 B body + %5 B tail")
              .arg(captype).arg(pp.header.size()).arg(pp.subs.size())
              .arg(pp.bodyBytes).arg(pp.tailBytes);
    st += pp.notes;
    m_bufStatus->setText(st.join(QStringLiteral("  ·  ")));
    m_bufStatus->setStyleSheet(pp.notes.isEmpty() ? QString()
                                                  : UiColor::warningStyle());

    disarm(tr("Disarmed: the form was replaced from a buffer."));
    m_status->say(tr("Fields filled from a %1-byte buffer. Edit anything, "
                         "then Build && Verify — PKT_LENGTH, the MAC and the CRC "
                         "are all recomputed.").arg(frame.size()));
}

// Push a parsed frame into the live form. Header editors are written by name;
// PKT_LENGTH and MAC_CODE are skipped because PacketBuilder owns them and their
// editors are disabled. Sub-packets replace the list wholesale.
void PacketMakerDialog::applyParsed(const Schema::ParsedPacket &pp)
{
    for (auto it = pp.header.constBegin(); it != pp.header.constEnd(); ++it) {
        if (it.key() == QLatin1String("PKT_LENGTH")) { continue; }
        if (it.key() == QLatin1String("MAC_CODE"))   { continue; }
        QWidget *ed = m_headerEditors.value(it.key(), nullptr);
        if (ed) { writeEditor(ed, it.value()); }
    }

    // Release the editor's target before the vector is replaced: it writes
    // through a pointer and an index, and committing at the old index into
    // the new sub-packets would corrupt whichever one now sits there.
    if (m_subWindow) { m_subWindow->clearTarget(); }
    m_curSub = -1;                       // nothing to commit into
    m_subs = pp.subs;
    m_subList->clear();

    const Schema::PacketInfo pi = m_builder.encoder().packet(pp.captype);
    for (const Schema::SubEntry &se : m_subs) {
        QString sname;
        for (const Schema::CaseInfo &c : pi.cases) {
            if (c.type == se.type) { sname = c.structName; break; }
        }
        m_subList->addItem(tr("type %1 — %2").arg(se.type).arg(sname));
    }
    if (!m_subs.isEmpty()) { m_subList->setCurrentRow(0); }
    m_curSub = m_subList->currentRow();
    rebuildSubEditor();

    // Keep what was loaded, so the form can be diffed against it as it is
    // edited. This is the pair to Fill from buffer: loading a captured frame
    // and then asking "what have I changed" is the loop for working out what
    // an unknown field does.
    m_refHeader = pp.header;
    m_refSubs   = pp.subs;
    m_haveRef   = true;
    if (m_onlyChanged) { m_onlyChanged->setEnabled(true); }
    if (m_diffBtn)     { m_diffBtn->setEnabled(true); }
    markChangedFields();

    m_lastFrame.clear();                 // nothing verified yet — Build first
    setSendEnabled(false);
}

void PacketMakerDialog::onClearBuffer()
{
    m_bufEdit->clear();
    m_bufStatus->setText(QString());
    m_bufStatus->setStyleSheet(QString());
}

void PacketMakerDialog::onSenderError(const QString &what)
{
    m_status->fail(tr("Send error: %1").arg(what));
}

QString PacketMakerDialog::varySummary() const
{
    QStringList parts;
    for (const PacketVary::Rule &r : readVaryRules()) {
        if (!r.enabled || r.field.isEmpty()) { continue; }
        parts << r.describe(headerFieldBits(r.field));
    }
    if (parts.isEmpty()) {
        return tr("\n\nNo fields vary — every datagram will carry identical "
                  "packet bytes.");
    }
    return tr("\n\nVarying per send:\n  %1").arg(parts.join(QStringLiteral("\n  ")));
}

void PacketMakerDialog::onPacketChanged()
{
    m_sender.stop();
    // A different packet is a different decision.
    disarm(tr("Disarmed: the packet type changed."));
    m_subs.clear();
    m_curSub = -1;
    m_subList->clear();
    rebuildSubEditor();
    rebuildHeaderForm();
    rebuildVaryTable();
    m_lastFrame.clear();
    setSendEnabled(false);

    // The reference frame belongs to the packet it was parsed as; keeping it
    // across a type change would diff against a different layout entirely.
    m_haveRef = false;
    m_refHeader.clear();
    m_refSubs.clear();
    if (m_onlyChanged) { m_onlyChanged->setChecked(false); m_onlyChanged->setEnabled(false); }
    if (m_diffBtn)     { m_diffBtn->setEnabled(false); }
    if (m_fieldFilter) { m_fieldFilter->clear(); }
    if (m_bufStatus) {
        m_bufStatus->setText(QString());
        m_bufStatus->setStyleSheet(QString());
    }

    const QString captype = m_packetCombo->currentText();
    const bool hasHdr = MessageHeader::applies(captype);
    m_hdrBox->setEnabled(hasHdr);
    if (m_extraBox) { m_extraBox->setEnabled(hasHdr); refreshExtrasTitle(); }
    m_msgIdLabel->setText(hasHdr
        ? tr("message_id: %1").arg(MessageHeader::messageId(captype))
        : tr("message_id: — (no header for this type)"));

    m_status->say(tr("Edit fields, then Build && Verify."));
}

// =============================================================================
//  Extra header fields
// =============================================================================
namespace {
enum ExtraCol { XC_SEND = 0, XC_NAME, XC_TYPE, XC_VALUE, XC_ORDER };
}

void PacketMakerDialog::appendExtraRow(const MessageHeader::Extra &e)
{
    const bool was = m_writingExtras;
    m_writingExtras = true;

    const int row = m_extraTable->rowCount();
    m_extraTable->insertRow(row);

    // A real QCheckBox rather than a checkable item: in the dark theme an
    // unchecked item indicator draws as nothing at all, and a tick box you
    // cannot see is one you cannot tell is off.
    auto *onHost = new QWidget(m_extraTable);
    auto *onLay  = new QHBoxLayout(onHost);
    onLay->setContentsMargins(0, 0, 0, 0);
    onLay->setAlignment(Qt::AlignCenter);
    auto *on = new QCheckBox(onHost);
    on->setObjectName(QStringLiteral("extraSend"));
    on->setChecked(e.enabled);
    onLay->addWidget(on);
    m_extraTable->setCellWidget(row, XC_SEND, onHost);
    connect(on, &QCheckBox::toggled, this, [this](bool) { onExtrasEdited(); });

    m_extraTable->setItem(row, XC_NAME, new QTableWidgetItem(e.name));

    auto *type = new QComboBox(m_extraTable);
    type->addItem(QStringLiteral("uint8_t"),  1);
    type->addItem(QStringLiteral("uint16_t"), 2);
    type->addItem(QStringLiteral("uint32_t"), 4);
    type->setCurrentIndex(qMax(0, type->findData(e.bytes)));
    m_extraTable->setCellWidget(row, XC_TYPE, type);
    connect(type, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onExtrasEdited(); });

    m_extraTable->setItem(row, XC_VALUE, new QTableWidgetItem(QString::number(e.value)));

    auto *order = new QComboBox(m_extraTable);
    order->addItem(tr("little-endian"), false);
    order->addItem(tr("big-endian"),    true);
    order->setCurrentIndex(e.bigEndian ? 1 : 0);
    m_extraTable->setCellWidget(row, XC_ORDER, order);
    m_extraTable->setColumnWidth(XC_TYPE,
        qMax(m_extraTable->columnWidth(XC_TYPE), type->sizeHint().width() + 28));
    m_extraTable->setColumnWidth(XC_ORDER,
        qMax(m_extraTable->columnWidth(XC_ORDER), order->sizeHint().width() + 28));
    m_extraTable->setColumnWidth(XC_SEND,
        qMax(m_extraTable->columnWidth(XC_SEND),
             m_extraTable->fontMetrics().horizontalAdvance(tr("Send")) + 24));
    connect(order, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onExtrasEdited(); });

    m_writingExtras = was;
}

void PacketMakerDialog::setExtras(const QVector<MessageHeader::Extra> &extras)
{
    if (!m_extraTable) { return; }
    m_writingExtras = true;
    m_extraTable->setRowCount(0);
    for (const MessageHeader::Extra &e : extras) { appendExtraRow(e); }
    m_writingExtras = false;
    refreshExtrasTitle();
}

void PacketMakerDialog::fitExtrasTable()
{
    if (!m_extraTable) { return; }
    // Tall enough for every row up to four, then it scrolls: two rows cut in
    // half is worse than a box one row taller.
    const int rows = qBound(1, m_extraTable->rowCount(), 4);
    const int h = m_extraTable->horizontalHeader()->sizeHint().height()
                + rows * m_extraTable->verticalHeader()->defaultSectionSize()
                + 2 * m_extraTable->frameWidth() + 2;
    m_extraTable->setFixedHeight(h);
}

QVector<MessageHeader::Extra> PacketMakerDialog::readExtras(QString *err) const
{
    QVector<MessageHeader::Extra> out;
    if (err) { err->clear(); }
    if (!m_extraTable) { return out; }

    for (int row = 0; row < m_extraTable->rowCount(); ++row) {
        MessageHeader::Extra e;
        const QWidget *onHost = m_extraTable->cellWidget(row, XC_SEND);
        const QCheckBox *on = onHost ? onHost->findChild<QCheckBox *>() : nullptr;
        const QTableWidgetItem *nm = m_extraTable->item(row, XC_NAME);
        const QTableWidgetItem *vl = m_extraTable->item(row, XC_VALUE);
        auto *type  = qobject_cast<QComboBox *>(m_extraTable->cellWidget(row, XC_TYPE));
        auto *order = qobject_cast<QComboBox *>(m_extraTable->cellWidget(row, XC_ORDER));

        e.enabled   = on && on->isChecked();
        e.name      = nm ? nm->text().trimmed() : QString();
        e.bytes     = type ? type->currentData().toInt() : 2;
        e.bigEndian = order && order->currentData().toBool();

        QString perr;
        qint64 v = 0;
        if (MessageHeader::parseExtraValue(vl ? vl->text() : QString(), e.bytes, &v, &perr)) {
            e.value = v;
        } else if (e.enabled) {
            // Report the first enabled row that is wrong, and mark the row as
            // invalid (-1) so nothing downstream can encode it by accident.
            e.value = -1;
            if (err && err->isEmpty()) {
                *err = tr("extra field '%1': %2")
                           .arg(e.name.isEmpty() ? tr("row %1").arg(row + 1) : e.name, perr);
            }
        }
        out << e;
    }
    return out;
}

void PacketMakerDialog::refreshExtrasTitle()
{
    if (!m_extraBox) { return; }
    fitExtrasTable();
    QString err;
    const QVector<MessageHeader::Extra> ex = readExtras(&err);
    const int n = MessageHeader::extrasSize(ex);
    QString title = tr("Extra header fields (after header, before packet)");
    if (!err.isEmpty()) {
        title += tr(" — INVALID: %1").arg(err);
    } else if (n > 0) {
        title += tr(" — +%1 B: %2").arg(n).arg(MessageHeader::describeExtras(ex));
    } else {
        title += tr(" — none");
    }
    m_extraBox->setTitle(title);
}

void PacketMakerDialog::onExtrasEdited()
{
    if (m_writingExtras) { return; }
    refreshExtrasTitle();
    // The envelope in front of the verified frame just changed, so what the
    // preview shows is no longer what Send would put on the wire. Same rule as
    // any other edit: Build again before Send.
    if (m_builtOk && !m_sender.isRunning()) {
        setSendEnabled(false);
        m_status->warn(tr("Extra header fields changed — Build && Verify again to send."));
    }
}

void PacketMakerDialog::onExtraAdd()
{
    MessageHeader::Extra e;
    e.name  = tr("field_%1").arg(m_extraTable->rowCount() + 1);
    e.bytes = 2;
    appendExtraRow(e);
    m_extraTable->setCurrentCell(m_extraTable->rowCount() - 1, XC_NAME);
    onExtrasEdited();
}

void PacketMakerDialog::onExtraRemove()
{
    const int row = m_extraTable->currentRow();
    if (row < 0) { return; }
    m_extraTable->removeRow(row);
    onExtrasEdited();
}

// =============================================================================
//  Escape and the close box while sending. See sendguard.h for why.
// =============================================================================
void PacketMakerDialog::reject()
{
    if (SendGuard::interceptReject(isSending(), [this] {
            m_sender.stop();
            m_status->warn(tr("Sending stopped. Press Escape again to close."));
        })) {
        return;
    }
    QDialog::reject();
}

void PacketMakerDialog::closeEvent(QCloseEvent *event)
{
    if (isSending() && !SendGuard::confirmClose(this, tr("A repeating send"))) {
        event->ignore();
        return;
    }
    m_sender.stop();
    QDialog::closeEvent(event);
}
