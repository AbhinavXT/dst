#include "engunits.h"
#include "packetmakerdialog.h"
#include "framenumberwatch.h"
#include "frameclock.h"
#include "rejectrules.h"
#include "capturedecoder.h"
#include "windowgeometry.h"
#include "statusline.h"
#include <QCloseEvent>
#include "sendguard.h"
#include "uicolors.h"
#include "uistyle.h"

#include "messageheader.h"
#include "packetpreset.h"
#include "stationkavachpanel.h"
#include "stationlayoutwindow.h"
#include "sessionkeystore.h"
#include "stationanswer.h"
#include <QRandomGenerator>
#include "subpacketwindow.h"

#include <QPalette>
#include "packetvariation.h"

#include <QComboBox>
#include <QToolTip>
#include <QLineEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
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
#include <QSet>
#include <cstring>
#include <QTimer>
#include <QFile>
#include <QTextBlock>

#include <algorithm>

// ---- small helpers ---------------------------------------------------------

namespace {

// Parse a decimal or 0x-hex integer, allowing a leading '-' for signed fields.
// Session 342: a value typed with its unit becomes the raw value when the
// editor is left ("45 km/h" -> 9 in a speed6 field, "1200 m" -> 1200). One
// that is not a value the field can carry is refused: the box goes back to
// its last value and says why. A bare number stays the raw value.
void watchUnits(QLineEdit *edit, QWidget *editor, const QVector<EngUnits::Range> &ranges, const QString &unit,
                const QString &plainTip)
{
    edit->setProperty("pmLastGood", edit->text());
    QObject::connect(edit, &QLineEdit::editingFinished, editor, [edit, editor, ranges, unit, plainTip]() {
        const QString typed = edit->text();
        QString why;
        qint64 raw = 0;
        bool ok = true;
        if (EngUnits::hasUnit(typed)) {
            ok = !ranges.isEmpty() ? EngUnits::toRaw(ranges, typed, &raw, &why)
                                   : EngUnits::plainToRaw(unit, typed, &raw, &why);
            if (ok) PacketMakerDialog::writeFieldEditor(editor, raw);
        }
        if (!ok) {
            edit->setText(edit->property("pmLastGood").toString());
            editor->setToolTip(QObject::tr("Not taken: %1.").arg(why) + QStringLiteral("\n\n") + plainTip);
            QToolTip::showText(editor->mapToGlobal(QPoint(0, editor->height())), why, editor);
            return;
        }
        editor->setToolTip(plainTip);
        edit->setProperty("pmLastGood", edit->text());
    });
}

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
    // Session 244: typed fields. The value behind the editor is the raw bit
    // pattern (float/double) or the packed bytes (char); the box shows a
    // number or text, and readEditor/writeEditor convert.
    if (f.type == QLatin1String("float") || f.type == QLatin1String("double")) {
        auto *le = new QLineEdit(QStringLiteral("0"), parent);
        le->setProperty("pmType", f.type);
        le->setToolTip(tr("IEEE-754 %1 — a decimal number").arg(f.type));
        return le;
    }
    if (f.type == QLatin1String("char")) {
        auto *le = new QLineEdit(parent);
        le->setProperty("pmType", f.type);
        le->setProperty("pmBytes", f.bits / 8);
        le->setMaxLength(f.bits / 8);
        le->setToolTip(tr("Up to %n ASCII character(s); the rest is zero", nullptr, f.bits / 8));
        return le;
    }
    if (!f.enumName.isEmpty()) {
        const auto choices = enc.enumChoices(f.enumName);
        if (!choices.isEmpty()) {
            auto *cb = new QComboBox(parent);
            cb->setEditable(true);                 // still allow a raw number
            // Long labels stay in the popup; the box keeps a field's size.
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            cb->setMinimumContentsLength(20);
            for (const auto &c : choices) {
                // kavach.xml labels mostly lead with the value ("1 (Stand_By)").
                const QString num = QString::number(c.first);
                cb->addItem(c.second.startsWith(num) ? c.second
                                                     : QStringLiteral("%1 — %2").arg(num, c.second),
                            QVariant(qlonglong(c.first)));
            }
            // Start at 0 like a plain field does, named if 0 is declared.
            const int zero = cb->findData(QVariant(qlonglong(0)));
            if (zero >= 0) { cb->setCurrentIndex(zero); } else { cb->setEditText(QStringLiteral("0")); }
            // Session 342: a coded speed takes "45 km/h" too.
            const QVector<EngUnits::Range> ranges = enc.enumRanges(f.enumName);
            if (!ranges.isEmpty()) {
                QStringList spans;
                for (const auto &r : ranges)
                    spans << EngUnits::text({ r }, r.from) + QStringLiteral("–") + EngUnits::text({ r }, r.to);
                const QString tip = tr("A code. Type a number for the code itself, or a value with its unit "
                                       "for the code that means it (%1).").arg(spans.join(QStringLiteral(", ")));
                cb->setToolTip(tip);
                watchUnits(cb->lineEdit(), cb, ranges, QString(), tip);
            }
            return cb;
        }
    }
    auto *le = new QLineEdit(parent);
    le->setText(QStringLiteral("0"));
    QString tip = QStringLiteral("%1 bits%2 — decimal or 0x-hex")
                      .arg(f.bits).arg(f.isSigned ? QStringLiteral(", signed") : QString());
    if (!f.unit.isEmpty()) {
        tip = tr("In %1. ").arg(f.unit) + tip + tr("; \"1200 %1\" is taken too").arg(f.unit);   // session 342
        le->setPlaceholderText(f.unit);
    }
    le->setToolTip(tip);
    watchUnits(le, le, {}, f.unit, tip);
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

QVector<PacketMakerDialog::ReceiverLine> PacketMakerDialog::receiverCheck(
    const QString &captype, const QHash<QString, qint64> &raw,
    const QVector<Schema::SubEntry> &subs, bool macPlaceholder,
    const FrameNumberWatch::Seen &live, qint64 nowMs, int localSecs)
{
    QVector<ReceiverLine> out;
    const auto fnIt = raw.constFind(QStringLiteral("FRAME_NUM"));
    if (fnIt != raw.constEnd()) {
        const qint64 fn = fnIt.value();
        const qint64 liveAge = live.valid() ? nowMs - live.atMs : -1;
        qint64 equipNow; QString against;
        if (liveAge >= 0 && liveAge <= 10000) {
            equipNow = live.value + liveAge / 1000;
            against = tr("the loco's frame number (%1, seen %2 s ago)")
                          .arg(live.value).arg(liveAge / 1000);
        } else {
            equipNow = FrameClock::frameNumFor(localSecs);
            against = tr("this laptop's clock (no live frame number in the last 10 s)");
        }
        qint64 age = equipNow - fn;
        // Midnight: take the shorter way round, as FrameClock::skewSeconds does.
        if (age >  FrameClock::kSecondsPerDay / 2) { age -= FrameClock::kSecondsPerDay; }
        if (age < -FrameClock::kSecondsPerDay / 2) { age += FrameClock::kSecondsPerDay; }
        const FrameClock::Accept a = FrameClock::classify(int(age));   // within ±12 h after the wrap
        ReceiverLine l;
        l.notProcessed = a == FrameClock::Accept::Stale || a == FrameClock::Accept::Ahead;
        if (a == FrameClock::Accept::Stale) {
            l.text = tr("FRAME_NUM %1 is %2 s old against %3 — would not be processed "
                        "(more than 4 s old)").arg(fn).arg(age).arg(against);
        } else if (a == FrameClock::Accept::Ahead) {
            l.text = tr("FRAME_NUM %1 is %2 s early against %3 — would not be processed "
                        "(2 s or more early)").arg(fn).arg(-age).arg(against);
        } else {
            l.text = tr("FRAME_NUM %1, age %2 s against %3 — %4")
                         .arg(fn).arg(age).arg(against, FrameClock::acceptText(a));
        }
        out << l;
    }
    if (macPlaceholder) {
        out << ReceiverLine{ true, tr("MAC is a zero placeholder (no session key) — a receiver "
                                      "checking the MAC would not process it") };
    }
    QStringList headerFindings;
    for (const RejectRules::Finding &f : kavachRejectRules().evaluate(raw, captype)) {
        headerFindings << f.text;
        out << ReceiverLine{ true, tr("would not be processed: %1").arg(f.text) };
    }
    for (int i = 0; i < subs.size(); ++i) {
        QHash<QString, qint64> scope = raw;
        for (auto it = subs[i].values.cbegin(); it != subs[i].values.cend(); ++it) {
            scope.insert(it.key(), it.value());
        }
        for (const RejectRules::Finding &f : kavachRejectRules().evaluate(scope, captype)) {
            if (headerFindings.contains(f.text)) { continue; }
            out << ReceiverLine{ true, tr("would not be processed: sub-packet %1 (type %2): %3")
                                           .arg(i + 1).arg(subs[i].type).arg(f.text) };
        }
    }
    return out;
}

QVector<PacketMakerDialog::ReceiverLine>
PacketMakerDialog::receiverLinesFor(const PacketBuilder::Result &r) const
{
    return receiverCheck(m_packetCombo->currentText(), r.raw, r.subs, r.hasMac && !r.macKeyed,
                         m_frameWatch ? m_frameWatch->latest() : FrameNumberWatch::Seen(),
                         QDateTime::currentMSecsSinceEpoch(),
                         QTime::currentTime().msecsSinceStartOfDay() / 1000);
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
        const QString type = le->property("pmType").toString();
        if (type == QLatin1String("float")) {
            const float f = float(le->text().trimmed().toDouble());
            quint32 raw = 0; std::memcpy(&raw, &f, sizeof raw);
            return qint64(raw);
        }
        if (type == QLatin1String("double")) {
            const double d = le->text().trimmed().toDouble();
            quint64 raw = 0; std::memcpy(&raw, &d, sizeof raw);
            return qint64(raw);
        }
        if (type == QLatin1String("char")) {
            const QByteArray b = le->text().toLatin1().left(le->property("pmBytes").toInt());
            quint64 u = 0;
            for (int k = 0; k < b.size(); ++k) { u |= quint64(uchar(b[k])) << (8 * k); }
            return qint64(u);
        }
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
    if (auto *le = qobject_cast<QLineEdit *>(w)) {
        const QString type = le->property("pmType").toString();
        if (type == QLatin1String("float")) {
            const quint32 raw = quint32(v); float f; std::memcpy(&f, &raw, sizeof f);
            le->setText(QString::number(double(f), 'g', 9));
        } else if (type == QLatin1String("double")) {
            const quint64 raw = quint64(v); double d; std::memcpy(&d, &raw, sizeof d);
            le->setText(QString::number(d, 'g', 17));
        } else if (type == QLatin1String("char")) {
            QByteArray b;
            for (int k = 0; k < le->property("pmBytes").toInt(); ++k) {
                const char c = char((quint64(v) >> (8 * k)) & 0xFF);
                if (c == '\0') { break; }
                b.append(c);
            }
            le->setText(QString::fromLatin1(b));
        } else {
            le->setText(QString::number(v));
        }
    }
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
    // Defaults: loco 1 here, loco 2 as Destination 2 (VCC 1 controller 1, session 300).
    m_destEdit = new QLineEdit(QStringLiteral("192.168.25.16"), this);
    m_destEdit->setObjectName(QStringLiteral("pmDestHost"));
    m_destEdit->setToolTip(tr("Loco 1 · VCC 1 controller 1 by default"));
    m_destEdit->setMaximumWidth(140);
    top->addWidget(m_destEdit);
    top->addWidget(new QLabel(tr("Port:"), this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(50001);
    m_portSpin->setObjectName(QStringLiteral("pmDestPort"));
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
        m_destPort[i]->setValue(50001);
        if (i == 1) {
            m_destHost[i]->setText(QStringLiteral("192.168.25.77"));
            m_destHost[i]->setToolTip(tr("Loco 2 · VCC 1 controller 1 by default"));
        }
        destGrid->addWidget(m_destOn[i],   i - 1, 0);
        destGrid->addWidget(m_destHost[i], i - 1, 1);
        destGrid->addWidget(new QLabel(tr("Port:"), destBody), i - 1, 2);
        destGrid->addWidget(m_destPort[i], i - 1, 3);
    }
    destGrid->setColumnStretch(4, 1);
    destOuter->addWidget(destBody);
    UiStyle::makeFoldable(destBox, destBody);  // session 125
    root->addWidget(destBox);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Session key:"), this));
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setPlaceholderText(tr("32 hex digits, optional; blank = zero MAC placeholder"));
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
    UiStyle::makeFoldable(m_extraBox, exBody); // session 125
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
    UiStyle::makeFoldable(bufBox, bufBody);    // session 125: off = one line, no empty frame
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

    // The header form gets the larger share (session 128). The sub-packet
    // side here is only a list: its fields and repeat tables are edited in
    // their own window (Edit fields...). At 400 px the form squeezed
    // FRAME_NUM's editor to two digits behind a sideways scrollbar.
    m_mid = mid;
    mid->setStretchFactor(0, 5);
    mid->setStretchFactor(1, 3);
    mid->setSizes({ 600, 460 });

    // The middle and the preview share the remaining height through a
    // splitter rather than the preview taking a fixed slice. On a packet
    // with 171 header fields the preview does not need 200 px and the form
    // does.
    auto *vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->setChildrenCollapsible(false);
    vsplit->addWidget(mid);
    root->addWidget(vsplit, 1);
    m_vsplit = vsplit;

    // Output and Vary per send share the pane under the field editors as
    // tabs (session 128). Vary per send was a box of its own below the
    // status line, permanently costing ~150 px of the editors' height for a
    // table that matters only in interval mode; the tab title still says
    // how many rules there are, so none run unseen.
    m_lowerTabs = new QTabWidget(this);
    m_lowerTabs->setDocumentMode(true);
    m_preview = new QPlainTextEdit(m_lowerTabs);
    m_preview->setObjectName(QStringLiteral("pmOutput"));
    connect(m_preview, &QPlainTextEdit::cursorPositionChanged, this, &PacketMakerDialog::onOutputCursor);
    m_preview->setReadOnly(true);
    m_preview->setFont(UiStyle::monoFont());
    m_preview->setMinimumHeight(3 * m_preview->fontMetrics().lineSpacing());
    m_lowerTabs->addTab(m_preview, tr("Output"));

    // Vary per send. Interval sending rebuilds the frame every tick and
    // applies these rules, so a repeated send is a stream rather than the
    // same bytes with a new sequence number on the envelope.
    auto *varyPage = new QWidget(m_lowerTabs);
    auto *varyLay = new QVBoxLayout(varyPage);
    varyLay->setContentsMargins(0, 4, 0, 0);
    m_varyTable = new QTableWidget(0, 5, varyPage);
    m_varyTable->setHorizontalHeaderLabels(
        { tr("Field"), tr("Mode"), tr("Start / min"), tr("Step"), tr("Max") });
    m_varyTable->horizontalHeaderItem(4)->setToolTip(tr("0 = the field's full width"));
    // The field name takes the spare width; the mode fits its longest
    // choice; the numbers get a fixed, readable width. Before, "Max" took
    // half the table and the combos were cut to "from liv".
    QHeaderView *vh = m_varyTable->horizontalHeader();
    vh->setStretchLastSection(false);
    vh->setSectionResizeMode(0, QHeaderView::Stretch);
    vh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int c = 2; c <= 4; ++c) {
        vh->setSectionResizeMode(c, QHeaderView::Interactive);
        m_varyTable->setColumnWidth(c, fontMetrics().horizontalAdvance(QStringLiteral("0000000000000")));
    }
    m_varyTable->verticalHeader()->setVisible(false);
    varyLay->addWidget(m_varyTable, 1);
    auto *varyBtns = new QHBoxLayout;
    m_varyAddBtn = new QPushButton(tr("Add rule"), varyPage);
    m_varyDelBtn = new QPushButton(tr("Remove rule"), varyPage);
    varyBtns->addWidget(m_varyAddBtn);
    varyBtns->addWidget(m_varyDelBtn);
    varyBtns->addWidget(new QLabel(tr("Applied on every interval send."), varyPage), 1);
    varyLay->addLayout(varyBtns);
    m_lowerTabs->addTab(varyPage, QString());
    QAbstractItemModel *vm = m_varyTable->model();
    connect(vm, &QAbstractItemModel::rowsInserted, this, &PacketMakerDialog::updateVaryTabTitle);
    connect(vm, &QAbstractItemModel::rowsRemoved,  this, &PacketMakerDialog::updateVaryTabTitle);
    connect(vm, &QAbstractItemModel::modelReset,   this, &PacketMakerDialog::updateVaryTabTitle);
    updateVaryTabTitle();

    // Send log (session 241): what went out, when, where — enough to repeat a run.
    auto *logPage = new QWidget(m_lowerTabs);
    auto *logLay = new QVBoxLayout(logPage);
    logLay->setContentsMargins(0, 4, 0, 0);
    // Reply watch (session 243): what came back after each send, and how soon.
    auto *replyRow = new QHBoxLayout;
    m_replyTypes = new QLineEdit(logPage);
    m_replyTypes->setObjectName(QStringLiteral("pmReplyTypes"));
    m_replyTypes->setPlaceholderText(tr("e.g. lsrp, arp — blank: off"));
    m_replyField = new QLineEdit(logPage);
    m_replyField->setObjectName(QStringLiteral("pmReplyField"));
    m_replyField->setPlaceholderText(tr("blank: the first frame"));
    m_replyWindow = new QSpinBox(logPage);
    m_replyWindow->setRange(100, 600000);
    m_replyWindow->setValue(5000);
    m_replyWindow->setSuffix(tr(" ms"));
    replyRow->addWidget(new QLabel(tr("Reply: watch"), logPage));
    replyRow->addWidget(m_replyTypes, 1);
    replyRow->addWidget(new QLabel(tr("for a change in"), logPage));
    replyRow->addWidget(m_replyField, 1);
    replyRow->addWidget(new QLabel(tr("within"), logPage));
    replyRow->addWidget(m_replyWindow);
    logLay->addLayout(replyRow);
    connect(m_replyField, &QLineEdit::textChanged, this, [this]() { m_replyHaveLast = false; });

    // Answer the loco (session 378): ARP -> AAP from a preset, LSRP -> this SLRP.
    auto *answerRow = new QHBoxLayout;
    m_answerLoco = new QCheckBox(tr("Answer the loco: ARP → AAP, LSRP →"), logPage);
    m_answerLoco->setObjectName(QStringLiteral("pmAnswerLoco"));
    m_answerLoco->setToolTip(tr("While armed: each ARP gets an AAP (the preset, with a fresh station random\n"
                                "and the session key derived from the loco's auth keys); each LSRP gets an SLRP."));
    m_answerFrom = new QComboBox(logPage);
    m_answerFrom->setObjectName(QStringLiteral("pmAnswerFrom"));
    m_answerFrom->addItem(tr("this SLRP"));
    m_answerFrom->addItem(tr("the station's routes (KAVACH data in the layout)"));
    m_answerFrom->setItemData(0, tr("This SLRP with the LSRP's FRAME_NUM and the last tag the loco read as\n"
                                    "LAST_REF_RFID; DIST_PKT_START and APPR_SIG_DIST are re-counted from that\n"
                                    "tag, so the start signal and the signal stay where this SLRP puts them.\n"
                                    "Only the form's DEST_LOCO_ID is answered."), Qt::ToolTipRole);
    m_answerFrom->setItemData(1, tr("The SLRP the station would send, from the layout's KAVACH data: the route the\n"
                                    "loco is on, the MA in sections from the aspect of the signal ahead, the\n"
                                    "profile of those sections cut at the MA end, a new REF_PROF_ID sent in full\n"
                                    "until the loco acknowledges it. Every loco is answered, each MACed with the\n"
                                    "session key from its own ARP."), Qt::ToolTipRole);
    m_aapPresetEdit = new QLineEdit(logPage);
    m_aapPresetEdit->setObjectName(QStringLiteral("pmAnswerAap"));
    m_aapPresetEdit->setPlaceholderText(tr("AAP preset (.json)"));
    m_layoutEdit = new QLineEdit(StationLayout::defaultFile(), logPage);
    m_layoutEdit->setObjectName(QStringLiteral("pmAnswerLayout"));
    m_layoutEdit->setToolTip(tr("Station layout: where each RFID tag is; with KAVACH data (docs/STATION_KAVACH.md),\n"
                                "the station's signals and routes that answer as the station"));
    const auto browse = [this, logPage](QLineEdit *e, const QString &filter) {
        const QString p = QFileDialog::getOpenFileName(logPage, QString(), e->text(), filter);
        if (!p.isEmpty()) e->setText(p);
    };
    auto *aapBrowse = new QPushButton(tr("…"), logPage);
    auto *layoutBrowse = new QPushButton(tr("…"), logPage);
    connect(aapBrowse, &QPushButton::clicked, this,
            [this, browse]() { browse(m_aapPresetEdit, tr("Packet preset (*.json)")); });
    connect(layoutBrowse, &QPushButton::clicked, this,
            [this, browse]() { browse(m_layoutEdit, tr("Station layout (*.xlsx *.json)")); });
    answerRow->addWidget(m_answerLoco);
    answerRow->addWidget(m_answerFrom);
    answerRow->addWidget(m_aapPresetEdit, 1);
    answerRow->addWidget(aapBrowse);
    answerRow->addWidget(m_layoutEdit, 1);
    answerRow->addWidget(layoutBrowse);
    m_stnPanelBtn = new QPushButton(tr("Station…"), logPage);
    m_stnPanelBtn->setObjectName(QStringLiteral("pmStationPanel"));
    m_stnPanelBtn->setToolTip(tr("The station's live state while answering as its routes: the signals' aspects,\n"
                                 "the points, what is occupied, each route's authority, the TSRs; and why each\n"
                                 "answer was what it was."));
    m_stnPanelBtn->setEnabled(false);
    answerRow->addWidget(m_stnPanelBtn);
    connect(m_stnPanelBtn, &QPushButton::clicked, this, [this]() {
        if (!m_stnPanel) {
            m_stnPanel = new StationKavachPanel(m_builder.encoder().enumChoices(QStringLiteral("sigAspect")), this);
            m_stnPanel->setStation(m_station, &m_stnState);
            connect(m_stnPanel, &StationKavachPanel::stateChanged, this, [this]() { workStation(); showStationLive(); });
            connect(m_stnPanel, &StationKavachPanel::refused, this, [this](const QString &why) { m_status->fail(why); });
            connect(m_stnPanel, &StationKavachPanel::drawingRequested, this, [this]() { showStationDrawing(); });
        }
        m_stnPanel->show();
        m_stnPanel->raise();
    });
    logLay->addLayout(answerRow);
    connect(m_answerLoco, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !startAnswering()) m_answerLoco->setChecked(false);
    });
    connect(m_answerFrom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_answerLoco->isChecked()) m_answerLoco->setChecked(false);   // a different answer: start it again
    });
    connect(m_answerLoco, &QCheckBox::toggled, this, [this](bool on) {
        m_stnPanelBtn->setEnabled(on && answeringAsStation());
        if (!m_stnPanelBtn->isEnabled() && m_stnPanel) m_stnPanel->hide();
        if (!m_stnPanelBtn->isEnabled() && m_stnDrawing) m_stnDrawing->clearLiveStation();   // no longer live
    });
    auto *replyTimer = new QTimer(this);
    replyTimer->start(250);
    connect(replyTimer, &QTimer::timeout, this,
            [this]() { closeExpiredReplies(QDateTime::currentMSecsSinceEpoch()); });

    m_sendLog = new QTableWidget(0, 8, logPage);
    m_sendLog->setObjectName(QStringLiteral("pmSendLog"));
    m_sendLog->setHorizontalHeaderLabels({ tr("Time"), tr("#"), tr("Packet"), tr("FRAME_NUM"),
                                           tr("To"), tr("Bytes"), tr("Datagram (hex)"), tr("Reply") });
    // The reply is what is read; the hex is long. Reply before it on screen.
    m_sendLog->horizontalHeader()->moveSection(7, 6);
    m_sendLog->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sendLog->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sendLog->verticalHeader()->setVisible(false);
    m_sendLog->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_sendLog->horizontalHeader()->setStretchLastSection(false);
    logLay->addWidget(m_sendLog, 1);
    auto *logBtns = new QHBoxLayout;
    auto *logClear  = new QPushButton(tr("Clear"), logPage);
    auto *logExport = new QPushButton(tr("Export CSV…"), logPage);
    logBtns->addWidget(logClear);
    logBtns->addWidget(logExport);
    logBtns->addWidget(new QLabel(tr("The datagram is what went on the wire, header included."), logPage), 1);
    logLay->addLayout(logBtns);
    m_lowerTabs->addTab(logPage, QString());
    updateSendLogTitle();
    connect(logClear, &QPushButton::clicked, this, [this]() {
        m_sendLog->setRowCount(0);
        m_replyPending.clear();
        updateSendLogTitle();
    });
    connect(logExport, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export send log"),
            QStringLiteral("sendlog_%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
            tr("CSV (*.csv)"));
        if (path.isEmpty()) { return; }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_status->fail(tr("Could not write %1").arg(path));
            return;
        }
        f.write(sendLogCsv().toUtf8());
        m_status->ok(tr("Send log: %1 row(s) written to %2").arg(m_sendLog->rowCount()).arg(path));
    });
    connect(&m_sender, &UdpSender::datagramSent, this, &PacketMakerDialog::onDatagramSent);

    // A tab widget's floor is its largest page's (the Vary table and its
    // buttons, 150 px), which capped the field editors above it. Four lines
    // under the tab bar is enough to read the result; drag for more.
    m_lowerTabs->setMinimumHeight(m_lowerTabs->tabBar()->sizeHint().height()
                                  + 4 * m_preview->fontMetrics().lineSpacing());
    vsplit->addWidget(m_lowerTabs);
    vsplit->setStretchFactor(0, 4);
    vsplit->setStretchFactor(1, 1);
    // Stretch factors only share out a resize; the first layout follows
    // the size hints, which gave the field editors two rows (session 128).
    vsplit->setSizes({ 400, 100 });
    m_status = new StatusLine(this);
    m_status->say(tr("Not built yet."));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // bottom buttons
    auto *btns = new QHBoxLayout;
    m_buildBtn = new QPushButton(tr("Build && Verify"), this);
    UiStyle::makePrimary(m_buildBtn);          // session 125: the window's primary action
    m_sendOnceBtn = new QPushButton(tr("Send Once"), this);
    m_loopbackBtn = new QPushButton(tr("Show in tab"), this);
    m_loopbackBtn->setToolTip(tr("Rebuild and put the frame into a \"Packet Maker loopback\" tab,\n"
                                 "decoded as a received frame would be. Nothing is sent."));
    m_loopbackBtn->setVisible(false);
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(10, 600000);
    m_intervalSpin->setValue(1000);
    m_intervalSpin->setSuffix(tr(" ms"));
    m_intervalBtn = new QPushButton(tr("Start Interval"), this);
    auto *saveBtn = new QPushButton(tr("Save preset…"), this);
    auto *loadBtn2 = new QPushButton(tr("Load preset…"), this);
    btns->addWidget(m_buildBtn);
    btns->addWidget(m_loopbackBtn);
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
    connect(m_loopbackBtn, &QPushButton::clicked, this, &PacketMakerDialog::onLoopback);
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
            m_mid->setSizes(pi.hasSub ? QList<int>{ 600, 460 }
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
                    ? tr("Frame number taken from live traffic — Build & Verify again.")
                    : tr("No ARP/LSRP seen yet; used the clock instead — "
                         "Build & Verify again."));
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

