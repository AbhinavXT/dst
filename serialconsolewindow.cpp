#include "serialconsolewindow.h"

#include "messagedispatcher.h"
#include "serialmanager.h"
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
#include <QInputDialog>
#include <QIntValidator>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBlock>
#include "querylineedit.h"
#include <QDialog>
#include <QHeaderView>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QProgressBar>
#include <QRadioButton>
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
// Before session 102 every window saved here; still read when a port has
// no settings of its own yet.
const QString kLegacyGroup = QStringLiteral("serial/last");
constexpr int kMaxViewLines = 20000;
constexpr int kMaxHistory = 30;
}

quint16 SerialConsoleWindow::kvchForPort(const QString &portName)
{
    return SerialManager::kvchForPort(portName);
}

SerialConsoleWindow::SerialConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_mgr(new SerialManager(dispatcher, this))
    , m_ownsMgr(true)
{
    build();
}

SerialConsoleWindow::SerialConsoleWindow(SerialManager *manager, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_mgr(manager)
{
    build();
}

void SerialConsoleWindow::build()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Serial Port Terminal"));
    WindowGeometry::makeResizableWindow(this);
    resize(900, 640);
    WindowGeometry::restore(this, QStringLiteral("serialWindow"));

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
    m_lowLatency = new QCheckBox(tr("Low latency"), this);
    m_lowLatency->setObjectName(QStringLiteral("serialLowLatency"));
    m_lowLatency->setChecked(true);
    m_lowLatency->setToolTip(tr("Ask for a 1 ms FTDI latency timer (the default 16 ms adds up to 16 ms of "
                                "timestamp jitter). Linux can set it from here; on Windows it is set in "
                                "Device Manager, and the terminal says so when the port opens."));
    m_open = new QPushButton(tr("Open"), this);
    m_open->setObjectName(QStringLiteral("serialOpen"));
    UiStyle::makePrimary(m_open);
    m_dtr = new QCheckBox(tr("DTR"), this);
    m_rts = new QCheckBox(tr("RTS"), this);
    m_dtr->setToolTip(tr("Data Terminal Ready line (some adapters reset the card on it)"));
    m_rts->setToolTip(tr("Request To Send line (driven by the driver under RTS/CTS flow control)"));

    // ---- profiles -----------------------------------------------------------------
    m_profile = new QComboBox(this);
    m_profile->setObjectName(QStringLiteral("serialProfile"));
    m_profile->setMinimumWidth(180);
    m_profile->setToolTip(tr("A named port setup: port, line settings, Feed console and auto-open. "
                             "Choosing one fills the window in; Tools \u25B8 Serial Profiles opens them, "
                             "or all of them at once."));
    m_autoOpen = new QCheckBox(tr("Open at start"), this);
    m_autoOpen->setObjectName(QStringLiteral("serialAutoOpen"));
    m_autoOpen->setToolTip(tr("Open this profile's port when DLConsole starts (saved with the profile)"));
    auto *saveProf = new QPushButton(tr("Save as profile\u2026"), this);
    saveProf->setObjectName(QStringLiteral("serialSaveProfile"));
    auto *delProf = new QToolButton(this);
    delProf->setText(tr("Delete"));
    delProf->setObjectName(QStringLiteral("serialDeleteProfile"));
    auto *profRow = new QHBoxLayout;
    profRow->addWidget(new QLabel(tr("Profile"), this));
    profRow->addWidget(m_profile);
    profRow->addWidget(m_autoOpen);
    profRow->addWidget(saveProf);
    profRow->addWidget(delProf);
    profRow->addStretch(1);
    connect(m_profile, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        selectProfile(m_profile->currentData().toString());
    });
    connect(saveProf, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save serial profile"),
                                                   tr("Name (e.g. IOA Input):"), QLineEdit::Normal,
                                                   currentProfile(), &ok);
        if (ok && !name.trimmed().isEmpty()) saveProfile(name);
    });
    connect(delProf, &QToolButton::clicked, this, [this]() {
        const QString name = currentProfile();
        if (name.isEmpty()) return;
        if (QMessageBox::question(this, tr("Delete serial profile"),
                                  tr("Delete the profile \u201C%1\u201D? Its port is not closed.").arg(name))
            == QMessageBox::Yes) {
            deleteProfile(name);
        }
    });

    auto *cfg = new QHBoxLayout;
    auto label = [this](const QString &t) { return new QLabel(t, this); };
    cfg->addWidget(label(tr("Port")));      cfg->addWidget(m_port); cfg->addWidget(refresh);
    m_findBaud = new QToolButton(this);
    m_findBaud->setObjectName(QStringLiteral("serialFindBaud"));
    m_findBaud->setText(tr("Find"));
    m_findBaud->setToolTip(tr("Find the baud rate: try each standard rate for a second and keep the one "
                              "where the card's lines decode as @ frames. Closes the port while it looks, "
                              "and opens it at the rate it finds."));
    cfg->addWidget(label(tr("Baud")));      cfg->addWidget(m_baud); cfg->addWidget(m_findBaud);
    cfg->addStretch(1);
    cfg->addWidget(m_open);
    // The line settings on a row of their own (session 153): with them, the
    // port row alone held the window at 1480 px, over a 1366-px laptop.
    auto *line = new QHBoxLayout;
    line->addWidget(label(tr("Data")));      line->addWidget(m_dataBits);
    line->addWidget(label(tr("Parity")));    line->addWidget(m_parity);
    line->addWidget(label(tr("Stop")));      line->addWidget(m_stopBits);
    line->addWidget(label(tr("Flow")));      line->addWidget(m_flow);
    line->addWidget(m_lowLatency);
    line->addWidget(m_dtr); line->addWidget(m_rts);
    line->addStretch(1);

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

    // ---- show only / highlight (session 110) -------------------------------------
    m_filterEdit = new QueryLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("serialFilter"));
    m_filterEdit->setPlaceholderText(tr("@dop2   msg:/FAIL/   field:SIG_OV=1   dir:out"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setToolTip(tr("Show only the lines that match, in the console's own query language "
                                "(the same as a tab's filter in Query mode). Changing it re-filters what is "
                                "already on screen. The log file and the console still get every line."));
    m_highlightEdit = new QueryLineEdit(this);
    m_highlightEdit->setObjectName(QStringLiteral("serialHighlight"));
    m_highlightEdit->setPlaceholderText(tr("/LINK.*FAIL/   dir:out   time:14:02..14:05"));
    m_highlightEdit->setClearButtonEnabled(true);
    m_highlightEdit->setToolTip(tr("Mark the lines that match: a \u25B6 at the start and a tinted background."));
    m_queryError = new QLabel(this);
    m_queryError->setObjectName(QStringLiteral("serialQueryError"));
    m_queryError->setStyleSheet(UiColor::errorStyle());
    m_queryError->hide();
    auto *qRow = new QHBoxLayout;
    qRow->addWidget(new QLabel(tr("Show only"), this));
    qRow->addWidget(m_filterEdit, 1);
    qRow->addWidget(new QLabel(tr("Highlight"), this));
    qRow->addWidget(m_highlightEdit, 1);
    qRow->addWidget(m_queryError);
    m_queryDebounce = new QTimer(this);
    m_queryDebounce->setSingleShot(true);
    m_queryDebounce->setInterval(200);
    connect(m_queryDebounce, &QTimer::timeout, this, &SerialConsoleWindow::applyViewQueries);
    connect(m_filterEdit, &QLineEdit::textChanged, m_queryDebounce, [this]() { m_queryDebounce->start(); });
    connect(m_highlightEdit, &QLineEdit::textChanged, m_queryDebounce, [this]() { m_queryDebounce->start(); });

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
    m_sender = new SerialFileSender(this);
    m_autoBaud = new SerialAutoBaud(this);
    m_sendProgress = new QProgressBar(this);
    m_sendProgress->setObjectName(QStringLiteral("serialSendProgress"));
    m_sendProgress->setMaximumWidth(220);
    m_sendProgress->setFormat(tr("%p% sent"));
    m_sendProgress->hide();
    m_sendStop = new QPushButton(tr("Stop"), this);
    m_sendStop->setObjectName(QStringLiteral("serialSendStop"));
    m_sendStop->setToolTip(tr("Stop sending the file"));
    m_sendStop->hide();
    txRow->addWidget(m_sendProgress); txRow->addWidget(m_sendStop);
    m_repeatTimer = new QTimer(this);

    // ---- status -------------------------------------------------------------------
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("serialState"));
    UiStyle::makeChip(m_state, UiStyle::Tone::Neutral);   // session 122: the state is a chip
    m_counts = new QLabel(this);
    UiStyle::makeMono(m_counts);
    m_counts->setStyleSheet(UiColor::mutedStyle());
    m_health = new QLabel(this);
    m_health->setObjectName(QStringLiteral("serialHealth"));
    m_health->setToolTip(tr("Line health since the port opened. \u201CDecode\u201D is the share of lines that are "
                            "well-formed @ capture lines of a known type: a wrong baud rate or parity shows up "
                            "at once as nearly 0%. Qt 5.15 does not report framing or parity errors separately; "
                            "\u201Cdriver errors\u201D counts every error the driver did report."));
    auto *resetCounts = new QToolButton(this);
    resetCounts->setText(tr("Reset"));
    m_feedAnyway = new QPushButton(tr("Feed anyway"), this);
    m_feedAnyway->setObjectName(QStringLiteral("serialFeedAnyway"));
    m_feedAnyway->setToolTip(tr("This port came back sending other packet types than before, so its "
                                "lines are kept out of the console. Feed them anyway: it is the right card."));
    m_feedAnyway->hide();
    auto *stRow = new QHBoxLayout;
    // The state chip hugs its text; health, counters and Reset to the right.
    stRow->addWidget(m_state);
    stRow->addWidget(m_feedAnyway);
    stRow->addStretch(1);
    stRow->addWidget(m_health);
    stRow->addWidget(m_counts);
    stRow->addWidget(resetCounts);

    auto *root = new QVBoxLayout(this);
    // Session 122: the connection — profile, port and line settings, and the
    // one primary action, Open — as one strip at the top.
    auto *conn = new QWidget(this);
    conn->setObjectName(QStringLiteral("serialConnection"));
    UiStyle::makePanel(conn);
    auto *connCol = new QVBoxLayout(conn);
    connCol->setContentsMargins(UiStyle::space(3), UiStyle::space(2), UiStyle::space(3), UiStyle::space(2));
    connCol->setSpacing(UiStyle::space(2));
    connCol->addLayout(profRow);
    connCol->addLayout(cfg);
    connCol->addLayout(line);
    root->addWidget(conn);
    root->addLayout(rxRow);
    root->addLayout(qRow);
    root->addWidget(m_view, 1);
    root->addLayout(txRow);
    // ---- macros (session 109) ----------------------------------------------------
    m_macroBar = new QWidget(this);
    m_macroBar->setObjectName(QStringLiteral("serialMacros"));
    auto *macroRow = new QHBoxLayout(m_macroBar);
    macroRow->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_macroBar);
    m_confirmMacro = [this](const SerialMacro &m) {
        const QString port = m_link ? m_link->config().portName : QString();
        return QMessageBox::question(this, tr("Send %1").arg(m.label),
                                     tr("Send \u201C%1\u201D to %2?\n\n%3\n\nThe card acts on it.")
                                         .arg(m.label, port, m.text),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               == QMessageBox::Yes;
    };
    root->addLayout(stRow);

    // ---- remembered ---------------------------------------------------------------
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    refreshPorts();
    refreshProfiles();
    loadMacros();
    {
        // A port already running in the background comes first: reopening
        // the terminal shows what is capturing. Then the last port used,
        // with ITS settings.
        const QStringList running = m_mgr->openPorts();
        const QString last = running.isEmpty() ? s.value(QStringLiteral("serial/lastPort")).toString()
                                               : running.first();
        SerialConfig c;
        bool feed = true;
        if (!running.isEmpty()) {
            c = m_mgr->link(last)->config();
            feed = m_mgr->feeds(last);
        } else if (last.isEmpty() || !SerialManager::loadPortSettings(s, last, &c, &feed)) {
            c = SerialConfig::load(s, kLegacyGroup);
            feed = s.value(QStringLiteral("serial/feed"), true).toBool();
        }
        setConfigToUi(c);
        m_feed->setChecked(feed);
    }
    m_timestamps->setChecked(s.value(QStringLiteral("serial/timestamps"), true).toBool());
    m_hexView->setChecked(s.value(QStringLiteral("serial/hexView"), false).toBool());
    m_echo->setChecked(s.value(QStringLiteral("serial/echo"), true).toBool());
    m_sendHex->setChecked(s.value(QStringLiteral("serial/sendHex"), false).toBool());
    m_ending->setCurrentIndex(qBound(0, s.value(QStringLiteral("serial/ending"), 3).toInt(), 3));
    m_send->addItems(s.value(QStringLiteral("serial/history")).toStringList());
    m_send->setCurrentIndex(-1);

    // ---- wiring -------------------------------------------------------------------
    connect(refresh, &QToolButton::clicked, this, &SerialConsoleWindow::refreshPorts);
    connect(m_open, &QPushButton::clicked, this, [this]() {
        const bool waiting = m_link && m_mgr->isReconnecting(m_link->config().portName);
        if (isOpen() || waiting || m_openPending) closePort(); else openPortAsync();
    });
    connect(m_feedAnyway, &QPushButton::clicked, this, [this]() {
        if (!m_link) return;
        m_mgr->feedAnyway(m_link->config().portName);
        appendView(tr("── fed anyway at %1 ──").arg(stamp(QDateTime::currentMSecsSinceEpoch())));
        updateState();
    });
    // Session 156: an Open that did not wait finishes here.
    connect(m_mgr, &SerialManager::openFinished, this, [this](const QString &port, bool ok, const QString &error) {
        if (!m_openPending || port.compare(m_pendingOpen.portName, Qt::CaseInsensitive) != 0) return;
        m_openPending = false;
        if (ok) { afterOpened(m_pendingOpen); return; }
        updateState();
        m_state->setText(tr("\u2715 %1").arg(error));
        UiStyle::setTone(m_state, UiStyle::Tone::Fail);
    });
    connect(m_mgr, &SerialManager::portsChanged, this, [this]() { updateState(); });
    connect(m_mgr, &SerialManager::portSuspect, this, [this](const QString &port, const QString &why) {
        if (!m_link || m_mgr->link(port) != m_link) return;
        appendView(tr("── %1 ──").arg(why));
        updateState();
    });
    // Session 106: the port this window shows was lost, or came back.
    connect(m_mgr, &SerialManager::portLost, this, [this](const QString &port, const QString &why) {
        if (!m_link || m_mgr->link(port) != m_link) return;
        appendView(tr("── lost %1 at %2: %3; waiting for it to come back ──")
                       .arg(port, stamp(QDateTime::currentMSecsSinceEpoch()), why));
        updateState();
    });
    connect(m_mgr, &SerialManager::portReconnected, this, [this](const QString &port, qint64 gapMs) {
        if (!m_link || m_mgr->link(port) != m_link) return;
        // Possibly under a new name (Windows renumbers COM ports).
        m_attaching = true;
        setConfigToUi(m_link->config());
        m_attaching = false;
        appendView(tr("── reconnected %1 at %2, gap %3 s ──")
                       .arg(port, stamp(QDateTime::currentMSecsSinceEpoch()))
                       .arg(gapMs / 1000.0, 0, 'f', 1));
        updateState();
    });
    connect(m_dtr, &QCheckBox::toggled, this, [this](bool on) { if (m_link) m_link->setDtr(on); });
    connect(m_rts, &QCheckBox::toggled, this, [this](bool on) { if (m_link) m_link->setRts(on); });
    connect(m_port, &QComboBox::currentTextChanged, this, &SerialConsoleWindow::onPortChosen);
    connect(m_feed, &QCheckBox::toggled, this, [this](bool on) {
        if (m_attaching || !m_link) return;
        m_mgr->setFeed(configFromUi().portName, on);
        updateState();
    });
    connect(clear, &QPushButton::clicked, this, [this]() { m_view->clear(); m_buffer.clear(); });
    connect(m_hold, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            // Release: what arrived while held goes into the view, in order.
            const QVector<ViewLine> held = m_heldLines;
            m_heldLines.clear();
            m_held = 0;
            for (const ViewLine &l : held) showLine(l);
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
        if (!isOpen() || !sendText(m_send->currentText())) { m_repeat->setChecked(false); }
    });
    connect(sendFile, &QPushButton::clicked, this, [this]() {
        if (!isOpen()) { m_state->setText(tr("Open the port first.")); return; }
        if (m_sender->isRunning()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Send file"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { m_state->setText(tr("Cannot read %1").arg(path)); return; }
        const QByteArray data = f.readAll();
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        SerialSendOptions o = SerialSendOptions::load(s);
        if (!askSendOptions(&o, QFileInfo(path).fileName(), data.size())) return;
        o.save(s);
        sendFileData(data, QFileInfo(path).fileName(), o);
    });
    connect(m_sendStop, &QPushButton::clicked, m_sender, &SerialFileSender::stop);
    connect(m_findBaud, &QToolButton::clicked, this, [this]() {
        if (m_autoBaud->isRunning()) m_autoBaud->stop(); else findBaud();
    });
    connect(m_autoBaud, &SerialAutoBaud::trying, this, [this](qint32 rate, int index, int count) {
        m_state->setText(tr("Finding the baud rate: trying %1 (%2 of %3)\u2026").arg(rate).arg(index + 1).arg(count));
        UiStyle::setTone(m_state, UiStyle::Tone::Neutral);
    });
    connect(m_autoBaud, &SerialAutoBaud::finished, this, [this](bool ok, qint32 rate, const QString &message) {
        m_findBaud->setText(tr("Find"));
        m_open->setEnabled(true);
        appendView(tr("\u2500\u2500 baud: %1 \u2500\u2500").arg(message));
        if (!ok) {
            updateState();
            m_state->setText(tr("\u2715 Baud not found: %1").arg(message));
            UiStyle::setTone(m_state, UiStyle::Tone::Fail);
            return;
        }
        m_baud->setEditText(QString::number(rate));
        openPortAsync();
    });
    connect(m_sender, &SerialFileSender::progress, this, [this](qint64 sent, qint64 total) {
        // Bytes, scaled to int range for the bar.
        m_sendProgress->setMaximum(1000);
        m_sendProgress->setValue(total ? int(sent * 1000 / total) : 0);
        m_sendProgress->setToolTip(tr("%1 of %2 bytes").arg(sent).arg(total));
    });
    connect(m_sender, &SerialFileSender::finished, this, [this](bool ok, const QString &message) {
        m_sendProgress->hide();
        m_sendStop->hide();
        appendView(tr("── %1: %2 ──").arg(m_sendingName, message));
        m_state->setText(ok ? tr("Sent %1: %2").arg(m_sendingName, message)
                            : tr("\u2715 %1: %2").arg(m_sendingName, message));
        UiStyle::setTone(m_state, ok ? UiStyle::Tone::Ok : UiStyle::Tone::Fail);
    });
    connect(resetCounts, &QToolButton::clicked, this, [this]() {
        if (!m_link) return;
        m_link->resetCounters();
        m_mgr->resetFedLines(m_link->config().portName);
        updateState();
    });

    // A partial hex row is shown once the line goes quiet: 60 ms, checked
    // every 30. Short, so a few bytes and then silence still appear at once;
    // long against a 16-byte row's time on the wire at 9600 baud (17 ms).
    m_hexFlush = new QTimer(this);
    m_hexFlush->setInterval(30);
    connect(m_hexFlush, &QTimer::timeout, this, [this]() {
        if (!m_hex.hasPartial()) { m_hexFlush->stop(); return; }
        if (QDateTime::currentMSecsSinceEpoch() - m_hex.lastByteMs() >= 60) flushHexRow();
    });
    connect(m_hexView, &QCheckBox::toggled, this, [this](bool on) {
        // Hex rows are not lines: Show only / Highlight apply to the text view.
        m_filterEdit->setEnabled(!on);
        m_highlightEdit->setEnabled(!on);
        flushHexRow();
        m_hex.reset();                             // offsets count from the switch
    });
    connect(clear, &QPushButton::clicked, this, [this]() { m_hex.reset(); });

    m_countTimer = new QTimer(this);
    m_countTimer->setInterval(500);
    connect(m_countTimer, &QTimer::timeout, this, [this]() {
        if (!m_link) { m_counts->clear(); m_health->clear(); return; }
        const QString port = m_link->config().portName;
        if (isOpen()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            m_health->setText(m_mgr->healthText(port, now));
            m_health->setStyleSheet(m_mgr->health(port, now).failing() ? UiColor::warningStyle()
                                                                      : UiColor::mutedStyle());
        } else {
            m_health->clear();
        }
        m_counts->setText(tr("RX %1 · TX %2 · to console %3 lines")
                              .arg(m_link->rxBytes()).arg(m_link->txBytes())
                              .arg(m_mgr->fedLines(m_link->config().portName)));
    });
    m_countTimer->start();
    m_filterEdit->setEnabled(!m_hexView->isChecked());
    m_highlightEdit->setEnabled(!m_hexView->isChecked());
    onPortChosen();
}

