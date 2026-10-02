#include "fieldsweepdialog.h"
#include "windowgeometry.h"
#include "statusline.h"
#include "uistyle.h"
#include <QCloseEvent>
#include "sendguard.h"

#include "packetmakerdialog.h"      // parseBuffer(), shared with the maker
#include "sessionkeystore.h"

#include <QCheckBox>
#include <QRegularExpression>
#include <QTime>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// Columns of the results table.
enum { ColValue = 0, ColNote, ColSent, ColSeen, ColVerdict, ColCount };

QVector<qint64> parseList(const QString &text)
{
    QVector<qint64> out;
    const QStringList toks = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                        Qt::SkipEmptyParts);
    for (const QString &t : toks) {
        bool ok = false;
        const qint64 v = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                             ? t.mid(2).toLongLong(&ok, 16)
                             : t.toLongLong(&ok, 10);
        if (ok) { out.push_back(v); }
    }
    return out;
}

// "1 value" / "5 values" (the status read "5 value(s) sent").
QString countOf(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QCoreApplication::translate("FieldSweepDialog", n == 1 ? one : many));
}

}  // namespace

FieldSweepDialog::FieldSweepDialog(QWidget *parent, SessionKeyStore *keys)
    : QDialog(parent)
{
    m_keys = keys ? keys : new SessionKeyStore(this);
    setWindowTitle(tr("Field Sweep"));
    WindowGeometry::makeResizableWindow(this);
    // Setup in two columns over the results: stacked, the three boxes took
    // 787 px (more than a 768-px screen) and left the results one row.
    resize(1100, 700);

    auto *root = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    auto *right = new QVBoxLayout;

    // ---- what to build ---------------------------------------------------
    auto *what = new QGroupBox(tr("Packet"), this);
    auto *wf = new QFormLayout(what);
    m_packetBox = new QComboBox(what);
    if (m_builder.ready()) {
        for (const QString &n : m_builder.encoder().packetNames()) {
            m_packetBox->addItem(n, n);
        }
    }
    wf->addRow(tr("Type:"), m_packetBox);

    m_baseTable = new QTableWidget(0, 2, what);
    m_baseTable->setHorizontalHeaderLabels({ tr("Field"), tr("Base value") });
    m_baseTable->verticalHeader()->setVisible(false);
    // Names whole ("SOURCE_LOCO_..." was cut); the value takes the rest.
    m_baseTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_baseTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_baseTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // As tall as the Sweep and Send column beside it (was capped at 170 px,
    // and showed one field of lsrp's 29).
    m_baseTable->setMinimumHeight(m_baseTable->verticalHeader()->defaultSectionSize() * 4);
    wf->addRow(m_baseTable);     // full width: its header says what it holds

    auto *seedRow = new QHBoxLayout;
    auto *seedEdit = new QLineEdit(what);
    seedEdit->setPlaceholderText(tr("paste a captured frame to seed the values above"));
    auto *seedBtn = new QPushButton(tr("Seed"), what);
    seedRow->addWidget(seedEdit, 1);
    seedRow->addWidget(seedBtn);
    wf->addRow(tr("Seed from:"), seedRow);
    connect(seedBtn, &QPushButton::clicked, this,
            [this, seedEdit]() { seedFromBuffer(seedEdit->text()); });

    top->addWidget(what, 1);

    // ---- what to sweep ---------------------------------------------------
    auto *sweep = new QGroupBox(tr("Sweep"), this);
    auto *sf = new QFormLayout(sweep);
    m_sweepForm = sf;
    m_fieldBox = new QComboBox(sweep);
    sf->addRow(tr("Field:"), m_fieldBox);

    m_modeBox = new QComboBox(sweep);
    m_modeBox->addItem(tr("Boundary values"), int(FieldSweep::Mode::Boundary));
    m_modeBox->addItem(tr("Range"),           int(FieldSweep::Mode::Range));
    m_modeBox->addItem(tr("List"),            int(FieldSweep::Mode::List));
    m_modeBox->addItem(tr("Enum codes"),      int(FieldSweep::Mode::EnumCodes));
    m_modeBox->setToolTip(tr("Boundary is the default: bit-packed field bugs live at\n"
                             "the edges — off-by-one at the top of the range, sign\n"
                             "handling at -1, the code one past the last defined enum."));
    sf->addRow(tr("Mode:"), m_modeBox);

    m_rangeRow = new QWidget(sweep);
    auto *rangeRow = new QHBoxLayout(m_rangeRow);
    rangeRow->setContentsMargins(0, 0, 0, 0);
    m_fromEdit = new QLineEdit(QStringLiteral("0"), sweep);
    m_toEdit   = new QLineEdit(QStringLiteral("10"), sweep);
    m_stepEdit = new QLineEdit(QStringLiteral("1"), sweep);
    rangeRow->addWidget(new QLabel(tr("from"), sweep)); rangeRow->addWidget(m_fromEdit);
    rangeRow->addWidget(new QLabel(tr("to"),   sweep)); rangeRow->addWidget(m_toEdit);
    rangeRow->addWidget(new QLabel(tr("step"), sweep)); rangeRow->addWidget(m_stepEdit);
    sf->addRow(tr("Range:"), m_rangeRow);

    m_listEdit = new QLineEdit(sweep);
    m_listEdit->setPlaceholderText(tr("0, 1, 7, 0x1F …"));
    sf->addRow(tr("List:"), m_listEdit);

    right->addWidget(sweep);

    // ---- how to send -----------------------------------------------------
    auto *send = new QGroupBox(tr("Send"), this);
    auto *ef = new QFormLayout(send);
    auto *destRow = new QHBoxLayout;
    m_destEdit = new QLineEdit(QStringLiteral("127.0.0.1"), send);
    m_portSpin = new QSpinBox(send);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(50000);
    destRow->addWidget(m_destEdit, 1);
    destRow->addWidget(new QLabel(tr("port"), send));
    destRow->addWidget(m_portSpin);
    ef->addRow(tr("Destination:"), destRow);

    auto *timeRow = new QHBoxLayout;
    m_gapSpin = new QSpinBox(send);
    m_gapSpin->setRange(10, 60000);
    m_gapSpin->setValue(500);
    m_gapSpin->setSuffix(tr(" ms"));
    m_windowSpin = new QSpinBox(send);
    m_windowSpin->setRange(10, 60000);
    m_windowSpin->setValue(400);
    m_windowSpin->setSuffix(tr(" ms"));
    timeRow->addWidget(new QLabel(tr("gap between values"), send));
    timeRow->addWidget(m_gapSpin);
    timeRow->addWidget(new QLabel(tr("answer window"), send));
    timeRow->addWidget(m_windowSpin);
    timeRow->addStretch(1);
    ef->addRow(tr("Timing:"), timeRow);

    m_replyEdit = new QLineEdit(send);
    m_replyEdit->setPlaceholderText(tr("aap, slrp — captypes that count as an answer"));
    m_replyEdit->setToolTip(tr("Leave blank to score any traffic as 'other'.\n"
                               "Fault packets are always noticed separately."));
    ef->addRow(tr("Reply types:"), m_replyEdit);

    auto *keyRow = new QHBoxLayout;
    m_keyEdit = new QLineEdit(send);
    m_keyEdit->setMaxLength(32);
    m_keyEdit->setPlaceholderText(tr("session key (32 hex) — blank for a zero MAC"));
    m_keySetBox = new QComboBox(send);
    keyRow->addWidget(m_keyEdit, 3);
    keyRow->addWidget(new QLabel(tr("from log:"), send));
    keyRow->addWidget(m_keySetBox, 2);
    ef->addRow(tr("MAC key:"), keyRow);

    m_stopOnReply = new QCheckBox(tr("Stop at the first value that draws a reply"), send);
    ef->addRow(QString(), m_stopOnReply);

    right->addWidget(send);
    right->addStretch(1);
    top->addLayout(right, 1);
    root->addLayout(top);

    // ---- run -------------------------------------------------------------
    // The buttons and what they report on one row.
    auto *runRow = new QHBoxLayout;
    auto *previewBtn = new QPushButton(tr("Preview plan"), this);
    m_startBtn = new QPushButton(tr("Start sweep"), this);
    UiStyle::makePrimary(m_startBtn);
    runRow->addWidget(previewBtn);
    runRow->addWidget(m_startBtn);

    m_status = new StatusLine(this);
    m_status->say(tr("Pick a packet and a field."));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runRow->addWidget(m_status, 1);
    root->addLayout(runRow);

    m_results = new QTableWidget(0, ColCount, this);
    m_results->setHorizontalHeaderLabels({ tr("Value"), tr("Why"), tr("Sent"),
                                           tr("Seen"), tr("Verdict") });
    m_results->verticalHeader()->setVisible(false);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // UiStyle's mono, not systemFont(FixedFont), which can come out
    // proportional. Every column but Seen fits its text ("23:32:03.4..."
    // was clipped).
    m_results->setFont(UiStyle::monoFont());
    m_results->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_results->horizontalHeader()->setSectionResizeMode(ColSeen, QHeaderView::Stretch);
    root->addWidget(m_results, 1);

    connect(m_packetBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FieldSweepDialog::onPacketChanged);
    connect(m_fieldBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FieldSweepDialog::onFieldChanged);
    connect(m_modeBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateModeRows(); onPreview(); });
    updateModeRows();
    connect(previewBtn, &QPushButton::clicked, this, &FieldSweepDialog::onPreview);
    connect(m_startBtn, &QPushButton::clicked, this, &FieldSweepDialog::onStartStop);
    connect(&m_stepTimer, &QTimer::timeout, this, &FieldSweepDialog::onStep);
    connect(&m_sender, &UdpSender::error, this, [this](const QString &e) {
        m_status->fail(tr("send failed: %1").arg(e));
        setRunning(false);
    });

    refreshKeySets();
    connect(m_keys, &SessionKeyStore::changed,
            this, &FieldSweepDialog::refreshKeySets);
    connect(m_keySetBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const int id = m_keySetBox->currentData().toInt();
        if (id <= 0) { return; }
        if (const KeySnapshot *k = (*m_keys).snapshot(id)) {
            if (k->result.ok) {
                m_keyEdit->setText(QString::fromLatin1(k->result.sessionKey.toHex()));
            }
        }
    });

    onPacketChanged();
}