PacketMakerDialog::~PacketMakerDialog()
{
    // A QTableWidget emits modelReset while it is being destroyed, which is
    // after this destructor: the tab-title slot would then run on a
    // half-destroyed dialog and write to a dying tab widget. That corrupted
    // the heap and crashed the next restyle (session 128). Cut it first.
    if (m_varyTable) { m_varyTable->model()->disconnect(this); }
}

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
            m_status->say(tr("Sub-packet edited — Build & Verify again."));
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

        // Session 234: header and sub-packet fields, the way a preset's
        // build() and Packet Sequence apply them (packetpreset.h).
        QVector<Schema::SubEntry> subs = m_subs;
        applyVary(readVaryRules(), sendIndex, m_builder.encoder(), m_packetCombo->currentText(), &header, &subs, live);
        return m_builder.build(m_packetCombo->currentText(), header, subs, sessionKey());
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
        // "MovementAuthority.CUR_SIG_ASPECT" is a sub-packet field (session 234)
        const QString target = fieldCombo->currentText();
        r.field = target.section(QLatin1Char('.'), -1);
        if (target.contains(QLatin1Char('.'))) { r.sub = target.section(QLatin1Char('.'), 0, 0); }
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
            const int idx = fc->findText(r.target());
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
        if (!f.type.isEmpty()) { continue; }   // a float's raw bits are not a number to step
        if (f.name == QLatin1String("PKT_LENGTH") || f.name == QLatin1String("MAC_CODE")) { continue; }
        fieldCombo->addItem(f.name);
    }
    // Session 234: and every scalar field of the packet's sub-packets, as
    // "Struct.FIELD". A rule sets it in every sub-packet of that struct.
    for (const Schema::CaseInfo &c : pi.cases) {
        const Schema::StructLayout lay = m_builder.encoder().structLayout(c.structName);
        for (const Schema::FieldInfo &f : lay.scalarFields) {
            if (f.isPad || f.isCrc) { continue; }
            fieldCombo->addItem(c.structName + QLatin1Char('.') + f.name);
        }
    }
    fieldCombo->setToolTip(tr("A header field, or Struct.FIELD: that field in every sub-packet of the struct"));
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

