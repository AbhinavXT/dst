#include "framediffwindow.h"
#include "uistyle.h"
#include "uicolors.h"

#include "framediff.h"
#include "theme.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

// Same list the Decode Workbench offers, for the same reason: bare hex needs
// to be told what it is.
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

}  // namespace

FrameDiffWindow::FrameDiffWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Frame Diff"));
    resize(900, 700);
    WindowGeometry::restore(this, QStringLiteral("frameDiff"));

    QWidget *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // --- the frames, side by side ----------------------------------------
    // Two to start with; "Add frame" grows the row. A scroll area rather
    // than ever-narrower boxes: at six frames, boxes that shrink to fit are
    // too small to read a pasted line in.
    m_inputs = new QHBoxLayout;
    auto *inputHost = new QWidget(central);
    inputHost->setLayout(m_inputs);
    auto *inputScroll = new QScrollArea(central);
    inputScroll->setWidgetResizable(true);
    inputScroll->setWidget(inputHost);
    inputScroll->setFrameShape(QFrame::NoFrame);
    inputScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    inputScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Exactly the frame boxes' height (set below, once one exists): the
    // decoded table under them is what the window is for.
    root->addWidget(inputScroll);
    addFrameColumn();
    addFrameColumn();

    // --- controls ---------------------------------------------------------
    auto *ctl = new QHBoxLayout;
    m_onlyDiff = new QCheckBox(tr("Show only differences"), central);
    m_onlyDiff->setChecked(true);   // the reason to open this window
    ctl->addWidget(m_onlyDiff);
    auto *swapBtn = new QPushButton(tr("Swap A / B"), central);
    ctl->addWidget(swapBtn);
    m_addBtn = new QPushButton(tr("Add frame"), central);
    m_delBtn = new QPushButton(tr("Remove frame"), central);
    connect(m_addBtn, &QPushButton::clicked, this, [this] { addFrameColumn(); rebuild(); });
    connect(m_delBtn, &QPushButton::clicked, this, [this] { removeFrameColumn(); rebuild(); });
    ctl->addWidget(m_addBtn);
    ctl->addWidget(m_delBtn);
    ctl->addStretch(1);
    root->addLayout(ctl);

    m_status = new StatusLine(this);
    // A caption, not an event: it describes what the panel is showing,
    // so it must not expire the way a report of something that happened
    // does. A window that explains itself for six seconds and then goes
    // blank is worse than one that never explained itself.
    m_status->state(tr("Paste two frames, or send two rows here from the log."));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);

    // --- the diff ---------------------------------------------------------
    m_table = new QTableWidget(0, 3, central);
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("A"), tr("B") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setFont(UiStyle::monoFont());
    m_table->setObjectName(QStringLiteral("frameDiffTable"));
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);   // as the other tool tables
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    root->addWidget(m_table, 1);

    setCentralWidget(central);

    connect(m_onlyDiff, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    connect(swapBtn, &QPushButton::clicked, this, [this]() {
        m_updating = true;
        const QString a = m_in[0]->toPlainText();
        const int     ta = m_type[0]->currentIndex();
        m_in[0]->setPlainText(m_in[1]->toPlainText());
        m_type[0]->setCurrentIndex(m_type[1]->currentIndex());
        m_in[1]->setPlainText(a);
        m_type[1]->setCurrentIndex(ta);
        m_updating = false;
        rebuild();
    });

    rebuild();
}

int FrameDiffWindow::maxFrames() { return 6; }