void FieldSweepDialog::updateModeRows()
{
    // Range and List only in the modes that read them; Boundary and Enum
    // codes take their values from the field itself.
    const auto mode = FieldSweep::Mode(m_modeBox->currentData().toInt());
    const bool range = mode == FieldSweep::Mode::Range;
    const bool list  = mode == FieldSweep::Mode::List;
    m_rangeRow->setVisible(range);
    if (QWidget *l = m_sweepForm->labelForField(m_rangeRow)) l->setVisible(range);
    m_listEdit->setVisible(list);
    if (QWidget *l = m_sweepForm->labelForField(m_listEdit)) l->setVisible(list);
}

void FieldSweepDialog::refreshKeySets()
{
    if (!m_keySetBox) { return; }
    const int keep = m_keySetBox->currentData().toInt();
    const QSignalBlocker block(m_keySetBox);
    m_keySetBox->clear();
    m_keySetBox->addItem(tr("(none — type a key)"), 0);
    for (const KeySnapshot &k : (*m_keys).snapshots()) {
        m_keySetBox->addItem(k.label(), k.id);
    }
    const int idx = m_keySetBox->findData(keep);
    m_keySetBox->setCurrentIndex(idx >= 0 ? idx : 0);
}

void FieldSweepDialog::onPacketChanged()
{
    m_fieldBox->clear();
    m_baseTable->setRowCount(0);
    if (!m_builder.ready()) {
        m_status->fail(tr("The schema encoder is not loaded — nothing can be built."));
        return;
    }
    const Schema::PacketInfo pi = m_builder.encoder().packet(m_packetBox->currentData().toString());
    if (!pi.ok) { return; }

    for (const Schema::FieldInfo &f : pi.header) {
        if (f.isPad) { continue; }
        m_fieldBox->addItem(QStringLiteral("%1 (%2 bit%3)")
                                .arg(f.name).arg(f.bits).arg(f.bits == 1 ? "" : "s"),
                            f.name);
        const int row = m_baseTable->rowCount();
        m_baseTable->insertRow(row);
        m_baseTable->setItem(row, 0, new QTableWidgetItem(f.name));
        auto *v = new QTableWidgetItem(QStringLiteral("0"));
        v->setFlags(v->flags() | Qt::ItemIsEditable);
        m_baseTable->setItem(row, 1, v);
    }
    if (!pi.unsupported.isEmpty()) {
        m_status->fail(tr("This packet uses grammar the encoder cannot emit: %1")
                              .arg(pi.unsupported.join(QStringLiteral(", "))));
    }
    // Wide enough for this packet's names and a 24-bit value beside them
    // (session 148): with Linux's fonts the names took the column and
    // clipped the values.
    {
        int names = m_baseTable->horizontalHeader()->sectionSizeHint(0);
        for (int r = 0; r < m_baseTable->rowCount(); ++r)
            if (const QTableWidgetItem *n = m_baseTable->item(r, 0))
                names = qMax(names, m_baseTable->fontMetrics().horizontalAdvance(n->text()) + 24);
        const int value = m_baseTable->fontMetrics().horizontalAdvance(QStringLiteral("16777215")) + 24;
        m_baseTable->setMinimumWidth(names + value + m_baseTable->verticalScrollBar()->sizeHint().width()
                                     + 2 * m_baseTable->frameWidth());
    }
    onFieldChanged();
}

