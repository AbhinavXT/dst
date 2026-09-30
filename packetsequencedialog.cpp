#include "packetsequencedialog.h"
#include "windowgeometry.h"
#include "statusline.h"
#include <QCloseEvent>
#include "sendguard.h"
#include "uicolors.h"

#include "messageheader.h"
#include "settings.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kColPath     = 0;
constexpr int kColType     = 1;
constexpr int kColRepeats  = 2;
constexpr int kColInterval = 3;
constexpr int kColDelay    = 4;
}  // namespace

PacketSequenceDialog::PacketSequenceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Packet sequence"));
    WindowGeometry::makeResizableWindow(this);
    setMinimumWidth(760);

    auto *root = new QVBoxLayout(this);

    auto *warn = new QLabel(
        tr("<b>This transmits.</b> Every step is built and self-verified first, "
           "and a step that fails to build stops the run rather than being "
           "skipped."),
        this);
    warn->setWordWrap(true);
    // The caution banner follows the palette now. It was a fixed dark amber,
    // which read as a dark block dropped into a light window.
    auto paintWarn = [warn] {
        warn->setStyleSheet(QStringLiteral(
            "QLabel { background:%1; color:%2; border:1px solid %3;"
            " border-radius:4px; padding:8px; }")
            .arg(UiColor::bannerBg().name(), UiColor::bannerFg().name(),
                 UiColor::bannerBorder().name()));
    };
    paintWarn();
    UiColor::onThemeChange(this, paintWarn);
    root->addWidget(warn);

    auto *stepsBox = new QGroupBox(tr("Steps"), this);
    auto *stepsLay = new QVBoxLayout(stepsBox);

    m_table = new QTableWidget(0, 5, stepsBox);
    m_table->setHorizontalHeaderLabels({ tr("Preset"), tr("Packet"), tr("Repeats"),
                                         tr("Interval (ms)"), tr("Delay after (ms)") });
    m_table->horizontalHeader()->setSectionResizeMode(kColPath, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(160);
    stepsLay->addWidget(m_table);

    auto *stepBtns = new QHBoxLayout;
    auto *addBtn  = new QPushButton(tr("Add preset…"), stepsBox);
    auto *rmBtn   = new QPushButton(tr("Remove"), stepsBox);
    auto *upBtn   = new QPushButton(tr("Move up"), stepsBox);
    auto *downBtn = new QPushButton(tr("Move down"), stepsBox);
    stepBtns->addWidget(addBtn);
    stepBtns->addWidget(rmBtn);
    stepBtns->addWidget(upBtn);
    stepBtns->addWidget(downBtn);
    stepBtns->addStretch(1);
    auto *saveBtn = new QPushButton(tr("Save sequence…"), stepsBox);
    auto *loadBtn = new QPushButton(tr("Load sequence…"), stepsBox);
    stepBtns->addWidget(saveBtn);
    stepBtns->addWidget(loadBtn);
    stepsLay->addLayout(stepBtns);

    root->addWidget(stepsBox, 1);

    auto *cfg = new QFormLayout;
    m_dest = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_dest->setToolTip(tr("Overrides the destination stored in each preset, so "
                          "one sequence can be pointed at a different target "
                          "without editing every step."));
    cfg->addRow(tr("Send to"), m_dest);

    m_key = new QLineEdit(this);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("32 hex characters, if the packets are MAC'd"));
    m_key->setToolTip(tr("Presets never store the session key. It is supplied "
                         "here, per run."));
    cfg->addRow(tr("Session key"), m_key);

    m_loop = new QCheckBox(tr("Loop the sequence until stopped"), this);
    cfg->addRow(QString(), m_loop);
    root->addLayout(cfg);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    root->addWidget(m_progress);

    m_status = new StatusLine(this);
    m_status->say(tr("Idle."));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *btns = new QHBoxLayout;
    m_runBtn = new QPushButton(tr("Run"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btns->addWidget(m_runBtn);
    btns->addStretch(1);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);

    if (!m_builder.ready()) {
        m_status->setText(tr("Schema not loaded — sequences cannot be built."));
        m_runBtn->setEnabled(false);
    }

    connect(addBtn,  &QPushButton::clicked, this, &PacketSequenceDialog::onAddStep);
    connect(rmBtn,   &QPushButton::clicked, this, &PacketSequenceDialog::onRemoveStep);
    connect(upBtn,   &QPushButton::clicked, this, &PacketSequenceDialog::onMoveUp);
    connect(downBtn, &QPushButton::clicked, this, &PacketSequenceDialog::onMoveDown);
    connect(saveBtn, &QPushButton::clicked, this, &PacketSequenceDialog::onSaveSequence);
    connect(loadBtn, &QPushButton::clicked, this, &PacketSequenceDialog::onLoadSequence);
    connect(m_runBtn, &QPushButton::clicked, this, &PacketSequenceDialog::onRunStop);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_timer,  &QTimer::timeout, this, &PacketSequenceDialog::onTick);
    connect(&m_sender, &UdpSender::error, this, &PacketSequenceDialog::onSenderError);
}