void SerialConsoleWindow::attach(SerialLink *link)
{
    if (link == m_link) return;
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
    m_link = link;
    if (m_link) {
        connect(m_link, &SerialLink::lineReceived, this, &SerialConsoleWindow::onLine);
        connect(m_link, &SerialLink::bytesReceived, this, &SerialConsoleWindow::onBytes);
        connect(m_link, &SerialLink::bytesWritten, this, &SerialConsoleWindow::onWritten);
        connect(m_link, &SerialLink::opened, this, &SerialConsoleWindow::updateState);
        connect(m_link, &SerialLink::closed, this, &SerialConsoleWindow::updateState);
        connect(m_link, &SerialLink::errorOccurred, this, [this](const QString &t) {
            m_state->setText(tr("\u2715 %1").arg(t));
            UiStyle::setTone(m_state, UiStyle::Tone::Fail);
        });
    }
}

void SerialConsoleWindow::onPortChosen()
{
    if (m_attaching) return;
    // Choosing another port while one is open leaves that one running in
    // the manager: this window just looks at a different port.
    const QString port = configFromUi().portName;
    // Only a port the manager already knows is attached: the box is
    // editable, and making a link per keystroke ("C", "CO", "COM") would
    // litter the manager. openPort() makes it.
    SerialLink *l = port.isEmpty() ? nullptr : m_mgr->link(port);
    attach(l);
    if (l && l->isOpen()) {
        // Running in the background: show it as it is.
        m_attaching = true;
        setConfigToUi(l->config());
        m_feed->setChecked(m_mgr->feeds(port));
        m_attaching = false;
    }
    updateState();
}