void PacketMakerDialog::updateVaryTabTitle()
{
    if (!m_lowerTabs || !m_varyTable) { return; }
    const int n = m_varyTable->rowCount();
    m_lowerTabs->setTabText(1, n == 0 ? tr("Vary per send")
                             : n == 1 ? tr("Vary per send (1 rule)")
                                      : tr("Vary per send (%1 rules)").arg(n));
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
    m_hexStart = out.size();
    m_hexBytes = r.frame.size();
    out += QString::fromLatin1(r.frame.toHex(' ')) + "\n";
    out += tr("  click a byte to see its field(s), or a decoded field to see its bytes\n\n");
    out += tr("PKT_LENGTH = %1   CRC = %2   MAC = %3%4\n\n")
               .arg(r.pktLength).arg(r.crcHex)
               .arg(r.hasMac ? r.macHex : tr("(none)"))
               .arg(r.macKeyed ? QString() : tr(" [placeholder]"));
    out += tr("receiver would (at build time):\n");
    const QVector<ReceiverLine> recv = receiverLinesFor(r);
    for (const ReceiverLine &l : recv) {
        out += QStringLiteral("  %1 %2\n").arg(l.notProcessed ? QStringLiteral("✗") : QStringLiteral("·"), l.text);
    }
    bool anyNot = false;
    for (const ReceiverLine &l : recv) { anyNot |= l.notProcessed; }
    if (!anyNot) { out += tr("  no reject condition matched (%1 rules checked)\n").arg(kavachRejectRules().count()); }
    out += QLatin1Char('\n');
    out += tr("decoded back:\n");
    m_rowLines.clear();
    for (const FieldRow &fr : r.decoded) {
        if (fr.hasSpan()) { m_rowLines.append({ int(out.count(QLatin1Char('\n'))), fr }); }
        out += QStringLiteral("  %1  %2\n").arg(fr.field, -28).arg(fr.value);
    }
    m_preview->setPlainText(out);
}