void FrameDiffWindow::addFrameColumn()
{
    if (m_in.size() >= maxFrames()) { return; }
    const int s = m_in.size();

    // A, B, C … rather than 1, 2, 3: the table headers and the status line
    // read as letters, and two namings for the same column is one too many.
    auto *box = new QGroupBox(tr("Frame %1").arg(QChar('A' + s)), this);
    box->setMinimumWidth(260);
    auto *v = new QVBoxLayout(box);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Type:"), box));
    auto *type = new QComboBox(box);
    type->addItem(tr("(from capture line)"), QString());
    for (const char *tk : kTokens) {
        type->addItem(QString::fromLatin1(tk), QString::fromLatin1(tk));
    }
    type->setToolTip(tr("Only needed for bare hex. A pasted capture line\n"
                        "carries its own type and ignores this."));
    row->addWidget(type, 1);
    v->addLayout(row);

    auto *edit = new QPlainTextEdit(box);
    edit->setFont(UiStyle::monoFont());
    edit->setPlaceholderText(tr("Paste a capture line or bare hex"));
    // Tall enough for a whole frame (session 138): at 90 px a 39-byte
    // capture line showed three of its five lines.
    edit->setObjectName(QStringLiteral("frameDiffInput"));
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 7 + 12);
    v->addWidget(edit);

    auto *head = new QLabel(QString(), box);
    head->setWordWrap(true);
    head->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(head);

    m_inputs->addWidget(box, 1);
    m_in.push_back(edit);
    m_type.push_back(type);
    m_head.push_back(head);
    m_box.push_back(box);

    connect(edit, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_updating) { rebuild(); }
    });
    connect(type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_updating) { rebuild(); }
    });

    if (m_addBtn) { m_addBtn->setEnabled(m_in.size() < maxFrames()); }
    if (m_delBtn) { m_delBtn->setEnabled(m_in.size() > 2); }
}

void FrameDiffWindow::removeFrameColumn()
{
    // Never below two: a diff of one frame is a decode, and the Decode
    // Workbench already does that better.
    if (m_in.size() <= 2) { return; }
    delete m_box.takeLast();
    m_in.removeLast();
    m_type.removeLast();
    m_head.removeLast();
    if (m_addBtn) { m_addBtn->setEnabled(m_in.size() < maxFrames()); }
    if (m_delBtn) { m_delBtn->setEnabled(m_in.size() > 2); }
}

void FrameDiffWindow::setSide(int side, const QString &text)
{
    if (side < 0 || side >= maxFrames()) { return; }
    while (m_in.size() <= side) { addFrameColumn(); }
    m_updating = true;
    m_in[side]->setPlainText(text.trimmed());
    m_updating = false;
    rebuild();
}

void FrameDiffWindow::setEntries(const QVector<LogEntryPtr> &entries)
{
    m_updating = true;
    while (m_in.size() < qMax(2, qMin(entries.size(), maxFrames()))) { addFrameColumn(); }
    while (m_in.size() > qMax(2, qMin(entries.size(), maxFrames()))) { removeFrameColumn(); }
    for (int i = 0; i < m_in.size(); ++i) {
        m_in[i]->setPlainText(i < entries.size() ? entryBufferText(entries.at(i))
                                                 : QString());
    }
    m_updating = false;
    rebuild();
}

void FrameDiffWindow::setEntries(const LogEntryPtr &left, const LogEntryPtr &right)
{
    setEntries(QVector<LogEntryPtr>{ left, right });
}