void SerialConsoleWindow::showPort(const QString &portName)
{
    m_attaching = true;
    const int i = m_port->findData(portName);
    if (i >= 0) m_port->setCurrentIndex(i);
    else m_port->setEditText(portName);
    m_attaching = false;
    SerialLink *l = m_mgr->linkFor(portName);
    attach(l);
    if (l->isOpen()) {
        m_attaching = true;
        setConfigToUi(l->config());
        m_feed->setChecked(m_mgr->feeds(portName));
        m_attaching = false;
    }
    updateState();
}

void SerialConsoleWindow::saveSettings()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const SerialConfig c = (isOpen() ? m_link->config() : configFromUi());
    SerialManager::savePortSettings(s, c, m_feed->isChecked());
}

SerialConsoleWindow::~SerialConsoleWindow()
{
    setLogging(false);
}

void SerialConsoleWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("serial/timestamps"), m_timestamps->isChecked());
    s.setValue(QStringLiteral("serial/hexView"), m_hexView->isChecked());
    s.setValue(QStringLiteral("serial/echo"), m_echo->isChecked());
    s.setValue(QStringLiteral("serial/sendHex"), m_sendHex->isChecked());
    s.setValue(QStringLiteral("serial/ending"), m_ending->currentIndex());
    QStringList hist;
    for (int i = 0; i < m_send->count(); ++i) hist << m_send->itemText(i);
    s.setValue(QStringLiteral("serial/history"), hist);
    WindowGeometry::save(this, QStringLiteral("serialWindow"));
    m_repeatTimer->stop();
    // The standalone form owns its ports; MainWindow's manager keeps them
    // running and feeding the console after this viewer goes.
    if (m_ownsMgr) m_mgr->closeAll();
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
    c.lowLatency = m_lowLatency->isChecked();
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
    m_lowLatency->setChecked(c.lowLatency);
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

