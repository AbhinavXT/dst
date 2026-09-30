#include "serialconsolewindow.h"

#include "messagedispatcher.h"
#include "settings.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QGridLayout>
#include <QIntValidator>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QString kGroup = QStringLiteral("serial/last");
constexpr int kMaxViewLines = 20000;
constexpr int kMaxHistory = 30;
}

quint16 SerialConsoleWindow::kvchForPort(const QString &portName)
{
    // Stable across runs and machines for the same name; never 0.
    quint32 h = 2166136261u;                         // FNV-1a
    for (const QChar c : portName.toUpper()) { h ^= c.unicode(); h *= 16777619u; }
    return quint16(1 + (h % 65000u));
}

SerialConsoleWindow::SerialConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Serial Port Terminal"));
    WindowGeometry::makeResizableWindow(this);
    resize(900, 640);
    WindowGeometry::restore(this, QStringLiteral("serialWindow"));

    m_link = new SerialLink(this);
    m_link->setIdleFlushMs(300);

    // ---- line settings ------------------------------------------------------------
    m_port = new QComboBox(this);
    m_port->setObjectName(QStringLiteral("serialPort"));
    m_port->setEditable(true);            // a path the enumerator does not list
    m_port->setMinimumWidth(240);          // "COM3 \u2014 USB Serial Port" fits
    auto *refresh = new QToolButton(this);
    refresh->setText(QStringLiteral("\u27F3"));
    refresh->setToolTip(tr("Look for ports again"));
    m_baud = new QComboBox(this);
    m_baud->setObjectName(QStringLiteral("serialBaud"));
    m_baud->setEditable(true);
    for (qint32 b : serialStandardBauds()) m_baud->addItem(QString::number(b), b);
    m_baud->lineEdit()->setValidator(new QIntValidator(50, 16000000, m_baud));
    m_dataBits = new QComboBox(this);
    for (int d : { 5, 6, 7, 8 }) m_dataBits->addItem(QString::number(d), d);
    m_parity = new QComboBox(this);
    m_parity->addItem(tr("None"), int(QSerialPort::NoParity));
    m_parity->addItem(tr("Even"), int(QSerialPort::EvenParity));
    m_parity->addItem(tr("Odd"), int(QSerialPort::OddParity));
    m_parity->addItem(tr("Mark"), int(QSerialPort::MarkParity));
    m_parity->addItem(tr("Space"), int(QSerialPort::SpaceParity));
    m_stopBits = new QComboBox(this);
    m_stopBits->addItem(QStringLiteral("1"), int(QSerialPort::OneStop));
    m_stopBits->addItem(QStringLiteral("1.5"), int(QSerialPort::OneAndHalfStop));
    m_stopBits->addItem(QStringLiteral("2"), int(QSerialPort::TwoStop));
    m_flow = new QComboBox(this);
    m_flow->addItem(tr("None"), int(QSerialPort::NoFlowControl));
    m_flow->addItem(tr("RTS/CTS"), int(QSerialPort::HardwareControl));
    m_flow->addItem(tr("XON/XOFF"), int(QSerialPort::SoftwareControl));
    m_open = new QPushButton(tr("Open"), this);
    m_open->setObjectName(QStringLiteral("serialOpen"));
    m_dtr = new QCheckBox(tr("DTR"), this);
    m_rts = new QCheckBox(tr("RTS"), this);
    m_dtr->setToolTip(tr("Data Terminal Ready line (some adapters reset the card on it)"));
    m_rts->setToolTip(tr("Request To Send line (driven by the driver under RTS/CTS flow control)"));

    auto *cfg = new QHBoxLayout;
    auto label = [this](const QString &t) { return new QLabel(t, this); };
    cfg->addWidget(label(tr("Port")));      cfg->addWidget(m_port); cfg->addWidget(refresh);
    cfg->addWidget(label(tr("Baud")));      cfg->addWidget(m_baud);
    cfg->addWidget(label(tr("Data")));      cfg->addWidget(m_dataBits);
    cfg->addWidget(label(tr("Parity")));    cfg->addWidget(m_parity);
    cfg->addWidget(label(tr("Stop")));      cfg->addWidget(m_stopBits);
    cfg->addWidget(label(tr("Flow")));      cfg->addWidget(m_flow);
    cfg->addStretch(1);
    cfg->addWidget(m_dtr); cfg->addWidget(m_rts);
    cfg->addWidget(m_open);

    // ---- receive ------------------------------------------------------------------
    m_hexView = new QCheckBox(tr("Hex"), this);
    m_hexView->setObjectName(QStringLiteral("serialHexView"));
    m_timestamps = new QCheckBox(tr("Timestamps"), this);
    m_timestamps->setObjectName(QStringLiteral("serialTimestamps"));
    m_echo = new QCheckBox(tr("Show sent"), this);
    m_hold = new QCheckBox(tr("Hold"), this);
    m_hold->setObjectName(QStringLiteral("serialHold"));
    m_hold->setToolTip(tr("Freeze the view to read it; what arrives meanwhile is kept and shown on release "
                          "(and is still logged and fed to the console)"));
    auto *clear = new QPushButton(tr("Clear"), this);
    m_logFile = new QCheckBox(tr("Log to file…"), this);
    m_logFile->setObjectName(QStringLiteral("serialLogFile"));
    m_feed = new QCheckBox(tr("Feed console"), this);
    m_feed->setObjectName(QStringLiteral("serialFeed"));
    m_feed->setToolTip(tr("Also put every received line into the console, in a tab named after this port: "
                          "decoded, coloured, searchable and recorded like UDP traffic. "
                          "For the IOA's input, output and analog logs."));
    auto *rxRow = new QHBoxLayout;
    rxRow->addWidget(m_hexView); rxRow->addWidget(m_timestamps); rxRow->addWidget(m_echo);
    rxRow->addWidget(m_hold); rxRow->addWidget(clear);
    rxRow->addStretch(1);
    rxRow->addWidget(m_feed); rxRow->addWidget(m_logFile);

    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("serialView"));
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(kMaxViewLines);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(UiStyle::monoFont());       // the console's monospace, at its text size

    // ---- send ---------------------------------------------------------------------
    m_send = new QComboBox(this);
    m_send->setObjectName(QStringLiteral("serialSend"));
    m_send->setEditable(true);
    m_send->setInsertPolicy(QComboBox::NoInsert);
    m_send->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_send->lineEdit()->setPlaceholderText(tr("text (\\r \\n \\xHH allowed), or hex bytes with Hex ticked"));
    m_sendHex = new QCheckBox(tr("Hex"), this);
    m_sendHex->setObjectName(QStringLiteral("serialSendHex"));
    m_ending = new QComboBox(this);
    m_ending->setObjectName(QStringLiteral("serialEnding"));
    m_ending->addItem(tr("No line end"), QByteArray());
    m_ending->addItem(QStringLiteral("CR"), QByteArray("\r"));
    m_ending->addItem(QStringLiteral("LF"), QByteArray("\n"));
    m_ending->addItem(QStringLiteral("CR+LF"), QByteArray("\r\n"));
    m_sendBtn = new QPushButton(tr("Send"), this);
    m_repeat = new QCheckBox(tr("Every"), this);
    m_repeatMs = new QSpinBox(this);
    m_repeatMs->setRange(10, 3600000);
    m_repeatMs->setSuffix(tr(" ms"));
    m_repeatMs->setValue(1000);
    auto *sendFile = new QPushButton(tr("Send file…"), this);
    auto *txRow = new QHBoxLayout;
    txRow->addWidget(m_send, 1); txRow->addWidget(m_sendHex); txRow->addWidget(m_ending);
    txRow->addWidget(m_repeat); txRow->addWidget(m_repeatMs);
    txRow->addWidget(m_sendBtn); txRow->addWidget(sendFile);
    m_repeatTimer = new QTimer(this);

    // ---- status -------------------------------------------------------------------
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("serialState"));
    m_counts = new QLabel(this);
    auto *resetCounts = new QToolButton(this);
    resetCounts->setText(tr("Reset"));
    auto *stRow = new QHBoxLayout;
    stRow->addWidget(m_state, 1); stRow->addWidget(m_counts); stRow->addWidget(resetCounts);

    auto *root = new QVBoxLayout(this);
    root->addLayout(cfg);
    root->addLayout(rxRow);
    root->addWidget(m_view, 1);
    root->addLayout(txRow);
    root->addLayout(stRow);

    // ---- remembered ---------------------------------------------------------------
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    refreshPorts();
    setConfigToUi(SerialConfig::load(s, kGroup));
    m_timestamps->setChecked(s.value(QStringLiteral("serial/timestamps"), true).toBool());
    m_hexView->setChecked(s.value(QStringLiteral("serial/hexView"), false).toBool());
    m_echo->setChecked(s.value(QStringLiteral("serial/echo"), true).toBool());
    m_feed->setChecked(s.value(QStringLiteral("serial/feed"), true).toBool());
    m_sendHex->setChecked(s.value(QStringLiteral("serial/sendHex"), false).toBool());
    m_ending->setCurrentIndex(qBound(0, s.value(QStringLiteral("serial/ending"), 3).toInt(), 3));
    m_send->addItems(s.value(QStringLiteral("serial/history")).toStringList());
    m_send->setCurrentIndex(-1);

    // ---- wiring -------------------------------------------------------------------
    connect(refresh, &QToolButton::clicked, this, &SerialConsoleWindow::refreshPorts);
    connect(m_open, &QPushButton::clicked, this, [this]() { if (m_link->isOpen()) closePort(); else openPort(); });
    connect(m_dtr, &QCheckBox::toggled, this, [this](bool on) { m_link->setDtr(on); });
    connect(m_rts, &QCheckBox::toggled, this, [this](bool on) { m_link->setRts(on); });
    connect(clear, &QPushButton::clicked, m_view, &QPlainTextEdit::clear);
    connect(m_hold, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            // Release: what arrived while held goes into the view, in order.
            const QStringList held = m_heldLines;
            m_heldLines.clear();
            m_held = 0;
            for (const QString &t : held) m_view->appendPlainText(t);
            m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
        }
        updateState();
    });
    connect(m_logFile, &QCheckBox::toggled, this, &SerialConsoleWindow::setLogging);
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() { sendText(m_send->currentText()); });
    connect(m_send->lineEdit(), &QLineEdit::returnPressed, this, [this]() { sendText(m_send->currentText()); });
    connect(m_repeat, &QCheckBox::toggled, this, [this](bool on) {
        if (on) { m_repeatTimer->start(m_repeatMs->value()); } else { m_repeatTimer->stop(); }
    });
    connect(m_repeatMs, qOverload<int>(&QSpinBox::valueChanged), this, [this](int ms) {
        if (m_repeatTimer->isActive()) m_repeatTimer->start(ms);
    });
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (!m_link->isOpen() || !sendText(m_send->currentText())) { m_repeat->setChecked(false); }
    });
    connect(sendFile, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Send file"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { m_state->setText(tr("Cannot read %1").arg(path)); return; }
        const qint64 n = m_link->write(f.readAll());
        m_state->setText(tr("Sent %1 bytes from %2").arg(n).arg(QFileInfo(path).fileName()));
    });
    connect(resetCounts, &QToolButton::clicked, this, [this]() { m_link->resetCounters(); m_fedLines = 0; updateState(); });

    connect(m_link, &SerialLink::lineReceived, this, &SerialConsoleWindow::onLine);
    connect(m_link, &SerialLink::bytesReceived, this, &SerialConsoleWindow::onBytes);
    connect(m_link, &SerialLink::bytesWritten, this, &SerialConsoleWindow::onWritten);
    connect(m_link, &SerialLink::closed, this, &SerialConsoleWindow::updateState);
    connect(m_link, &SerialLink::errorOccurred, this, [this](const QString &t) {
        m_state->setText(tr("\u2715 %1").arg(t));
        m_state->setStyleSheet(UiColor::errorStyle());
    });

    m_countTimer = new QTimer(this);
    m_countTimer->setInterval(500);
    connect(m_countTimer, &QTimer::timeout, this, [this]() {
        m_counts->setText(tr("RX %1 · TX %2 · to console %3 lines")
                              .arg(m_link->rxBytes()).arg(m_link->txBytes()).arg(m_fedLines));
    });
    m_countTimer->start();
    updateState();
}