void PacketMakerDialog::onOutputCursor()
{
    // A click in the hex lights up every field with a bit in that byte (bit-
    // packed: a byte can hold several) and their bytes; a click on a decoded
    // field lights up its line and its bytes.
    QList<QTextEdit::ExtraSelection> sels;
    QTextDocument *doc = m_preview->document();
    const int pos = m_preview->textCursor().position();
    QVector<const FieldRow *> picked;
    QSet<int> pickedLines;
    if (m_hexStart >= 0 && pos >= m_hexStart && pos < m_hexStart + m_hexBytes * 3) {
        const int b = (pos - m_hexStart) / 3;
        for (const auto &rl : m_rowLines) {
            if (b >= rl.second.byteStart() && b <= rl.second.byteEnd()) {
                picked << &rl.second; pickedLines << rl.first;
            }
        }
    } else {
        const int line = doc->findBlock(pos).blockNumber();
        for (const auto &rl : m_rowLines) {
            if (rl.first == line) { picked << &rl.second; pickedLines << rl.first; }
        }
    }
    QColor hl = m_preview->palette().highlight().color();
    hl.setAlpha(90);
    const auto mark = [&](int from, int to) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(doc);
        sel.cursor.setPosition(from);
        sel.cursor.setPosition(to, QTextCursor::KeepAnchor);
        sel.format.setBackground(hl);
        sels.append(sel);
    };
    for (const FieldRow *r : picked) {
        for (int b = r->byteStart(); b <= r->byteEnd() && b < m_hexBytes; ++b) {
            mark(m_hexStart + b * 3, m_hexStart + b * 3 + 2);
        }
    }
    for (int line : pickedLines) {
        const QTextBlock blk = doc->findBlockByNumber(line);
        mark(blk.position(), blk.position() + blk.length() - 1);
    }
    m_preview->setExtraSelections(sels);
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
            m_hexStart = -1; m_rowLines.clear();
            m_preview->setPlainText(QString());
            m_status->fail(tr("Build failed: %1").arg(exErr));
            return;
        }
    }

    const PacketBuilder::Result r = buildNow();
    if (!r.ok) {
        m_lastFrame.clear();
        setSendEnabled(false);
        m_hexStart = -1; m_rowLines.clear();
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
    int notProcessed = 0;
    for (const ReceiverLine &l : receiverLinesFor(r)) { notProcessed += l.notProcessed ? 1 : 0; }
    if (notProcessed > 0) {
        st << tr("receiver would not process it: %n reason(s), see Output", nullptr, notProcessed);
        m_status->warn(st.join(QStringLiteral("  ·  ")));
    } else {
        m_status->ok(st.join(QStringLiteral("  ·  ")));
    }
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
    m_loopbackBtn->setEnabled(m_builtOk);     // nothing leaves the laptop: no arming
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

QSet<QString> PacketMakerDialog::replyTypes() const
{
    QSet<QString> out;
    for (const QString &t : m_replyTypes->text().split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                                       Qt::SkipEmptyParts)) {
        out.insert(t.toLower());
    }
    return out;
}