PacketSequenceDialog::~PacketSequenceDialog()
{
    m_sender.stop();
}

QByteArray PacketSequenceDialog::sessionKey() const
{
    const QString hex = m_key->text().trimmed();
    if (hex.isEmpty()) { return {}; }
    return QByteArray::fromHex(hex.toLatin1());
}

void PacketSequenceDialog::addStepRow(const Step &s)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *path = new QTableWidgetItem(QFileInfo(s.path).fileName());
    path->setData(Qt::UserRole, s.path);
    path->setToolTip(s.path);
    path->setFlags(path->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, kColPath, path);

    auto *type = new QTableWidgetItem(s.captype);
    type->setFlags(type->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, kColType, type);

    m_table->setItem(row, kColRepeats,  new QTableWidgetItem(QString::number(s.repeats)));
    m_table->setItem(row, kColInterval, new QTableWidgetItem(QString::number(s.intervalMs)));
    m_table->setItem(row, kColDelay,    new QTableWidgetItem(QString::number(s.delayMs)));
}

void PacketSequenceDialog::onAddStep()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add presets to the sequence"), QString(),
        tr("Packet preset (*.packet.json *.json);;All files (*)"));

    QStringList problems;
    for (const QString &p : paths) {
        PacketPreset pre;
        QString err;
        if (!PacketPreset::load(p, &pre, &err)) {
            problems << tr("%1: %2").arg(QFileInfo(p).fileName(), err);
            continue;
        }
        Step s;
        s.path       = p;
        s.captype    = pre.captype;
        s.intervalMs = pre.intervalMs > 0 ? pre.intervalMs : 200;
        addStepRow(s);
    }
    if (!problems.isEmpty()) { m_status->setText(problems.join(QStringLiteral("  •  "))); }
}

void PacketSequenceDialog::onRemoveStep()
{
    const int row = m_table->currentRow();
    if (row >= 0) { m_table->removeRow(row); }
}

void PacketSequenceDialog::onMoveUp()
{
    const int row = m_table->currentRow();
    if (row <= 0) { return; }
    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem *a = m_table->takeItem(row, c);
        QTableWidgetItem *b = m_table->takeItem(row - 1, c);
        m_table->setItem(row - 1, c, a);
        m_table->setItem(row, c, b);
    }
    m_table->setCurrentCell(row - 1, 0);
}

void PacketSequenceDialog::onMoveDown()
{
    const int row = m_table->currentRow();
    if (row < 0 || row + 1 >= m_table->rowCount()) { return; }
    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem *a = m_table->takeItem(row, c);
        QTableWidgetItem *b = m_table->takeItem(row + 1, c);
        m_table->setItem(row + 1, c, a);
        m_table->setItem(row, c, b);
    }
    m_table->setCurrentCell(row + 1, 0);
}

QVector<PacketSequenceDialog::Step> PacketSequenceDialog::readSteps() const
{
    QVector<Step> out;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *pi = m_table->item(row, kColPath);
        if (!pi) { continue; }

        Step s;
        s.path = pi->data(Qt::UserRole).toString();

        auto num = [this, row](int col, int dflt) {
            QTableWidgetItem *it = m_table->item(row, col);
            if (!it) { return dflt; }
            bool ok = false;
            const int v = it->text().trimmed().toInt(&ok);
            return ok ? v : dflt;
        };
        s.repeats    = qMax(1, num(kColRepeats, 1));
        s.intervalMs = qMax(1, num(kColInterval, 200));
        s.delayMs    = qMax(0, num(kColDelay, 0));

        // Reloaded at Run, not cached from when the row was added: the point
        // of referencing files is that fixing a preset fixes every sequence
        // that uses it, which only holds if the file is read at run time.
        QString err;
        s.loaded  = PacketPreset::load(s.path, &s.preset, &err);
        s.captype = s.loaded ? s.preset.captype : QString();
        out.push_back(s);
    }
    return out;
}