bool SerialConsoleWindow::prepareOpen(const SerialConfig &c)
{
    if (c.portName.isEmpty()) {
        m_state->setText(tr("Choose a port first."));
        return false;
    }
    attach(m_mgr->linkFor(c.portName));
    m_mgr->setFeed(c.portName, m_feed->isChecked());
    // Opened from a profile (its port still the one chosen): carry its name
    // onto the chip and the console tab. Otherwise no label.
    {
        QSettings ps(Settings::iniPath(), QSettings::IniFormat);
        const QVector<SerialProfile> all = SerialProfile::loadAll(ps);
        const int i = SerialProfile::indexOf(all, currentProfile());
        const bool fromProfile = i >= 0
            && all.at(i).config.portName.compare(c.portName, Qt::CaseInsensitive) == 0;
        m_mgr->setLabel(c.portName, fromProfile ? all.at(i).name : QString());
    }
    return true;
}

bool SerialConsoleWindow::openPortAsync()
{
    const SerialConfig c = configFromUi();
    if (!prepareOpen(c)) return false;
    m_pendingOpen = c;
    m_openPending = true;
    m_mgr->openAsync(c);              // openFinished() -> afterOpened()
    updateState();
    return true;
}

bool SerialConsoleWindow::openPort()
{
    const SerialConfig c = configFromUi();
    if (!prepareOpen(c)) return false;
    m_openPending = false;
    if (!m_mgr->open(c)) {
        updateState();
        m_state->setText(tr("\u2715 %1").arg(m_link->errorText()));
        // After updateState(), which set "Closed" in the neutral tone: the
        // error was shown uncoloured (session 122).
        UiStyle::setTone(m_state, UiStyle::Tone::Fail);
        return false;
    }
    afterOpened(c);
    return true;
}