SerialConsoleWindow::~SerialConsoleWindow()
{
    setLogging(false);
}

void SerialConsoleWindow::closeEvent(QCloseEvent *e)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    configFromUi().save(s, kGroup);
    s.setValue(QStringLiteral("serial/timestamps"), m_timestamps->isChecked());
    s.setValue(QStringLiteral("serial/hexView"), m_hexView->isChecked());
    s.setValue(QStringLiteral("serial/echo"), m_echo->isChecked());
    s.setValue(QStringLiteral("serial/feed"), m_feed->isChecked());
    s.setValue(QStringLiteral("serial/sendHex"), m_sendHex->isChecked());
    s.setValue(QStringLiteral("serial/ending"), m_ending->currentIndex());
    QStringList hist;
    for (int i = 0; i < m_send->count(); ++i) hist << m_send->itemText(i);
    s.setValue(QStringLiteral("serial/history"), hist);
    WindowGeometry::save(this, QStringLiteral("serialWindow"));
    m_repeatTimer->stop();
    m_link->close();
    QWidget::closeEvent(e);
}

// ---- settings <-> UI ----------------------------------------------------------------

SerialConfig SerialConsoleWindow::configFromUi() const
{
    SerialConfig c;
    // "COM3 — USB Serial Port": the name is the first word.
    c.portName = m_port->currentText().section(QLatin1Char(' '), 0, 0).trimmed();
    const int idx = m_port->findText(m_port->currentText());
    if (idx >= 0 && !m_port->itemData(idx).toString().isEmpty()) c.portName = m_port->itemData(idx).toString();
    c.baud = m_baud->currentText().toInt();
    if (c.baud <= 0) c.baud = 115200;
    c.dataBits = QSerialPort::DataBits(m_dataBits->currentData().toInt());
    c.parity = QSerialPort::Parity(m_parity->currentData().toInt());
    c.stopBits = QSerialPort::StopBits(m_stopBits->currentData().toInt());
    c.flow = QSerialPort::FlowControl(m_flow->currentData().toInt());
    return c;
}