void PacketSequenceDialog::setRunning(bool on)
{
    m_running = on;
    m_runBtn->setText(on ? tr("Stop") : tr("Run"));
    m_table->setEnabled(!on);
    m_dest->setEnabled(!on);
    m_key->setEnabled(!on);
    m_loop->setEnabled(!on);
}

void PacketSequenceDialog::onRunStop()
{
    if (m_running) {
        m_timer->stop();
        finish(tr("Stopped."), false);
        return;
    }

    m_run = readSteps();
    if (m_run.isEmpty()) { m_status->setText(tr("No steps.")); return; }

    QStringList bad;
    for (const Step &s : m_run) {
        if (!s.loaded) { bad << QFileInfo(s.path).fileName(); }
    }
    if (!bad.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("These presets could not be read:\n%1")
                                 .arg(bad.join(QLatin1Char('\n'))));
        return;
    }

    m_totalSends = 0;
    QStringList summary;
    for (const Step &s : m_run) {
        m_totalSends += s.repeats;
        summary << tr("%1 × %2 @ %3 ms, then wait %4 ms")
                       .arg(s.captype).arg(s.repeats).arg(s.intervalMs).arg(s.delayMs);
    }

    if (QMessageBox::question(
            this, windowTitle(),
            tr("Run this sequence against %1?\n\n%2%3")
                .arg(m_dest->text(), summary.join(QLatin1Char('\n')),
                     m_loop->isChecked() ? tr("\n\nLooping until stopped.") : QString()))
        != QMessageBox::Yes) {
        return;
    }

    m_step       = 0;
    m_sendInStep = 0;
    m_sentSoFar  = 0;
    setRunning(true);
    m_status->setText(tr("Running…"));
    advance();
}

void PacketSequenceDialog::advance()
{
    if (!m_running) { return; }

    if (m_step >= m_run.size()) {
        if (m_loop->isChecked()) {
            m_step       = 0;
            m_sendInStep = 0;
            m_sentSoFar  = 0;
        } else {
            finish(tr("Sequence complete — %1 datagram(s) sent.").arg(m_sentSoFar), true);
            return;
        }
    }

    const Step &s = m_run[m_step];

    // The preset's own destination is overridden by the dialog's, so one
    // sequence can be aimed somewhere else without editing every step.
    const QString dest = m_dest->text().trimmed();
    const int     port = s.preset.port > 0 ? s.preset.port : Settings::udpPort();

    const PacketBuilder::Result r = s.preset.build(m_builder, m_sendInStep, sessionKey());
    if (!r.ok) {
        finish(tr("Step %1 (%2) failed to build at send %3: %4")
                   .arg(m_step + 1).arg(s.captype).arg(m_sendInStep).arg(r.error),
               false);
        return;
    }

    UdpSender::PrefixFn prefix;
    if (MessageHeader::applies(s.captype)) {
        // Refuse rather than send a header without the fields the preset
        // asked for: buildWithExtras() returns nothing for invalid extras,
        // and an empty prefix would put the packet on the wire bare.
        QString exErr;
        if (!MessageHeader::validateExtras(s.preset.extras, &exErr)) {
            finish(tr("Step %1 (%2) not sent: %3").arg(m_step + 1).arg(s.captype).arg(exErr),
                   false);
            return;
        }
        const quint8 src = quint8(s.preset.msgSrc);
        const quint8 dst = quint8(s.preset.msgDest);
        const int    len = r.frame.size();
        const int    seq0 = s.preset.msgSeq;
        const int    idx  = m_sentSoFar;
        prefix = [src, dst, len, seq0, idx, captype = s.captype,
                  extras = s.preset.extras](int) {
            return MessageHeader::buildWithExtras(captype, len,
                                                  quint16((seq0 + idx) & 0xFFFF),
                                                  src, dst, extras);
        };
    }

    m_sender.sendOnce(r.frame, dest, quint16(port), prefix);
    ++m_sentSoFar;
    ++m_sendInStep;

    if (m_totalSends > 0) {
        m_progress->setValue(int(qBound(0, m_sentSoFar * 1000 / m_totalSends, 1000)));
    }
    m_status->setText(tr("Step %1/%2 — %3, send %4/%5  (%6 total)")
                          .arg(m_step + 1).arg(m_run.size()).arg(s.captype)
                          .arg(m_sendInStep).arg(s.repeats).arg(m_sentSoFar));

    int waitMs = s.intervalMs;
    if (m_sendInStep >= s.repeats) {
        // Last datagram of the step: the gap before the next step is the
        // step's delay, not its interval.
        waitMs = s.delayMs;
        ++m_step;
        m_sendInStep = 0;
    }
    m_timer->start(qMax(1, waitMs));
}