void FieldSweepDialog::onFieldChanged()
{
    onPreview();
}

QHash<QString, qint64> FieldSweepDialog::baseValues() const
{
    QHash<QString, qint64> h;
    for (int r = 0; r < m_baseTable->rowCount(); ++r) {
        const QTableWidgetItem *n = m_baseTable->item(r, 0);
        const QTableWidgetItem *v = m_baseTable->item(r, 1);
        if (!n || !v) { continue; }
        const QString t = v->text().trimmed();
        bool ok = false;
        const qint64 x = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                             ? t.mid(2).toLongLong(&ok, 16) : t.toLongLong(&ok, 10);
        h.insert(n->text(), ok ? x : 0);
    }
    return h;
}

FieldSweep::Spec FieldSweepDialog::currentSpec(QString *err) const
{
    FieldSweep::Spec s;
    s.field = m_fieldBox->currentData().toString();
    s.mode  = FieldSweep::Mode(m_modeBox->currentData().toInt());

    if (m_builder.ready()) {
        const Schema::PacketInfo pi =
            m_builder.encoder().packet(m_packetBox->currentData().toString());
        for (const Schema::FieldInfo &f : pi.header) {
            if (f.name != s.field) { continue; }
            s.bits = f.bits;
            s.isSigned = f.isSigned;
            if (!f.enumName.isEmpty()) {
                s.enumChoices = m_builder.encoder().enumChoices(f.enumName);
            }
            break;
        }
    }
    bool ok = false;
    s.from = m_fromEdit->text().toLongLong(&ok); if (!ok) { s.from = 0; }
    s.to   = m_toEdit->text().toLongLong(&ok);   if (!ok) { s.to = 0; }
    s.step = m_stepEdit->text().toLongLong(&ok); if (!ok) { s.step = 1; }
    s.list = parseList(m_listEdit->text());
    if (err) { err->clear(); }
    return s;
}