void SerialConsoleWindow::setConfigToUi(const SerialConfig &c)
{
    if (!c.portName.isEmpty()) {
        const int i = m_port->findData(c.portName);
        if (i >= 0) m_port->setCurrentIndex(i);
        else m_port->setEditText(c.portName);
    }
    m_baud->setEditText(QString::number(c.baud));
    m_dataBits->setCurrentIndex(qMax(0, m_dataBits->findData(int(c.dataBits))));
    m_parity->setCurrentIndex(qMax(0, m_parity->findData(int(c.parity))));
    m_stopBits->setCurrentIndex(qMax(0, m_stopBits->findData(int(c.stopBits))));
    m_flow->setCurrentIndex(qMax(0, m_flow->findData(int(c.flow))));
}

void SerialConsoleWindow::refreshPorts()
{
    const QString was = m_port->currentText();
    m_port->clear();
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts()) {
        QString text = p.portName();
        if (!p.description().isEmpty()) text += QStringLiteral(" \u2014 ") + p.description();
        m_port->addItem(text, p.portName());
        m_port->setItemData(m_port->count() - 1, p.systemLocation(), Qt::ToolTipRole);
    }
    if (!was.isEmpty()) m_port->setEditText(was);
}

// ---- open / close -------------------------------------------------------------------