void PacketMakerDialog::setReply(int row, const QString &text)
{
    if (row < 0 || row >= m_sendLog->rowCount()) { return; }
    m_sendLog->setItem(row, 7, new QTableWidgetItem(text));
}

void PacketMakerDialog::setReplyWatchForTest(const QString &types, const QString &field, int windowMs)
{
    m_replyTypes->setText(types);
    m_replyField->setText(field);
    m_replyWindow->setValue(windowMs);
}

void PacketMakerDialog::observe(const LogEntryPtr &entry)
{
    if (entry.isNull() || entry->header.source_id == kLoopbackSourceId) { return; }
    const QString t = entry->text.trimmed();
    if (!t.startsWith(QLatin1Char('@'))) { return; }
    if (m_answerLoco->isChecked() && m_armed->isChecked()) {
        const CaptureLine c = CaptureDecoder::parseLine(t);
        if (c.valid && (c.type == CapType::ARP || c.type == CapType::LSRP)) {
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(c, nullptr, 0, &raw);
            if (c.type == CapType::ARP) answerArp(c, raw, entry->epochMs);
            else answerLsrp(raw);
        }
    }
    const QSet<QString> types = replyTypes();
    if (types.isEmpty()) { return; }
    const QString captype = t.mid(1, t.indexOf(QLatin1Char(' ')) - 1).section(QLatin1Char('_'), 0, 0).toLower();
    if (!types.contains(captype)) { return; }

    const QString field = m_replyField->text().trimmed();
    QString text;
    qint64 now = 0;
    if (!field.isEmpty()) {
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(t), nullptr, 0, &raw);
        const auto it = raw.constFind(field);
        if (it == raw.constEnd()) { return; }
        now = it.value();
        m_replyHaveLast = true;
        m_replyLast = now;
    }
    for (int i = m_replyPending.size() - 1; i >= 0; --i) {
        const ReplyPending &p = m_replyPending[i];
        const qint64 dt = entry->epochMs - p.sentMs;
        if (dt < 0) { continue; }
        if (field.isEmpty()) {
            text = tr("%1 after %2 ms").arg(captype).arg(dt);
        } else if (!p.haveBase) {
            // Nothing seen before the send: the first value is the baseline,
            // not a change.
            m_replyPending[i].haveBase = true;
            m_replyPending[i].base = now;
            continue;
        } else if (now != p.base) {
            text = tr("%1 %2 → %3 after %4 ms (%5)").arg(field).arg(p.base).arg(now).arg(dt).arg(captype);
        } else {
            continue;
        }
        setReply(p.row, text);
        m_replyPending.remove(i);
    }
}