void FieldSweepDialog::onPreview()
{
    QString err;
    const FieldSweep::Spec s = currentSpec(&err);
    const QVector<FieldSweep::Step> plan = FieldSweep::plan(s, &err);
    if (plan.isEmpty()) {
        m_status->fail(err.isEmpty() ? tr("nothing to send") : err);
        return;
    }
    QStringList vals;
    for (int i = 0; i < plan.size() && i < 12; ++i) {
        vals << QString::number(plan[i].value);
    }
    m_status->say(tr("%1 for %2: %3%4  —  about %5 s at the current gap")
                          .arg(countOf(plan.size(), "value", "values"), s.field)
                          .arg(vals.join(QStringLiteral(", ")))
                          .arg(plan.size() > 12 ? QStringLiteral(" …") : QString())
                          .arg((plan.size() * m_gapSpin->value()) / 1000));
}

void FieldSweepDialog::onStartStop()
{
    if (m_running) { setRunning(false); return; }

    QString err;
    const FieldSweep::Spec s = currentSpec(&err);
    m_plan = FieldSweep::plan(s, &err);
    if (m_plan.isEmpty()) {
        m_status->fail(err.isEmpty() ? tr("nothing to send") : err);
        return;
    }

    m_results->setRowCount(0);
    m_obs.clear();
    m_obs.resize(m_plan.size());
    for (const FieldSweep::Step &st : m_plan) {
        const int row = m_results->rowCount();
        m_results->insertRow(row);
        m_results->setItem(row, ColValue, new QTableWidgetItem(QString::number(st.value)));
        m_results->setItem(row, ColNote,  new QTableWidgetItem(st.note));
        m_results->setItem(row, ColSent,  new QTableWidgetItem(QString()));
        m_results->setItem(row, ColSeen,  new QTableWidgetItem(QString()));
        m_results->setItem(row, ColVerdict,
                           new QTableWidgetItem(FieldSweep::verdictName(
                               FieldSweep::Verdict::Pending)));
    }

    m_index = -1;
    setRunning(true);
    onStep();                       // first value goes out immediately
}