void SerialConsoleWindow::afterOpened(const SerialConfig &c)
{
    m_link->setDtr(m_dtr->isChecked());
    m_link->setRts(m_rts->isChecked());
    m_hex.reset();                                 // hex offsets count from the open
    saveSettings();
    appendView(tr("── opened %1 %2 at %3 ──").arg(c.portName, c.summary(), stamp(QDateTime::currentMSecsSinceEpoch())));
    if (!m_link->latencyNote().isEmpty()) appendView(tr("── %1 ──").arg(m_link->latencyNote()));
    updateState();
}

void SerialConsoleWindow::closePort()
{
    m_openPending = false;            // a Cancel while opening
    m_repeat->setChecked(false);
    const bool was = isOpen();
    // Through the manager: it also stops a wait for a lost adapter.
    if (m_link) m_mgr->close(m_link->config().portName);
    if (was) appendView(tr("── closed at %1 ──").arg(stamp(QDateTime::currentMSecsSinceEpoch())));
    updateState();
}

void SerialConsoleWindow::updateState()
{
    const bool open = isOpen();
    m_open->setText(open ? tr("Close") : tr("Open"));
    // The port box stays live: choosing another port views it, and leaves
    // this one running.
    for (QWidget *w : { static_cast<QWidget *>(m_baud),
                        static_cast<QWidget *>(m_dataBits), static_cast<QWidget *>(m_parity),
                        static_cast<QWidget *>(m_stopBits), static_cast<QWidget *>(m_flow),
                        static_cast<QWidget *>(m_lowLatency) })
        w->setEnabled(!open);
    m_sendBtn->setEnabled(open);
    const QString shownPort = m_link ? m_link->config().portName : QString();
    const bool suspect = open && m_mgr->isSuspect(shownPort);
    m_feedAnyway->setVisible(suspect);
    if (suspect) {
        setWindowTitle(tr("Serial — %1 (held: a different card?)").arg(shownPort));
        m_state->setText(tr("\u26A0 %1").arg(m_mgr->suspectText(shownPort)));
        UiStyle::setTone(m_state, UiStyle::Tone::Warn);
    } else if (open && m_mgr->isVerifying(shownPort)) {
        setWindowTitle(tr("Serial — %1 (checking)").arg(shownPort));
        m_state->setText(tr("\u25CC %1 reconnected \u2014 checking it is the same card; its lines wait "
                            "until one of the types this tab had arrives").arg(shownPort));
        UiStyle::setTone(m_state, UiStyle::Tone::Warn);
    } else if (open) {
        const SerialConfig &c = m_link->config();
        setWindowTitle(tr("Serial — %1 %2").arg(c.portName, c.summary()));
        QString t = tr("\u25CF %1 open, %2").arg(c.portName, c.summary());
        if (m_mgr->feeds(c.portName) && m_mgr->dispatcher())
            t += tr(" · feeding tab \u201C%1\u201D").arg(m_mgr->titleFor(c.portName));
        if (!m_ownsMgr) t += tr(" · keeps running when this window closes");
        if (m_hold->isChecked() && m_held) t += m_held == 1 ? tr(" · holding 1 line") : tr(" · holding %1 lines").arg(m_held);
        m_state->setText(t);
        UiStyle::setTone(m_state, UiStyle::Tone::Ok);
    } else if (m_link && m_mgr->isReconnecting(m_link->config().portName)) {
        const SerialConfig &c = m_link->config();
        m_open->setText(tr("Stop waiting"));
        setWindowTitle(tr("Serial — %1 (lost)").arg(c.portName));
        m_state->setText(tr("\u25CC %1 lost \u2014 reconnecting when the adapter comes back "
                            "(found by its USB serial number or USB socket, under any port name)").arg(c.portName));
        UiStyle::setTone(m_state, UiStyle::Tone::Warn);
    } else if (m_openPending || (m_link && m_link->isOpening())) {
        const QString port = m_openPending ? m_pendingOpen.portName : shownPort;
        m_open->setText(tr("Cancel"));
        setWindowTitle(tr("Serial — %1 (opening)").arg(port));
        m_state->setText(tr("\u25CC Opening %1\u2026").arg(port));
        UiStyle::setTone(m_state, UiStyle::Tone::Neutral);
    } else {
        setWindowTitle(tr("Serial Port Terminal"));
        m_state->setText(tr("Closed"));
        UiStyle::setTone(m_state, UiStyle::Tone::Neutral);
    }
}

// ---- receive ------------------------------------------------------------------------

QString SerialConsoleWindow::stamp(qint64 ms) const
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

void SerialConsoleWindow::appendView(const QString &text, const QByteArray &raw, qint64 ms, LogDirection dir)
{
    // The log file gets everything, whatever the view is showing.
    if (m_log && m_log->isOpen()) { m_log->write(text.toUtf8()); m_log->write("\n"); m_log->flush(); }
    const ViewLine l{ text, raw, ms, dir };
    m_buffer.append(l);
    if (m_buffer.size() > kMaxViewLines) m_buffer.removeFirst();
    if (!passesFilter(l)) return;
    if (m_hold->isChecked()) {
        // Kept, not dropped: shown when Hold is released. Bounded like the view.
        ++m_held;
        m_heldLines.append(l);
        if (m_heldLines.size() > kMaxViewLines) m_heldLines.removeFirst();
        return;
    }
    showLine(l);
}

LogEntry SerialConsoleWindow::entryFor(const ViewLine &l) const
{
    // A LogEntry like the one the console tab holds for this line, so a
    // query means the same here as there: the raw line as text (field:
    // decodes it), the port's tab key for src:, the direction for dir:.
    LogEntry e;
    e.text = QString::fromUtf8(l.raw.constData(), l.raw.size());
    e.epochMs = l.ms;
    e.direction = l.dir;
    e.header.source_id = SerialManager::kSerialSourceId;
    e.header.kvchId = SerialManager::kvchForPort(m_link ? m_link->config().portName : configFromUi().portName);
    e.header.message_len = quint16(qMin(l.raw.size(), 0xFFFF));
    e.cacheDerived();
    return e;
}

bool SerialConsoleWindow::passesFilter(const ViewLine &l) const
{
    if (l.raw.isEmpty() || m_filter.isEmpty() || !m_filter.isValid()) return true;
    return m_filter.match(entryFor(l));
}