bool SerialConsoleWindow::openPort()
{
    const SerialConfig c = configFromUi();
    if (c.portName.isEmpty()) {
        m_state->setText(tr("Choose a port first."));
        return false;
    }
    if (!m_link->open(c)) { updateState(); m_state->setText(tr("\u2715 %1").arg(m_link->errorText())); return false; }
    m_link->setDtr(m_dtr->isChecked());
    m_link->setRts(m_rts->isChecked());
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    c.save(s, kGroup);
    appendView(tr("── opened %1 %2 at %3 ──").arg(c.portName, c.summary(), stamp(QDateTime::currentMSecsSinceEpoch())));
    updateState();
    return true;
}

void SerialConsoleWindow::closePort()
{
    m_repeat->setChecked(false);
    const bool was = m_link->isOpen();
    m_link->close();
    if (was) appendView(tr("── closed at %1 ──").arg(stamp(QDateTime::currentMSecsSinceEpoch())));
    updateState();
}

void SerialConsoleWindow::updateState()
{
    const bool open = m_link->isOpen();
    m_open->setText(open ? tr("Close") : tr("Open"));
    for (QWidget *w : { static_cast<QWidget *>(m_port), static_cast<QWidget *>(m_baud),
                        static_cast<QWidget *>(m_dataBits), static_cast<QWidget *>(m_parity),
                        static_cast<QWidget *>(m_stopBits), static_cast<QWidget *>(m_flow) })
        w->setEnabled(!open);
    m_sendBtn->setEnabled(open);
    if (open) {
        const SerialConfig &c = m_link->config();
        setWindowTitle(tr("Serial — %1 %2").arg(c.portName, c.summary()));
        QString t = tr("\u25CF %1 open, %2").arg(c.portName, c.summary());
        if (m_feed->isChecked() && m_dispatcher) t += tr(" · feeding tab \u201CSerial %1\u201D").arg(c.portName.section(QLatin1Char('/'), -1));
        if (m_hold->isChecked() && m_held) t += m_held == 1 ? tr(" · holding 1 line") : tr(" · holding %1 lines").arg(m_held);
        m_state->setText(t);
        m_state->setStyleSheet(UiColor::okStyle());
    } else {
        setWindowTitle(tr("Serial Port Terminal"));
        m_state->setText(tr("Closed"));
        m_state->setStyleSheet(UiColor::mutedStyle());
    }
}

// ---- receive ------------------------------------------------------------------------

QString SerialConsoleWindow::stamp(qint64 ms) const
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