bool PacketMakerDialog::answeringAsStation() const
{
    return m_answerFrom && m_answerFrom->currentIndex() == 1;
}

StationLayoutWindow *PacketMakerDialog::stationDrawing() const { return m_stnDrawing.data(); }

StationLayoutWindow *PacketMakerDialog::showStationDrawing()
{
    if (!m_stnDrawing) {
        auto *w = new StationLayoutWindow(this);
        if (!w->openFile(m_stnLayoutPath)) {
            m_status->fail(tr("The station layout %1 did not open in Station Layout.").arg(m_stnLayoutPath));
            w->close();
            return nullptr;
        }
        m_stnDrawing = w;
        // Session 435: signals, points and routes changed on the drawing are the station's.
        connect(w, &StationLayoutWindow::liveStateEdited, this, [this](const StationKavach::State &st) {
            m_stnState = st;
            workStation();   // session 442
            if (m_stnPanel) m_stnPanel->refresh();
            showStationLive();
        });
    }
    showStationLive();
    m_stnDrawing->show();
    m_stnDrawing->raise();
    return m_stnDrawing;
}

void PacketMakerDialog::showStationLive()
{
    if (!m_stnDrawing) return;
    if (!m_answerLoco->isChecked() || !answeringAsStation()) { m_stnDrawing->clearLiveStation(); return; }
    QList<StationKavach::Loco> locos;
    QList<int> ids = m_stnLocos.keys();
    std::sort(ids.begin(), ids.end());
    for (int id : ids) locos << m_stnLocos.value(id);
    m_stnDrawing->showLiveStation(m_station, m_stnState, locos, tr("Packet Maker"));
}

bool PacketMakerDialog::startAnswering()
{
    if (answeringAsStation()) {
        PacketPreset aap;
        QString err;
        if (!PacketPreset::load(m_aapPresetEdit->text().trimmed(), &aap, &err) || aap.captype != QLatin1String("aap")) {
            m_status->fail(tr("Not answering — the AAP preset: %1").arg(err.isEmpty() ? tr("not an AAP") : err));
            return false;
        }
        StationLayout::Layout layout;
        StationKavach::Station station;
        if (!StationLayout::load(m_layoutEdit->text().trimmed(), &layout, &err)) {
            m_status->fail(tr("Not answering — the station layout: %1").arg(err));
            return false;
        }
        if (layout.kavach.isEmpty() || !StationKavach::fromJson(layout.kavach, &station, &err)) {
            m_status->fail(tr("Not answering — the layout has no KAVACH data (signals and routes)%1.")
                               .arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err));
            return false;
        }
        station.tagLocs = StationLayout::tagLocationsByNumber(layout);   // session 426: SPAD seen
        m_aapFields = aap.header;
        m_station = station;
        m_stnState = StationKavach::State::initial(station);
        m_stnLocos.clear();
        m_stnTrack = StationInterlock::trackOf(layout);   // session 441
        m_stnState.interlocking = !m_stnTrack.isEmpty();  // session 442: with track circuits, on
        m_stnTrackNotes.clear();
        m_stnLayoutPath = m_layoutEdit->text().trimmed();
        if (m_stnPanel) m_stnPanel->setStation(m_station, &m_stnState);
        showStationLive();
        const QStringList bad = StationKavach::checks(station);
        if (!bad.isEmpty())
            m_status->warn(tr("Answering as station %1 while armed; the data: %2").arg(station.stationId).arg(bad.join(QStringLiteral("; "))));
        else
            m_status->ok(tr("Answering as station %1 while armed (%2 routes, %3 signals).")
                             .arg(station.stationId).arg(station.routes.size()).arg(station.signalList.size()));
        return true;
    }
    if (currentCaptype() != QLatin1String("slrp")) {
        m_status->fail(tr("Answering needs an SLRP in the form."));
        return false;
    }
    PacketPreset aap;
    QString err;
    if (!PacketPreset::load(m_aapPresetEdit->text().trimmed(), &aap, &err) || aap.captype != QLatin1String("aap")) {
        m_status->fail(tr("Not answering — the AAP preset: %1").arg(err.isEmpty() ? tr("not an AAP") : err));
        return false;
    }
    StationLayout::Layout layout;
    if (!StationLayout::load(m_layoutEdit->text().trimmed(), &layout, &err)) {
        m_status->fail(tr("Not answering — the station layout: %1").arg(err));
        return false;
    }
    m_aapFields = aap.header;
    m_stnTags = StationAnswer::tagLocations(layout);
    m_stnRef = -1;
    const qint64 ref = readHeader().value(QStringLiteral("LAST_REF_RFID"));
    if (!m_stnTags.contains(int(ref))) {
        m_status->fail(tr("Not answering — LAST_REF_RFID %1 is not in the station layout.").arg(ref));
        return false;
    }
    m_status->ok(tr("Answering the loco while armed (%1 tags in the layout).").arg(m_stnTags.size()));
    return true;
}

