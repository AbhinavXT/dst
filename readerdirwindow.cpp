#include "readerdirwindow.h"

#include "messagedispatcher.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

QTableWidgetItem *cell(const QString &text, const QString &tip = QString())
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!tip.isEmpty()) it->setToolTip(tip);
    return it;
}

QTableWidgetItem *centred(const QString &text)
{
    QTableWidgetItem *it = cell(text);
    it->setTextAlignment(Qt::AlignCenter);
    return it;
}

QTableWidget *table(QWidget *parent, const QStringList &headers)
{
    auto *t = new QTableWidget(parent);
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

QString tsv(const QTableWidget *t)
{
    QStringList lines;
    QStringList head;
    for (int c = 0; c < t->columnCount(); ++c) head << t->horizontalHeaderItem(c)->text();
    lines << head.join(QLatin1Char('\t'));
    for (int r = 0; r < t->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < t->columnCount(); ++c) row << (t->item(r, c) ? t->item(r, c)->text() : QString());
        lines << row.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

ReaderDirWindow::ReaderDirWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Reader direction"));
    WindowGeometry::makeResizableWindow(this);
    resize(1100, 640);

    m_picker = new QComboBox(this);
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_test = new QComboBox(this);
    m_test->addItem(tr("No test case"), QString());
    for (const ReaderDir::TestCase &tc : ReaderDir::frs18())
        m_test->addItem(QStringLiteral("%1  %2").arg(tc.id, tc.title), tc.id);
    m_test->setToolTip(tr("An RDSO test case (FRS 18, direction determination by tag read): its expected table "
                          "beside what was observed"));
    // The titles are long: the box may be narrower than they are (the whole
    // title is in the list and under the tables).
    m_test->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_test->setMinimumContentsLength(24);
    m_from = new QSpinBox(this);
    m_from->setMinimum(1);
    m_from->setToolTip(tr("The read the test starts at. Both readers start at U there, as at a start of mission"));
    m_to = new QSpinBox(this);
    m_to->setMinimum(1);
    m_to->setToolTip(tr("The last read of the test run: a later run in the same log does not count for this one"));
    m_copy = new QPushButton(tr("Copy tables"), this);
    m_copy->setToolTip(tr("Both tables, tab-separated, for the test record"));

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Loco log:"), this));
    top->addWidget(m_picker);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Test case:"), this));
    top->addWidget(m_test, 1);
    top->addWidget(new QLabel(tr("From read:"), this));
    top->addWidget(m_from);
    top->addWidget(new QLabel(tr("to"), this));
    top->addWidget(m_to);
    top->addWidget(m_copy);

    m_mapping = new QLabel(this);
    m_mapping->setWordWrap(true);
    m_testInput = new QLabel(this);
    m_testInput->setWordWrap(true);

    m_observed = table(this, { tr("#"), tr("Time"), tr("Reader"), tr("Tag read"), tr("Reader-1 dir"),
                               tr("Reader-2 dir"), tr("OVK dir"), tr("Tag reported to SVK"), tr("Test row") });
    m_observed->setToolTip(tr("One row per tag read (@rdir). The tag reported to SVK is the loco's next ARP's "
                              "LAST_RFID_TAG, when it changed. Double-click: show this read in the log"));
    m_expected = table(this, { tr("Reader-1 dir"), tr("Reader-2 dir"), tr("OVK dir"), tr("Tag reported to SVK"),
                               tr("Observed"), tr("OK / Not OK") });
    m_expected->setToolTip(tr("The RDSO table. \"Seen at read N\": a read after the previous row's with these "
                              "directions and this tag reported. OK / Not OK is for the signatory"));

    // One above the other: each has a few rows and needs its full width.
    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_observed);
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_testInput);
    rightLayout->addWidget(m_expected, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);

    m_status = new StatusLine;
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_mapping);
    root->addWidget(split, 1);
    root->addWidget(m_status);

    connect(m_picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        setSource(m_picker->currentData().toString());
    });
    connect(m_test, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { fill(); });
    connect(m_from, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { fill(); });
    connect(m_to, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { fill(); });
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(asText());
        m_status->ok(tr("Copied both tables"));
    });
    connect(m_observed, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_reads.size()) emit jumpRequested(m_key, m_reads.at(row).ms);
    });
    if (m_dispatcher)
        connect(m_dispatcher, &MessageDispatcher::tabRequested, this, [this](QString, QString) { refreshPicker(); });

    refreshPicker();
    QString pick;
    for (int i = 0; i < m_picker->count() && pick.isEmpty(); ++i) {
        const QString k = m_picker->itemData(i).toString();
        if (m_dispatcher && ReaderDir::hasRdir(m_dispatcher->modelForKey(k))) pick = k;
    }
    if (pick.isEmpty() && m_picker->count() > 0) pick = m_picker->itemData(0).toString();
    setSource(pick);
}