void SerialConsoleWindow::showLine(const ViewLine &l)
{
    const bool marked = !l.raw.isEmpty() && !m_highlight.isEmpty() && m_highlight.isValid()
                        && m_highlight.match(entryFor(l));
    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;
    // A glyph as well as the tint: not colour alone.
    m_view->appendPlainText(marked ? QStringLiteral("\u25B6 ") + l.text : l.text);
    // Set on every line, marked or not: a new block inherits the previous
    // block's format, so after one highlighted line every line after it
    // came out tinted too.
    QTextCursor c(m_view->document()->lastBlock());
    QTextBlockFormat f;
    if (marked) {
        QColor tint = palette().highlight().color();
        tint.setAlpha(90);
        f.setBackground(tint);
        f.setProperty(QTextFormat::UserProperty, true);
    }
    c.setBlockFormat(f);
    if (atEnd) bar->setValue(bar->maximum());
}

void SerialConsoleWindow::applyViewQueries()
{
    // An unreadable query filters nothing and says why — never "matches
    // nothing", which would look like a silent card.
    m_filter.parse(m_filterEdit->text());
    m_highlight.parse(m_highlightEdit->text());
    QString err;
    if (!m_filter.isValid()) {
        err = tr("Show only: %1").arg(m_filter.errorString());
        m_filterEdit->setQueryError(m_filter.errorString(), m_filter.errorOffset());
    } else {
        m_filterEdit->clearQueryError();
    }
    if (!m_highlight.isValid()) {
        if (!err.isEmpty()) err += QStringLiteral(" \u00B7 ");
        err += tr("Highlight: %1").arg(m_highlight.errorString());
        m_highlightEdit->setQueryError(m_highlight.errorString(), m_highlight.errorOffset());
    } else {
        m_highlightEdit->clearQueryError();
    }
    m_queryError->setText(err);
    m_queryError->setVisible(!err.isEmpty());

    // Rebuild what is on screen from what was received.
    m_view->clear();
    m_heldLines.clear();
    m_held = 0;
    for (const ViewLine &l : qAsConst(m_buffer))
        if (passesFilter(l)) showLine(l);
    updateState();
}

void SerialConsoleWindow::setViewFilter(const QString &query)
{
    m_filterEdit->setText(query);
    applyViewQueries();
}

void SerialConsoleWindow::setViewHighlight(const QString &query)
{
    m_highlightEdit->setText(query);
    applyViewQueries();
}

// isHidden, not isVisible: true of a label in a window not (yet) shown.
QString SerialConsoleWindow::viewQueryError() const { return m_queryError->isHidden() ? QString() : m_queryError->text(); }

int SerialConsoleWindow::highlightedLines() const
{
    int n = 0;
    for (QTextBlock b = m_view->document()->begin(); b.isValid(); b = b.next())
        if (b.blockFormat().property(QTextFormat::UserProperty).toBool()) ++n;
    return n;
}

void SerialConsoleWindow::onLine(const QByteArray &line, qint64 ms)
{
    // The console feed is the manager's (it must not depend on a window).
    if (m_hexView->isChecked()) return;             // the hex view shows chunks
    // Escape codes removed and control bytes drawn, for the eye only: the
    // console and the decoder got the line as it came.
    const QString text = serialDisplayText(line);
    appendView(m_timestamps->isChecked() ? stamp(ms) + QStringLiteral("  ") + text : text,
               line, ms, LogDirection::In);
    if (m_hold->isChecked()) updateState();
}

void SerialConsoleWindow::onBytes(const QByteArray &bytes, qint64 ms)
{
    if (!m_hexView->isChecked()) return;
    for (const SerialHexDumper::Row &r : m_hex.feed(bytes, ms))
        appendView(m_timestamps->isChecked() ? stamp(r.firstByteMs) + QStringLiteral("  ") + r.text : r.text);
    if (m_hex.hasPartial() && !m_hexFlush->isActive()) m_hexFlush->start();
}

void SerialConsoleWindow::flushHexRow()
{
    for (const SerialHexDumper::Row &r : m_hex.flushPartial())
        appendView(m_timestamps->isChecked() ? stamp(r.firstByteMs) + QStringLiteral("  ") + r.text : r.text);
}

void SerialConsoleWindow::onWritten(const QByteArray &bytes, qint64 ms)
{
    if (!m_echo->isChecked()) return;
    // A file going out is summarised (start and end lines), not echoed
    // piece by piece: a binary file as text is garbage, and a big one would
    // bury everything the card says back.
    if (m_sender && m_sender->isRunning()) return;
    // The line end that was sent is not shown as \u240D\u240A.
    QByteArray body = bytes;
    while (body.endsWith('\r') || body.endsWith('\n')) body.chop(1);
    QString shown = m_hexView->isChecked() ? serialToHex(bytes) : serialDisplayText(body);
    shown = QStringLiteral("TX> ") + shown;
    appendView(m_timestamps->isChecked() ? stamp(ms) + QStringLiteral("  ") + shown : shown,
               m_hexView->isChecked() ? QByteArray() : body, ms, LogDirection::Out);
}

// ---- send ---------------------------------------------------------------------------

