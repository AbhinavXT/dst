#include "odometrywindow.h"

#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QTableWidgetItem *cell(const QString &text, bool right = false)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (right) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

}  // namespace

OdometryWindow::OdometryWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Odometry at the tags — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1200, 600);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("Tag"), tr("Tag at (m)"), tr("Loco's own (m)"), tr("Speed (km/h)"),
                                         tr("Error (m)"), tr("Since tag before (m)"), tr("m per km"), tr("Not counted") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("Loco's own: its @dmi location moved on by its speed to the moment of the read. Error: that "
                           "minus the tag's abs_loc, along the direction of travel (+ = ahead of the tag). "
                           "Double-click: show the read in the log"));
    m_status = new StatusLine;
    auto *rebuildBtn = new QPushButton(tr("Rebuild"), this);
    rebuildBtn->setToolTip(tr("Read the tab again (more traffic has arrived)"));
    auto *copy = new QPushButton(tr("Copy table"), this);
    copy->setToolTip(tr("Every read, tab-separated"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildBtn);
    buttons->addWidget(copy);
    auto *root = new QVBoxLayout(this);
    root->addWidget(m_summary);
    root->addWidget(m_table, 1);
    root->addLayout(buttons);

    connect(rebuildBtn, &QPushButton::clicked, this, &OdometryWindow::rebuild);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(Odometry::toTsv(m_report));
        m_status->ok(tr("Copied %1 reads").arg(m_report.reads.size()));
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_report.reads.size()) emit jumpRequested(m_tabKey, m_report.reads.at(row).ms);
    });
    rebuild();
}

QString OdometryWindow::summary() const { return m_summary->text(); }

void OdometryWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_report = Odometry::build(m_model);
    QApplication::restoreOverrideCursor();
    m_summary->setText(Odometry::summaryText(m_report));
    m_table->setRowCount(m_report.reads.size());
    for (int i = 0; i < m_report.reads.size(); ++i) {
        const Odometry::Read &rd = m_report.reads.at(i);
        const bool measured = rd.counted() || rd.direction;
        m_table->setItem(i, 0, cell(QDateTime::fromMSecsSinceEpoch(rd.ms).toString(QStringLiteral("HH:mm:ss.zzz"))));
        m_table->setItem(i, 1, cell(QString::number(rd.tag), true));
        m_table->setItem(i, 2, cell(QString::number(rd.tagLocM, 'f', 0), true));
        m_table->setItem(i, 3, cell(measured ? QString::number(rd.estimateM, 'f', 1) : QString::number(rd.dmiLocM, 'f', 0), true));
        m_table->setItem(i, 4, cell(QString::number(rd.speedKmh, 'f', 0), true));
        auto *err = cell(measured ? QString::number(rd.errorM, 'f', 1) : QString(), true);
        if (!rd.counted()) err->setForeground(UiColor::muted());
        m_table->setItem(i, 5, err);
        m_table->setItem(i, 6, cell(rd.spacingM > 0 ? QString::number(rd.spacingM, 'f', 0) : QString(), true));
        m_table->setItem(i, 7, cell(rd.spacingM > 0 ? QString::number(rd.perKm(), 'f', 1) : QString(), true));
        auto *why = cell(rd.notCounted);
        if (!rd.counted()) why->setForeground(UiColor::warning());
        m_table->setItem(i, 8, why);
    }
    if (m_report.reads.isEmpty()) m_status->warn(tr("No main-tag @rfid in this tab"));
    else m_status->state(tr("%1 measured of %2 reads").arg(m_report.counted).arg(m_report.reads.size()));
}