void FrameDiffWindow::rebuild()
{
    const int n = m_in.size();
    QVector<CaptureLine> c(n);
    int valid = 0;
    for (int s = 0; s < n; ++s) {
        QString err;
        c[s] = FrameDiff::parseInput(m_in[s]->toPlainText(),
                                     m_type[s]->currentData().toString(), &err);
        if (c[s].valid) {
            ++valid;
            m_head[s]->setText(tr("%1 · %2 B · loco %3 · CRC %4")
                                   .arg(c[s].typeToken.isEmpty() ? tr("?") : c[s].typeToken)
                                   .arg(c[s].bytes.size())
                                   .arg(c[s].locoId)
                                   .arg(c[s].crcChecked ? (c[s].crcOk ? tr("PASS") : tr("FAIL"))
                                                        : tr("n/a")));
            m_head[s]->setStyleSheet(QString());
        } else {
            m_head[s]->setText(err);
            m_head[s]->setStyleSheet(UiColor::errorStyle());
        }
    }

    // Header: Field, then one column per frame.
    QStringList heads{ tr("Field") };
    for (int s = 0; s < n; ++s) { heads << QString(QChar('A' + s)); }
    m_table->setColumnCount(heads.size());
    m_table->setHorizontalHeaderLabels(heads);
    m_table->setObjectName(QStringLiteral("frameDiffTable"));
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);   // as the other tool tables
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int col = 1; col < heads.size(); ++col) {
        m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    m_table->setRowCount(0);

    if (valid < 2) {
        m_status->state(tr("Paste at least two frames, or send rows here from the log."));
        return;
    }

    QVector<QVector<FieldRow>> decoded;
    QVector<int> col;                      // decoded index -> table column
    for (int s = 0; s < n; ++s) {
        if (!c[s].valid) { continue; }
        decoded.push_back(CaptureDecoder::describe(c[s]));
        col.push_back(s);
    }

    const QVector<FrameDiff::MultiRow> rows = FrameDiff::compareMany(decoded);
    const FrameDiff::MultiSummary sum = FrameDiff::summarizeMany(rows);

    // Byte offsets stay a two-frame idea: with four frames "bytes 3, 9 differ"
    // does not say between which of them, so it is only shown for a pair.
    QString byteText;
    if (decoded.size() == 2) {
        const QVector<int> bytes = FrameDiff::differingBytes(c[col[0]].bytes, c[col[1]].bytes);
        if (bytes.isEmpty()) {
            byteText = tr(" — identical bytes");
        } else {
            QStringList offs;
            for (int i = 0; i < bytes.size() && i < 12; ++i) { offs << QString::number(bytes[i]); }
            byteText = tr(" — %1 %2 differ: %3%4")
                           .arg(bytes.size(), 0, 10)
                           .arg(bytes.size() == 1 ? tr("byte") : tr("bytes"))
                           .arg(offs.join(QStringLiteral(", ")))
                           .arg(bytes.size() > 12 ? QStringLiteral(" …") : QString());
        }
    }

    if (sum.identical()) {
        // Also a caption — it describes the table underneath rather than
        // reporting an action, which is why it is sticky and not coloured as
        // success. "No differences" is a finding, not a win.
        m_status->state(tr("%1 frames, no field differences (%2 fields compared)%3")
                              .arg(decoded.size()).arg(sum.same).arg(byteText));
    } else {
        m_status->state(tr("%1 frames · %2 %3, %4 the same, "
                             "%5 missing from at least one%6")
                              .arg(decoded.size()).arg(sum.changed)
                              .arg(sum.changed == 1 ? tr("field differs") : tr("fields differ"))
                              .arg(sum.same).arg(sum.partial).arg(byteText));
    }

    const bool onlyDiff = m_onlyDiff->isChecked();
    for (const FrameDiff::MultiRow &r : rows) {
        const bool differs = !r.allSame || !r.everywhere;
        if (onlyDiff && !differs) { continue; }

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(r.field));

        // With three or more frames the useful thing is not "these differ" but
        // WHICH one is out of step, so the minority value is coloured and the
        // majority left plain. With two there is no minority to speak of, and
        // both sides are simply marked as changed.
        const QVector<int> odd = decoded.size() >= 3 ? FrameDiff::oddOnesOut(r)
                                                     : QVector<int>();

        for (int i = 0; i < r.values.size(); ++i) {
            auto *item = new QTableWidgetItem(
                r.present.value(i) ? r.values.at(i) : tr("—"));
            if (differs) {
                QFont bold = item->font(); bold.setBold(true); item->setFont(bold);
            }
            if (odd.contains(i)) { item->setForeground(UiColor::warning()); }
            if (!r.present.value(i)) { item->setForeground(UiColor::muted()); }
            m_table->setItem(row, col.value(i, i) + 1, item);
        }
    }
}

void FrameDiffWindow::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, QStringLiteral("frameDiff"));
    QMainWindow::closeEvent(event);
}