bool SerialConsoleWindow::sendText(const QString &text)
{
    if (!isOpen()) { m_state->setText(tr("Open the port first.")); return false; }
    QByteArray bytes;
    if (m_sendHex->isChecked()) {
        bool ok = false;
        bytes = serialParseHex(text, &ok);
        if (!ok) {
            // Refuse rather than send something other than what was typed.
            m_state->setText(tr("\u2715 Not hex bytes: %1").arg(text));
            UiStyle::setTone(m_state, UiStyle::Tone::Fail);
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
QString SerialConsoleWindow::healthText() const { return m_health->text(); }
QString SerialConsoleWindow::tabKey() const
{
    return SerialManager::tabKeyFor(m_link ? m_link->config().portName : configFromUi().portName);
}

// ---- profiles -----------------------------------------------------------------------

void SerialConsoleWindow::refreshProfiles(const QString &select)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QVector<SerialProfile> all = SerialProfile::loadAll(s);
    const QString keep = select.isEmpty() ? currentProfile() : select;
    m_profile->clear();
    m_profile->addItem(tr("\u2014 none \u2014"), QString());
    for (const SerialProfile &p : all)
        m_profile->addItem(tr("%1 \u2014 %2 %3").arg(p.name, p.config.portName, p.config.summary()), p.name);
    const int i = keep.isEmpty() ? 0 : m_profile->findData(keep);
    m_profile->setCurrentIndex(qMax(0, i));
}

QString SerialConsoleWindow::currentProfile() const
{
    return m_profile->currentData().toString();
}

void SerialConsoleWindow::selectProfile(const QString &name)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QVector<SerialProfile> all = SerialProfile::loadAll(s);
    const int i = SerialProfile::indexOf(all, name);
    if (i < 0) { m_profile->setCurrentIndex(0); loadMacros(); return; }
    const SerialProfile &p = all.at(i);
    m_profile->setCurrentIndex(qMax(0, m_profile->findData(p.name)));
    // Through showPort: a profile whose port is already running shows it
    // as it runs, rather than settings that are not in force.
    showPort(p.config.portName);
    if (!isOpen()) {
        m_attaching = true;
        setConfigToUi(p.config);
        m_feed->setChecked(p.feed);
        m_attaching = false;
    }
    m_autoOpen->setChecked(p.autoOpen);
    loadMacros();
    updateState();
}

bool SerialConsoleWindow::saveProfile(const QString &name)
{
    SerialProfile p;
    p.name = name.trimmed();
    p.config = isOpen() ? m_link->config() : configFromUi();
    p.feed = m_feed->isChecked();
    p.autoOpen = m_autoOpen->isChecked();
    p.macros = m_macros;              // the row on screen goes with it
    p.usbSerial = SerialManager::usbSerialFor(p.config.portName);   // finds it after a renumber
    // Session 156: and, for an adapter with no serial number, its USB socket.
    if (p.usbSerial.isEmpty()) p.usbLocation = SerialManager::usbLocationFor(p.config.portName);
    if (p.name.isEmpty() || p.config.portName.isEmpty()) {
        m_state->setText(tr("A profile needs a name and a port."));
        return false;
    }
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    QVector<SerialProfile> all = SerialProfile::loadAll(s);
    SerialProfile::upsert(&all, p);
    SerialProfile::saveAll(s, all);
    s.sync();
    refreshProfiles(p.name);
    if (isOpen() && m_link->config().portName.compare(p.config.portName, Qt::CaseInsensitive) == 0)
        m_mgr->setLabel(p.config.portName, p.name);
    m_state->setText(tr("Saved profile \u201C%1\u201D: %2 %3").arg(p.name, p.config.portName, p.config.summary()));
    return true;
}

bool SerialConsoleWindow::deleteProfile(const QString &name)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    QVector<SerialProfile> all = SerialProfile::loadAll(s);
    if (!SerialProfile::remove(&all, name)) return false;
    SerialProfile::saveAll(s, all);
    s.sync();
    refreshProfiles();
    loadMacros();
    return true;
}

// ---- send file (session 108) --------------------------------------------------------

bool SerialConsoleWindow::sendFileData(const QByteArray &data, const QString &name,
                                       const SerialSendOptions &options)
{
    if (!isOpen()) { m_state->setText(tr("Open the port first.")); return false; }
    m_sendingName = name;
    if (!m_sender->start(m_link, data, options)) return false;
    appendView(tr("── sending %1: %2 bytes, %3 ──").arg(name).arg(data.size()).arg(options.summary()));
    if (m_sender->isRunning()) {
        m_sendProgress->setValue(0);
        m_sendProgress->show();
        m_sendStop->show();
    }
    return true;
}

bool SerialConsoleWindow::askSendOptions(SerialSendOptions *o, const QString &fileName, qint64 size)
{
    QDialog d(this);
    d.setWindowTitle(tr("Send %1").arg(fileName));
    auto *chunks = new QRadioButton(tr("In chunks"), &d);
    auto *lines = new QRadioButton(tr("Line by line"), &d);
    (o->mode == SerialSendOptions::Mode::Lines ? lines : chunks)->setChecked(true);
    auto *chunk = new QSpinBox(&d);
    chunk->setRange(1, 1 << 20);
    chunk->setSuffix(tr(" bytes"));
    chunk->setValue(o->chunkBytes);
    auto *delay = new QSpinBox(&d);
    delay->setRange(0, 60000);
    delay->setSuffix(tr(" ms"));
    delay->setValue(o->delayMs);
    auto *prompt = new QLineEdit(o->prompt, &d);
    prompt->setPlaceholderText(tr("e.g. >   (empty: just the delay between lines)"));
    auto *timeout = new QSpinBox(&d);
    timeout->setRange(100, 600000);
    timeout->setSuffix(tr(" ms"));
    timeout->setValue(o->promptTimeoutMs);
    auto *form = new QFormLayout(&d);
    form->addRow(new QLabel(tr("%1 bytes").arg(size), &d));
    form->addRow(chunks);
    form->addRow(tr("Chunk size"), chunk);
    form->addRow(lines);
    form->addRow(tr("Wait for prompt"), prompt);
    form->addRow(tr("Prompt timeout"), timeout);
    form->addRow(tr("Delay between pieces"), delay);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Send"));
    form->addRow(buttons);
    auto sync = [=]() {
        chunk->setEnabled(chunks->isChecked());
        prompt->setEnabled(lines->isChecked());
        timeout->setEnabled(lines->isChecked());
    };
    connect(chunks, &QRadioButton::toggled, &d, sync);
    sync();
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return false;
    o->mode = lines->isChecked() ? SerialSendOptions::Mode::Lines : SerialSendOptions::Mode::Chunks;
    o->chunkBytes = chunk->value();
    o->delayMs = delay->value();
    o->prompt = prompt->text();
    o->promptTimeoutMs = timeout->value();
    return true;
}

// ---- macros (session 109) -----------------------------------------------------------

void SerialConsoleWindow::loadMacros()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QVector<SerialProfile> all = SerialProfile::loadAll(s);
    const int i = SerialProfile::indexOf(all, currentProfile());
    m_macros = i >= 0 ? all.at(i).macros : SerialMacro::loadList(s, QStringLiteral("serial/macros"));
    rebuildMacroButtons();
}

void SerialConsoleWindow::setMacros(const QVector<SerialMacro> &macros)
{
    m_macros = macros;
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    QVector<SerialProfile> all = SerialProfile::loadAll(s);
    const int i = SerialProfile::indexOf(all, currentProfile());
    if (i >= 0) {
        all[i].macros = macros;               // saved with the profile
        SerialProfile::saveAll(s, all);
    } else {
        SerialMacro::saveList(s, QStringLiteral("serial/macros"), macros);
    }
    s.sync();
    rebuildMacroButtons();
}