void PacketMakerDialog::answerArp(const CaptureLine &c, const QHash<QString, qint64> &arp, qint64 epochMs)
{
    const quint16 rnd = m_stnRandomForTest ? m_stnRandomForTest
                                           : quint16(QRandomGenerator::global()->bounded(1, 0x10000));
    const QHash<QString, qint64> h = StationAnswer::aapFor(m_aapFields, arp, rnd);
    const int locoId = int(arp.value(QStringLiteral("SOURCE_LOCO_ID")));
    const int stnId = int(h.value(QStringLiteral("SOURCE_STN_ID")));
    if (answeringAsStation()) {   // session 441: where it is, before anything else
        noteStationLoco(arp);
        if (m_stnPanel) m_stnPanel->refresh();
        showStationLive();
    }
    if (!m_keys->haveSet(0, c.locoId) || !m_keys->haveSet(1, c.locoId)) {
        m_status->fail(tr("ARP from loco %1 not answered — no auth keys for it in the log.").arg(locoId));
        return;
    }
    const SessionKeyGen::Result k = SessionKeyGen::derive(
        m_keys->keySet(0, c.locoId), m_keys->keySet(1, c.locoId),
        quint16(arp.value(QStringLiteral("LOCO_RND_NUM_RL"))), rnd, locoId, stnId,
        QDateTime::fromMSecsSinceEpoch(epochMs));
    if (!k.ok) {
        m_status->fail(tr("ARP from loco %1 not answered — session key: %2").arg(locoId).arg(k.error));
        return;
    }
    // The SLRPs that follow are MACed with this key.
    m_keyEdit->setText(QString::fromLatin1(k.sessionKey.toHex()));
    m_locoKeys.insert(locoId, k.sessionKey);
    const PacketBuilder::Result r = m_builder.build(QStringLiteral("aap"), h, {}, k.sessionKey);
    sendAnswer(QStringLiteral("aap"), r,
               tr("answers ARP %1 from loco %2, station random %3")
                   .arg(arp.value(QStringLiteral("FRAME_NUM"))).arg(locoId)
                   .arg(QStringLiteral("%1").arg(rnd, 4, 16, QLatin1Char('0')).toUpper()));
}

void PacketMakerDialog::answerLsrp(const QHash<QString, qint64> &lsrp)
{
    const qint64 fn = lsrp.value(QStringLiteral("FRAME_NUM"));
    if (answeringAsStation()) {
        const int locoId = int(lsrp.value(QStringLiteral("SOURCE_LOCO_ID")));
        noteStationLoco(lsrp);   // session 441: the circuits it occupies, before it is answered
        if (m_stnPanel) m_stnPanel->refresh();
        QList<StationKavach::Loco> others;   // session 427: a loco ahead cuts the MA
        for (auto it = m_stnLocos.cbegin(); it != m_stnLocos.cend(); ++it) if (it.key() != locoId) others << it.value();
        StationKavach::Loco &loco = m_stnLocos[locoId];
        const StationKavach::Answer a = StationKavach::answer(m_station, m_stnState, &loco, lsrp, m_builder.encoder(), others);
        if (!a.ok) {
            m_status->fail(tr("LSRP %1 from loco %2 not answered — %3").arg(fn).arg(locoId).arg(a.why));
            if (m_stnPanel) m_stnPanel->showAnswer(locoId, tr("LSRP %1 NOT answered: %2").arg(fn).arg(a.why), {});
            return;
        }
        showStationLive();   // session 428: where it is, what it was given
        if (!a.checks.isEmpty()) m_status->warn(tr("Loco %1: %2").arg(locoId).arg(a.checks.join(QStringLiteral("; "))));
        if (m_stnPanel) m_stnPanel->showAnswer(locoId, tr("LSRP %1 answered: %2").arg(fn).arg(a.note), a.checks);
        sendAnswer(QStringLiteral("slrp"),
                   m_builder.build(QStringLiteral("slrp"), a.header, a.subs, m_locoKeys.value(locoId, sessionKey())),
                   tr("answers LSRP %1: %2").arg(fn).arg(a.note));
        return;
    }
    commitCurrentSub();
    QHash<QString, qint64> h = readHeader();
    if (lsrp.value(QStringLiteral("SOURCE_LOCO_ID")) != h.value(QStringLiteral("DEST_LOCO_ID"))) return;
    QVector<Schema::SubEntry> subs = m_subs;
    const StationAnswer::Reply a = StationAnswer::slrpFor(lsrp, m_stnTags, &m_stnRef, &h, &subs);
    if (!a.ok) {
        m_status->fail(tr("LSRP %1 not answered — %2").arg(lsrp.value(QStringLiteral("FRAME_NUM"))).arg(a.why));
        return;
    }
    sendAnswer(QStringLiteral("slrp"), m_builder.build(QStringLiteral("slrp"), h, subs, sessionKey()),
               tr("answers LSRP %1: %2").arg(lsrp.value(QStringLiteral("FRAME_NUM"))).arg(a.note));
}

void PacketMakerDialog::noteStationLoco(const QHash<QString, qint64> &pkt)
{
    StationKavach::note(&m_stnLocos[int(pkt.value(QStringLiteral("SOURCE_LOCO_ID")))], pkt);
    const StationInterlock::Occupancy occ = StationInterlock::occupancy(m_stnTrack, m_stnLocos.values());
    m_stnState.occupiedBy = occ.by;
    if (occ.notes != m_stnTrackNotes && !occ.notes.isEmpty() && !m_stnTrack.isEmpty())
        m_status->warn(tr("Track circuits: %1").arg(occ.notes.join(QStringLiteral("; "))));
    m_stnTrackNotes = occ.notes;
    workStation();   // session 442
}

void PacketMakerDialog::workStation()
{
    const QStringList ev = StationInterlock::work(m_station, &m_stnState);
    if (ev.isEmpty()) return;
    m_status->ok(tr("Interlocking: %1").arg(ev.join(QStringLiteral("; "))));
    if (m_stnPanel) {
        m_stnPanel->showInterlocking(ev);
        // Queued: this may run inside one of the panel's own combo boxes, which a rebuild deletes.
        QPointer<StationKavachPanel> p = m_stnPanel;
        QMetaObject::invokeMethod(this, [p]() { if (p) p->refresh(); }, Qt::QueuedConnection);
    }
}