void ReaderDirWindow::refreshPicker()
{
    if (!m_dispatcher) return;
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    m_picker->blockSignals(true);
    m_picker->clear();
    for (const QString &k : keys) {
        const QString friendly = m_dispatcher->friendlyNameFor(k);
        m_picker->addItem(friendly == k ? k : QStringLiteral("%1   (%2)").arg(friendly, k), k);
    }
    const int idx = m_picker->findData(m_key);
    if (idx >= 0) m_picker->setCurrentIndex(idx);
    m_picker->blockSignals(false);
}

void ReaderDirWindow::setSource(const QString &key)
{
    m_key = key;
    const int idx = m_picker->findData(key);
    if (idx >= 0 && idx != m_picker->currentIndex()) {
        m_picker->blockSignals(true);
        m_picker->setCurrentIndex(idx);
        m_picker->blockSignals(false);
    }
    const QString name = m_dispatcher ? m_dispatcher->friendlyNameFor(key) : key;
    setWindowTitle(key.isEmpty() ? tr("Reader direction") : tr("Reader direction — %1").arg(name.isEmpty() ? key : name));
    rebuild();
}

void ReaderDirWindow::setTestCase(const QString &id)
{
    const int idx = m_test->findData(id);
    m_test->setCurrentIndex(idx >= 0 ? idx : 0);
}

void ReaderDirWindow::setFromRead(int index)
{
    m_from->setValue(index + 1);
}

void ReaderDirWindow::setToRead(int index)
{
    m_to->setValue(index < 0 ? m_to->maximum() : index + 1);
}

void ReaderDirWindow::rebuild()
{
    m_log = ReaderDir::extract(m_dispatcher && !m_key.isEmpty() ? m_dispatcher->modelForKey(m_key) : nullptr);
    m_from->blockSignals(true);
    m_from->setMaximum(qMax(1, m_log.reads.size()));
    m_from->setValue(qMin(m_from->value(), m_from->maximum()));
    m_from->blockSignals(false);
    m_to->blockSignals(true);
    m_to->setMaximum(qMax(1, m_log.reads.size()));
    m_to->setValue(m_to->maximum());
    m_to->blockSignals(false);
    fill();
}