void SerialConsoleWindow::rebuildMacroButtons()
{
    QLayout *row = m_macroBar->layout();
    while (QLayoutItem *it = row->takeAt(0)) {
        // Hidden first: deleteLater() leaves it drawn, out of any layout,
        // at its parent's corner until the event loop gets to it.
        if (QWidget *w = it->widget()) { w->hide(); w->deleteLater(); }
        delete it;
    }
    row->addWidget(new QLabel(currentProfile().isEmpty() ? tr("Macros")
                                                         : tr("Macros (%1)").arg(currentProfile()),
                              m_macroBar));
    for (int i = 0; i < m_macros.size(); ++i) {
        const SerialMacro &m = m_macros.at(i);
        auto *b = new QToolButton(m_macroBar);
        b->setObjectName(QStringLiteral("serialMacro"));
        UiStyle::makeChip(b, m.confirm ? UiStyle::Tone::Warn : UiStyle::Tone::Neutral);
        // A confirm macro says so on its face, not only when clicked.
        b->setText(m.confirm ? m.label + QStringLiteral(" \u26A0") : m.label);
        b->setToolTip(tr("Sends %1%2%3").arg(m.hex ? tr("hex ") : QString(), m.text,
                                             m.confirm ? tr("\nAsks first: the card acts on it.") : QString()));
        connect(b, &QToolButton::clicked, this, [this, i]() { runMacro(i); });
        row->addWidget(b);
    }
    if (m_macros.isEmpty()) {
        auto *hint = new QLabel(tr("none yet \u2014 Edit\u2026 to add STATUS?, DIAG?, RESET"), m_macroBar);
        hint->setStyleSheet(UiColor::mutedStyle());
        row->addWidget(hint);
    }
    static_cast<QHBoxLayout *>(row)->addStretch(1);
    auto *editBtn = new QToolButton(m_macroBar);
    editBtn->setObjectName(QStringLiteral("serialEditMacros"));
    editBtn->setText(tr("Edit\u2026"));
    connect(editBtn, &QToolButton::clicked, this, &SerialConsoleWindow::editMacros);
    row->addWidget(editBtn);
}

bool SerialConsoleWindow::runMacro(int index)
{
    if (index < 0 || index >= m_macros.size()) return false;
    const SerialMacro m = m_macros.at(index);
    if (!isOpen()) { m_state->setText(tr("Open the port first.")); return false; }
    bool ok = false;
    const QByteArray bytes = m.bytes(&ok);
    if (!ok) {
        m_state->setText(tr("\u2715 Macro %1 is not hex bytes: %2").arg(m.label, m.text));
        UiStyle::setTone(m_state, UiStyle::Tone::Fail);
        return false;
    }
    if (m.confirm && !(m_confirmMacro && m_confirmMacro(m))) {
        m_state->setText(tr("Macro %1 not sent.").arg(m.label));
        return false;
    }
    return m_link->write(bytes) == bytes.size();
}

void SerialConsoleWindow::editMacros()
{
    QDialog d(this);
    d.setWindowTitle(currentProfile().isEmpty() ? tr("Serial macros")
                                                : tr("Serial macros \u2014 %1").arg(currentProfile()));
    auto *table = new QTableWidget(0, 5, &d);
    table->setHorizontalHeaderLabels({ tr("Button"), tr("Sends"), tr("Hex"), tr("Line end"), tr("Ask first") });
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    auto addRow = [table](const SerialMacro &m) {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(m.label));
        table->setItem(r, 1, new QTableWidgetItem(m.text));
        auto *hex = new QTableWidgetItem;
        hex->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        hex->setCheckState(m.hex ? Qt::Checked : Qt::Unchecked);
        table->setItem(r, 2, hex);
        auto *end = new QComboBox(table);
        end->addItem(QObject::tr("none"), QByteArray());
        end->addItem(QStringLiteral("CR"), QByteArray("\r"));
        end->addItem(QStringLiteral("LF"), QByteArray("\n"));
        end->addItem(QStringLiteral("CR+LF"), QByteArray("\r\n"));
        end->setCurrentIndex(qMax(0, end->findData(m.ending)));
        table->setCellWidget(r, 3, end);
        auto *ask = new QTableWidgetItem;
        ask->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        ask->setCheckState(m.confirm ? Qt::Checked : Qt::Unchecked);
        table->setItem(r, 4, ask);
    };
    for (const SerialMacro &m : m_macros) addRow(m);
    // A new row whose text looks like RESET / ERASE / FLASH gets Ask first
    // ticked as it is typed; the operator can still untick it.
    connect(table, &QTableWidget::itemChanged, &d, [table](QTableWidgetItem *it) {
        if (it->column() != 0 && it->column() != 1) return;
        const int r = it->row();
        const QString words = (table->item(r, 0) ? table->item(r, 0)->text() : QString()) + QLatin1Char(' ')
                            + (table->item(r, 1) ? table->item(r, 1)->text() : QString());
        if (SerialMacro::looksDangerous(words) && table->item(r, 4)) table->item(r, 4)->setCheckState(Qt::Checked);
    });
    auto *add = new QPushButton(tr("Add"), &d);
    auto *del = new QPushButton(tr("Remove"), &d);
    connect(add, &QPushButton::clicked, &d, [=]() { addRow(SerialMacro{}); table->editItem(table->item(table->rowCount() - 1, 0)); });
    connect(del, &QPushButton::clicked, &d, [table]() { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto *lay = new QVBoxLayout(&d);
    lay->addWidget(table);
    auto *btns = new QHBoxLayout;
    btns->addWidget(add); btns->addWidget(del); btns->addStretch(1); btns->addWidget(buttons);
    lay->addLayout(btns);
    d.resize(640, 320);
    if (d.exec() != QDialog::Accepted) return;
    QVector<SerialMacro> out;
    for (int r = 0; r < table->rowCount(); ++r) {
        SerialMacro m;
        m.label = table->item(r, 0) ? table->item(r, 0)->text().trimmed() : QString();
        m.text = table->item(r, 1) ? table->item(r, 1)->text() : QString();
        m.hex = table->item(r, 2) && table->item(r, 2)->checkState() == Qt::Checked;
        if (auto *end = qobject_cast<QComboBox *>(table->cellWidget(r, 3))) m.ending = end->currentData().toByteArray();
        m.confirm = table->item(r, 4) && table->item(r, 4)->checkState() == Qt::Checked;
        if (!m.label.isEmpty()) out << m;
    }
    setMacros(out);
}

// ---- find the baud (session 111) ----------------------------------------------------

bool SerialConsoleWindow::findBaud(const QVector<qint32> &rates, int dwellMs)
{
    if (m_autoBaud->isRunning()) return false;
    SerialConfig c = configFromUi();
    if (c.portName.isEmpty()) { m_state->setText(tr("Choose a port first.")); return false; }
    // It probes on a link of its own, so the port must be free — and the
    // garbage read at wrong rates must not reach the console.
    if (isOpen() || m_mgr->isReconnecting(c.portName)) closePort();
    if (!m_autoBaud->start(c, rates, dwellMs)) return false;
    m_findBaud->setText(tr("Stop"));
    m_open->setEnabled(false);
    return true;
}