void PacketMakerDialog::sendAnswer(const QString &captype, const PacketBuilder::Result &r, const QString &note)
{
    if (!r.ok) {
        m_status->fail(tr("%1 not sent — %2").arg(captype.toUpper(), r.error));
        return;
    }
    const QVector<UdpSender::Target> dests = destinations();
    if (dests.isEmpty()) {
        m_status->fail(tr("%1 not sent — no destination.").arg(captype.toUpper()));
        return;
    }
    m_pendingCaptype = captype;
    m_pendingFrameNum = r.raw.value(QStringLiteral("FRAME_NUM"), -1);
    const int row = m_sendLog->rowCount();
    m_sender.sendOnce(r.frame, dests, headerPrefix(captype, r.frame.size()));
    m_pendingCaptype.clear();
    if (m_sendLog->rowCount() > row) setReply(m_sendLog->rowCount() - 1, note);
}

void PacketMakerDialog::closeExpiredReplies(qint64 nowMs)
{
    const QString field = m_replyField->text().trimmed();
    for (int i = m_replyPending.size() - 1; i >= 0; --i) {
        const ReplyPending &p = m_replyPending[i];
        if (nowMs - p.sentMs <= m_replyWindow->value()) { continue; }
        if (field.isEmpty()) {
            setReply(p.row, tr("nothing within %1 ms").arg(m_replyWindow->value()));
        } else if (p.haveBase) {
            setReply(p.row, tr("no change within %1 ms (%2 = %3)").arg(m_replyWindow->value()).arg(field).arg(p.base));
        } else {
            setReply(p.row, tr("%1 not seen within %2 ms").arg(field).arg(m_replyWindow->value()));
        }
        m_replyPending.remove(i);
    }
}

void PacketMakerDialog::updateSendLogTitle()
{
    const int n = m_sendLog ? m_sendLog->rowCount() : 0;
    m_lowerTabs->setTabText(2, n == 0 ? tr("Send log") : tr("Send log (%1)").arg(n));
}

void PacketMakerDialog::onDatagramSent(const QByteArray &datagram, const QStringList &reached)
{
    // Oldest dropped past the cap: an interval run at 100 ms is 36000 rows an hour.
    if (m_sendLog->rowCount() >= kSendLogMax) {
        m_sendLog->removeRow(0);
        for (ReplyPending &p : m_replyPending) { --p.row; }
    }
    const int row = m_sendLog->rowCount();
    m_sendLog->insertRow(row);
    const QStringList cells = {
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
        QString::number(m_sender.sentCount()),
        m_pendingCaptype.isEmpty() ? m_packetCombo->currentText() : m_pendingCaptype,
        m_pendingFrameNum >= 0 ? QString::number(m_pendingFrameNum) : QString(),
        reached.join(QStringLiteral("; ")),
        QString::number(datagram.size()),
        QString::fromLatin1(datagram.toHex(' ')).toUpper(),
    };
    for (int c = 0; c < cells.size(); ++c) { m_sendLog->setItem(row, c, new QTableWidgetItem(cells[c])); }
    if (!replyTypes().isEmpty()) {
        ReplyPending p;
        p.row = row;
        p.sentMs = QDateTime::currentMSecsSinceEpoch();
        p.haveBase = m_replyHaveLast;
        p.base = m_replyLast;
        m_replyPending << p;
        setReply(row, tr("watching…"));
    }
    updateSendLogTitle();
}

QString PacketMakerDialog::sendLogCsv() const
{
    const auto quote = [](QString v) {
        if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char(';'))) {
            v.replace(QLatin1String("\""), QLatin1String("\"\""));
            v = QLatin1Char('"') + v + QLatin1Char('"');
        }
        return v;
    };
    QString out = QStringLiteral("time,send,packet,frame_num,to,bytes,datagram_hex,reply\n");
    for (int r = 0; r < m_sendLog->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < m_sendLog->columnCount(); ++c) {
            const QTableWidgetItem *it = m_sendLog->item(r, c);
            cells << quote(it ? it->text() : QString());
        }
        out += cells.join(QLatin1Char(',')) + QLatin1Char('\n');
    }
    return out;
}

void PacketMakerDialog::setLoopbackAvailable(bool on) { m_loopbackBtn->setVisible(on); }

QString PacketMakerDialog::loopbackLine(const QString &captype, const QByteArray &frame,
                                        const QByteArray &envelope, int bodyOffsetBytes,
                                        const QDateTime &at, QString *err, QString *note)
{
    QByteArray bytes = frame;
    if (bodyOffsetBytes > 0) {
        if (envelope.size() > bodyOffsetBytes) {
            if (err) {
                *err = tr("%1 is logged with a %2-byte envelope in front; this window's is %3 B. "
                          "Untick extra header fields to fit.")
                           .arg(captype).arg(bodyOffsetBytes).arg(envelope.size());
            }
            return QString();
        }
        const int pad = bodyOffsetBytes - envelope.size();
        if (pad > 0 && note) {
            *note = tr("the log envelope's last %1 byte(s) are zero (this window sends %2 B, %3 is logged with %4)")
                        .arg(pad).arg(envelope.size()).arg(captype).arg(bodyOffsetBytes);
        }
        bytes.prepend(envelope + QByteArray(pad, '\0'));
    }
    return QStringLiteral("@%1_0_0 %2 0 %3")
        .arg(captype, at.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")),
             QString::fromLatin1(bytes.toHex(' ')).toUpper());
}

void PacketMakerDialog::onLoopback()
{
    // Rebuilt now, as Send Once does, so the frame number is the current one.
    const PacketBuilder::Result r = buildNow();
    if (!r.ok) {
        m_status->fail(tr("Not shown — rebuild failed: %1").arg(r.error));
        return;
    }
    m_lastFrame = r.frame;
    refreshPreview(r);
    const QString captype = currentCaptype();
    const Schema::PacketInfo pi = m_builder.encoder().packet(captype);
    QString err, note;
    const QString line = loopbackLine(captype, r.frame, currentHeaderPreview(captype, r.frame.size()),
                                      pi.ok ? pi.bodyOffsetBits / 8 : 0,
                                      QDateTime::currentDateTime(), &err, &note);
    if (line.isEmpty()) { m_status->fail(tr("Not shown — %1").arg(err)); return; }
    emit loopbackRequested(line);
    const QString done = tr("Shown in the \"Packet Maker loopback\" tab. Nothing was sent.");
    if (note.isEmpty()) { m_status->ok(done); } else { m_status->warn(done + QStringLiteral("  ·  ") + note); }
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
    m_pendingFrameNum = r.raw.value(QStringLiteral("FRAME_NUM"), -1);
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
            m_pendingFrameNum = r.raw.value(QStringLiteral("FRAME_NUM"), -1);
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
        m_status->ok(tr("Loaded %1. Build & Verify before sending.").arg(path));
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
                         "then Build & Verify — PKT_LENGTH, the MAC and the CRC "
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
        int bits = headerFieldBits(r.field);
        if (!r.sub.isEmpty()) {
            bits = 0;
            for (const Schema::FieldInfo &f : m_builder.encoder().structLayout(r.sub).scalarFields)
                if (f.name == r.field) { bits = f.bits; break; }
        }
        parts << r.describe(bits);
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
    if (m_answerLoco && !answeringAsStation()) m_answerLoco->setChecked(false);   // the station's answer is not the form's
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

    m_status->say(tr("Edit fields, then Build & Verify."));
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
        m_status->warn(tr("Extra header fields changed — Build & Verify again to send."));
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