void ReaderDirWindow::fill()
{
    const int from = m_from->value() - 1;
    m_reads = ReaderDir::fromRead(m_log, from, m_to->value() - 1);
    bool byLocation = false;
    m_order = ReaderDir::tagOrder(m_log, m_reads, &byLocation);

    QStringList map;
    for (int i = 0; i < m_order.size(); ++i) {
        const quint16 t = m_order.at(i);
        map << (byLocation ? tr("R%1 = tag %2 (%3 m)").arg(i + 1).arg(t).arg(m_log.tagLocM.value(t), 0, 'f', 0)
                           : tr("R%1 = tag %2").arg(i + 1).arg(t));
    }
    m_mapping->setText(map.isEmpty() ? tr("No tag read.")
        : map.join(QStringLiteral(" · ")) + (byLocation ? tr("  (ordered by location, from @rfid)")
                                                              : tr("  (ordered by tag id: no @rfid location for every tag)")));

    // The test case, if one is picked.
    const QString id = m_test->currentData().toString();
    const ReaderDir::TestCase *tc = nullptr;
    for (const ReaderDir::TestCase &c : ReaderDir::frs18()) if (c.id == id) tc = &c;
    m_match = tc ? ReaderDir::match(*tc, m_reads, m_order) : QVector<int>();
    QHash<int, int> rowOfRead;                       // observed read -> expected row
    for (int k = 0; k < m_match.size(); ++k) if (m_match.at(k) >= 0) rowOfRead.insert(m_match.at(k), k);

    m_observed->setRowCount(m_reads.size());
    for (int i = 0; i < m_reads.size(); ++i) {
        const ReaderDir::Read &r = m_reads.at(i);
        m_observed->setItem(i, 0, centred(QString::number(from + i + 1)));
        m_observed->setItem(i, 1, cell(hms(r.ms)));
        m_observed->setItem(i, 2, centred(tr("Reader-%1").arg(r.reader)));
        m_observed->setItem(i, 3, cell(ReaderDir::tagLabel(r.tag, m_order)));
        m_observed->setItem(i, 4, centred(ReaderDir::letter(r.r1Dir)));
        m_observed->setItem(i, 5, centred(ReaderDir::letter(r.r2Dir)));
        m_observed->setItem(i, 6, centred(ReaderDir::letter(r.movementDir)));
        m_observed->setItem(i, 7, cell(ReaderDir::tagLabel(r.reported, m_order)));
        auto *tr_ = centred(rowOfRead.contains(i) ? QString::number(rowOfRead.value(i) + 1) : QString());
        if (rowOfRead.contains(i)) {
            tr_->setForeground(UiColor::ok());
            tr_->setToolTip(tr("Expected row %1 of %2 is seen here").arg(rowOfRead.value(i) + 1).arg(id));
        }
        m_observed->setItem(i, 8, tr_);
    }

    m_testInput->setText(tc ? tr("%1: %2. Input, reader-1 / reader-2: %3").arg(tc->id, tc->title, tc->input)
                            : tr("Pick a test case to set its RDSO table beside the reads."));
    m_expected->setRowCount(tc ? tc->rows.size() : 0);
    int seen = 0;
    for (int k = 0; tc && k < tc->rows.size(); ++k) {
        const ReaderDir::ExpectedRow &row = tc->rows.at(k);
        m_expected->setItem(k, 0, centred(ReaderDir::letter(row.r1)));
        m_expected->setItem(k, 1, centred(ReaderDir::letter(row.r2)));
        m_expected->setItem(k, 2, centred(ReaderDir::letter(row.ovk)));
        m_expected->setItem(k, 3, centred(row.tag ? QStringLiteral("R%1").arg(row.tag) : QString()));
        const int j = m_match.value(k, -1);
        auto *o = cell(j >= 0 ? tr("seen at read %1").arg(from + j + 1) : tr("not seen in order"));
        o->setForeground(j >= 0 ? UiColor::ok() : UiColor::warning());
        m_expected->setItem(k, 4, o);
        m_expected->setItem(k, 5, cell(QString(), tr("For the signatory")));
        seen += j >= 0 ? 1 : 0;
    }

    QStringList parts;
    parts << tr("%1 reads").arg(m_log.reads.size());
    int reported = 0;
    for (const ReaderDir::Read &r : m_reads) reported += r.reported >= 0 ? 1 : 0;
    parts << tr("%1 tags reported to SVK").arg(reported);
    if (tc) parts << tr("%1: %2 of %3 expected rows seen in order").arg(tc->id).arg(seen).arg(tc->rows.size());
    if (m_log.badFrames) parts << tr("%1 @rdir not 5 bytes, not read").arg(m_log.badFrames);
    if (m_log.reads.isEmpty())
        m_status->warn(tr("No @rdir in this log: the firmware sends it only with the reader-direction logging added "
                          "(SOS_handoff, README 04)."));
    else if (tc && seen < tc->rows.size())
        m_status->warn(parts.join(QStringLiteral(" · ")));
    else
        m_status->state(parts.join(QStringLiteral(" · ")));
}

QString ReaderDirWindow::asText() const
{
    QString out = tr("Reader direction, %1").arg(m_key) + QLatin1Char('\n') + m_mapping->text() + QStringLiteral("\n\n")
                + tsv(m_observed);
    if (m_expected->rowCount() > 0) out += QStringLiteral("\n\n") + m_testInput->text() + QLatin1Char('\n') + tsv(m_expected);
    return out + QLatin1Char('\n');
}