void FieldSweepDialog::setRunning(bool on)
{
    m_running = on;
    m_startBtn->setText(on ? tr("Stop") : tr("Start sweep"));
    if (!on) {
        m_stepTimer.stop();
        // Score whatever the last step saw rather than leaving it "waiting…"
        // for ever — a stopped run should read as finished, not as broken.
        if (m_index >= 0 && m_index < m_plan.size()) { closeStep(m_index); }
        m_index = -1;
    }
}

void FieldSweepDialog::onStep()
{
    // Close the previous step before moving on.
    if (m_index >= 0 && m_index < m_plan.size()) { closeStep(m_index); }

    ++m_index;
    if (m_index >= m_plan.size()) {
        m_status->ok(tr("Sweep finished — %1 sent.").arg(countOf(m_plan.size(), "value", "values")));
        setRunning(false);
        return;
    }

    QHash<QString, qint64> header = baseValues();
    header.insert(m_fieldBox->currentData().toString(), m_plan[m_index].value);

    const QByteArray key = QByteArray::fromHex(m_keyEdit->text().trimmed().toLatin1());
    const PacketBuilder::Result r =
        m_builder.build(m_packetBox->currentData().toString(), header, {}, key);
    if (!r.ok) {
        // Stop rather than skip: a gap in the results table that looks like a
        // silent target, but was actually a frame that never left, is the
        // worst thing this tool could report.
        m_status->fail(tr("Stopped at value %1 — build failed: %2")
                              .arg(m_plan[m_index].value).arg(r.error));
        setRunning(false);
        return;
    }

    if (!m_sender.sendOnce(r.frame, m_destEdit->text(), quint16(m_portSpin->value()))) {
        setRunning(false);
        return;
    }
    m_sinceSend.restart();
    m_results->item(m_index, ColSent)
        ->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    m_results->scrollToItem(m_results->item(m_index, ColValue));
    m_status->say(tr("Sent %1 of %2 — %3 = %4")
                          .arg(m_index + 1).arg(m_plan.size())
                          .arg(m_fieldBox->currentData().toString())
                          .arg(m_plan[m_index].value));

    m_stepTimer.setSingleShot(true);
    m_stepTimer.start(qMax(m_gapSpin->value(), m_windowSpin->value()));
}

void FieldSweepDialog::observe(const LogEntryPtr &entry)
{
    if (!m_running || m_index < 0 || m_index >= m_obs.size() || entry.isNull()) { return; }
    const qint64 dt = m_sinceSend.isValid() ? m_sinceSend.elapsed() : -1;
    if (dt < 0 || dt > m_windowSpin->value()) { return; }

    // The captype comes off the capture line's tag; entries that are not
    // capture lines still count as traffic, just without a type.
    QString captype;
    const QString t = entry->text.trimmed();
    if (t.startsWith(QLatin1Char('@'))) {
        const int sp = t.indexOf(QLatin1Char(' '));
        captype = t.mid(1, (sp < 0 ? t.size() : sp) - 1).section(QLatin1Char('_'), 0, 0);
    }

    FieldSweep::Observation o;
    o.msAfterSend = dt;
    o.captype = captype;
    // A fault is worth separating from any other reply: a target that answers
    // with a fault has understood the frame and rejected it, which is a
    // different fact from silence.
    o.isFault = o.captype.startsWith(QLatin1String("nmsflt"))
             || entry->severity == Severity::Error;
    m_obs[m_index].push_back(o);

    QStringList seen;
    for (const FieldSweep::Observation &x : m_obs[m_index]) {
        seen << (x.captype.isEmpty() ? QStringLiteral("(text)") : x.captype);
    }
    m_results->item(m_index, ColSeen)->setText(seen.join(QStringLiteral(", ")));
}