void PacketSequenceDialog::onTick() { advance(); }

void PacketSequenceDialog::finish(const QString &why, bool ok)
{
    m_timer->stop();
    setRunning(false);
    m_status->setText(why);
    m_status->setStyleSheet(ok ? QString() : UiColor::errorStyle());
    if (ok) { m_progress->setValue(1000); }
}

void PacketSequenceDialog::onSenderError(const QString &what)
{
    if (!m_running) { return; }
    finish(tr("Send failed: %1").arg(what), false);
}

void PacketSequenceDialog::onSaveSequence()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save sequence"), QStringLiteral("sequence.seq.json"),
        tr("Packet sequence (*.seq.json *.json);;All files (*)"));
    if (path.isEmpty()) { return; }

    QJsonArray steps;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *pi = m_table->item(row, kColPath);
        if (!pi) { continue; }
        QJsonObject o;
        o["preset"]      = pi->data(Qt::UserRole).toString();
        o["repeats"]     = m_table->item(row, kColRepeats)  ? m_table->item(row, kColRepeats)->text().toInt()  : 1;
        o["interval_ms"] = m_table->item(row, kColInterval) ? m_table->item(row, kColInterval)->text().toInt() : 200;
        o["delay_ms"]    = m_table->item(row, kColDelay)    ? m_table->item(row, kColDelay)->text().toInt()    : 0;
        steps.append(o);
    }

    QJsonObject root;
    root["format"]  = QStringLiteral("dlconsole.sequence");
    root["version"] = 1;
    root["dest"]    = m_dest->text();
    root["loop"]    = m_loop->isChecked();
    root["steps"]   = steps;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1").arg(path));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    m_status->setText(tr("Saved %1 (preset paths are referenced, not copied; "
                         "the session key is not stored).").arg(path));
}

void PacketSequenceDialog::onLoadSequence()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load sequence"), QString(),
        tr("Packet sequence (*.seq.json *.json);;All files (*)"));
    if (path.isEmpty()) { return; }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read %1").arg(path));
        return;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()
        || doc.object().value("format").toString() != QLatin1String("dlconsole.sequence")) {
        QMessageBox::warning(this, windowTitle(), tr("%1 is not a sequence file").arg(path));
        return;
    }

    const QJsonObject root = doc.object();
    m_dest->setText(root.value("dest").toString(m_dest->text()));
    m_loop->setChecked(root.value("loop").toBool(false));

    m_table->setRowCount(0);
    QStringList missing;
    for (const QJsonValue &sv : root.value("steps").toArray()) {
        const QJsonObject o = sv.toObject();
        Step s;
        s.path       = o.value("preset").toString();
        s.repeats    = o.value("repeats").toInt(1);
        s.intervalMs = o.value("interval_ms").toInt(200);
        s.delayMs    = o.value("delay_ms").toInt(0);

        PacketPreset pre;
        QString err;
        if (PacketPreset::load(s.path, &pre, &err)) { s.captype = pre.captype; }
        else                                        { missing << QFileInfo(s.path).fileName(); }
        addStepRow(s);
    }

    // Missing presets are shown as rows with no packet type rather than
    // dropped, so the sequence can be repaired instead of silently shrinking.
    if (missing.isEmpty()) {
        m_status->ok(tr("Loaded %1.").arg(path));
    } else {
        // Sticky, because a sequence with holes in it will not do what the
        // operator thinks it will, and a transient message about that would
        // be gone before they pressed Run.
        m_status->warn(tr("Loaded %1, but these presets are missing: %2")
                           .arg(path, missing.join(QStringLiteral(", "))));
    }
}

// =============================================================================
//  Escape and the close box while a sequence is running. See sendguard.h.
//
//  This dialog is the reason the rule exists: a sequence stopped halfway
//  leaves the equipment having seen a partial run, and the window that says
//  which step it reached is exactly what the operator needs next.
// =============================================================================
void PacketSequenceDialog::reject()
{
    if (SendGuard::interceptReject(m_running, [this] {
            setRunning(false);
            m_status->setText(tr("Sequence stopped. Press Escape again to close."));
            m_status->setStyleSheet(UiColor::warningStyle());
        })) {
        return;
    }
    QDialog::reject();
}

void PacketSequenceDialog::closeEvent(QCloseEvent *event)
{
    if (m_running && !SendGuard::confirmClose(this, tr("The sequence"))) {
        event->ignore();
        return;
    }
    if (m_running) { setRunning(false); }
    QDialog::closeEvent(event);
}