void SerialConsoleWindow::appendView(const QString &text)
{
    if (m_log && m_log->isOpen()) { m_log->write(text.toUtf8()); m_log->write("\n"); m_log->flush(); }
    if (m_hold->isChecked()) {
        // Kept, not dropped: shown when Hold is released. Bounded like the view.
        ++m_held;
        m_heldLines.append(text);
        if (m_heldLines.size() > kMaxViewLines) m_heldLines.removeFirst();
        return;
    }
    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;
    m_view->appendPlainText(text);
    if (atEnd) bar->setValue(bar->maximum());
}

void SerialConsoleWindow::onLine(const QByteArray &line, qint64 ms)
{
    // Into the console first: that path must not depend on view options.
    if (m_feed->isChecked() && m_dispatcher && !line.trimmed().isEmpty()) {
        const QString port = m_link->config().portName;
        // Tab named by the port's short name ("COM3", "ttyUSB0"); keyed by the
        // full name, so two paths never share a tab.
        m_dispatcher->ingestLocal(kSerialSourceId, kvchForPort(port), line, ms,
                                  tr("Serial %1").arg(port.section(QLatin1Char('/'), -1)));
        ++m_fedLines;
    }
    if (m_hexView->isChecked()) return;             // the hex view shows chunks
    const QString text = QString::fromUtf8(line);
    appendView(m_timestamps->isChecked() ? stamp(ms) + QStringLiteral("  ") + text : text);
    if (m_hold->isChecked()) updateState();
}

void SerialConsoleWindow::onBytes(const QByteArray &bytes, qint64 ms)
{
    if (!m_hexView->isChecked()) return;
    const QString hex = serialToHex(bytes);
    appendView(m_timestamps->isChecked() ? stamp(ms) + QStringLiteral("  ") + hex : hex);
}

void SerialConsoleWindow::onWritten(const QByteArray &bytes, qint64 ms)
{
    if (!m_echo->isChecked()) return;
    QString shown = m_hexView->isChecked() ? serialToHex(bytes) : QString::fromUtf8(bytes).trimmed();
    shown = QStringLiteral("TX> ") + shown;
    appendView(m_timestamps->isChecked() ? stamp(ms) + QStringLiteral("  ") + shown : shown);
}

// ---- send ---------------------------------------------------------------------------

bool SerialConsoleWindow::sendText(const QString &text)
{
    if (!m_link->isOpen()) { m_state->setText(tr("Open the port first.")); return false; }
    QByteArray bytes;
    if (m_sendHex->isChecked()) {
        bool ok = false;
        bytes = serialParseHex(text, &ok);
        if (!ok) {
            // Refuse rather than send something other than what was typed.
            m_state->setText(tr("\u2715 Not hex bytes: %1").arg(text));
            m_state->setStyleSheet(UiColor::errorStyle());
            return false;
        }
    } else {
        bytes = serialUnescape(text);
    }
    bytes += m_ending->currentData().toByteArray();
    if (bytes.isEmpty()) return false;
    if (m_link->write(bytes) <= 0) return false;
    if (!text.isEmpty()) {
        const int at = m_send->findText(text);
        if (at != 0) {
            if (at > 0) m_send->removeItem(at);
            m_send->insertItem(0, text);
            while (m_send->count() > kMaxHistory) m_send->removeItem(m_send->count() - 1);
            m_send->setCurrentIndex(0);
        }
    }
    return true;
}

// ---- log file -----------------------------------------------------------------------

void SerialConsoleWindow::setLogging(bool on)
{
    if (!on) {
        if (m_log) { m_log->close(); delete m_log; m_log = nullptr; }
        if (m_logFile && m_logFile->isChecked()) { m_logFile->blockSignals(true); m_logFile->setChecked(false); m_logFile->blockSignals(false); }
        if (m_logFile) m_logFile->setText(tr("Log to file…"));
        return;
    }
    const QString def = QStringLiteral("serial_%1_%2.log")
                            .arg(configFromUi().portName.section(QLatin1Char('/'), -1),
                                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Log serial data to"), def, tr("Log (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) { setLogging(false); return; }
    m_log = new QFile(path);
    if (!m_log->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_state->setText(tr("\u2715 Cannot write %1").arg(path));
        setLogging(false);
        return;
    }
    m_logFile->setText(tr("Logging to %1").arg(QFileInfo(path).fileName()));
}

// ---- tests --------------------------------------------------------------------------

QString SerialConsoleWindow::receivedText() const { return m_view->toPlainText(); }
QString SerialConsoleWindow::statusText() const { return m_state->text(); }
QString SerialConsoleWindow::tabKey() const
{
    return QStringLiteral("%1_%2").arg(int(kSerialSourceId)).arg(int(kvchForPort(m_link->config().portName)));
}