void FieldSweepDialog::closeStep(int row)
{
    if (row < 0 || row >= m_plan.size() || row >= m_obs.size()) { return; }
    QSet<QString> reply;
    for (const QString &t : m_replyEdit->text().split(
             QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        reply.insert(t.toLower());
    }
    const FieldSweep::Verdict v =
        FieldSweep::classify(m_obs[row], reply, m_windowSpin->value());
    m_results->item(row, ColVerdict)->setText(FieldSweep::verdictName(v));

    if (m_stopOnReply->isChecked() && v == FieldSweep::Verdict::Reply && m_running) {
        m_status->warn(tr("Stopped: value %1 drew a reply.").arg(m_plan[row].value));
        m_stepTimer.stop();
        m_running = false;
        m_startBtn->setText(tr("Start sweep"));
        m_index = -1;
    }
}

void FieldSweepDialog::seedFromBuffer(const QString &bufferText)
{
    if (!m_builder.ready()) { return; }
    QString hint;
    const QByteArray frame = PacketMakerDialog::parseBuffer(bufferText, &hint);
    if (frame.isEmpty()) {
        m_status->fail(tr("No bytes found in that buffer."));
        return;
    }
    if (!hint.isEmpty()) {
        const int idx = m_packetBox->findData(hint);
        if (idx >= 0) { m_packetBox->setCurrentIndex(idx); }
    }
    // The same body-locating step the Packet Maker does, and for a stronger
    // reason: this dialog TRANSMITS from the values it seeds. Handing the
    // whole captured datagram to parseBody() — which is what this did — reads
    // an arp/lsrp message header as PKT_TYPE and seeds the sweep from fields
    // that were never in the frame, without failing.
    const QString captype = m_packetBox->currentData().toString();
    const PacketMakerDialog::BufferSplit split =
        PacketMakerDialog::splitBuffer(m_builder.encoder(), captype, frame);
    if (split.body.isEmpty()) {
        m_status->warn(split.notes.join(QStringLiteral("  ·  ")));
        return;
    }

    const Schema::ParsedPacket pp =
        m_builder.encoder().parseBody(captype, split.body);
    if (!pp.ok) {
        m_status->fail(tr("Could not read that frame as %1: %2")
                              .arg(captype, pp.error));
        return;
    }
    for (int r = 0; r < m_baseTable->rowCount(); ++r) {
        const QTableWidgetItem *n = m_baseTable->item(r, 0);
        if (!n || !pp.header.contains(n->text())) { continue; }
        m_baseTable->item(r, 1)->setText(QString::number(pp.header.value(n->text())));
    }
    QStringList st = split.notes;
    st << tr("Base values seeded from the pasted frame.");
    st += pp.notes;
    m_status->ok(st.join(QStringLiteral("  ·  ")));
}

// =============================================================================
//  Escape and the close box while a sweep is running. See sendguard.h.
// =============================================================================
void FieldSweepDialog::reject()
{
    if (SendGuard::interceptReject(m_running, [this] {
            setRunning(false);
            m_status->warn(tr("Sweep stopped. Press Escape again to close."));
        })) {
        return;
    }
    QDialog::reject();
}

void FieldSweepDialog::closeEvent(QCloseEvent *event)
{
    if (m_running && !SendGuard::confirmClose(this, tr("The sweep"))) {
        event->ignore();
        return;
    }
    if (m_running) { setRunning(false); }
    QDialog::closeEvent(event);
}
