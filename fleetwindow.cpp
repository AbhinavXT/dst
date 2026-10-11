#include "fleetwindow.h"

#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace {

QTableWidgetItem *text(const QString &s, const QString &tip = QString())
{
    auto *it = new QTableWidgetItem(s);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!tip.isEmpty()) it->setToolTip(tip);
    return it;
}

// A number that sorts as one.
QTableWidgetItem *number(double v, int decimals = 0)
{
    auto *it = new QTableWidgetItem;
    it->setData(Qt::DisplayRole, decimals ? QVariant(QString::number(v, 'f', decimals).toDouble()) : QVariant(qint64(std::llround(v))));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

}  // namespace

FleetWindow::FleetWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Fleet summary"));
    WindowGeometry::makeResizableWindow(this);
    resize(1250, 600);

    m_folder = new QLineEdit(this);
    m_folder->setPlaceholderText(tr("A folder of .log / .cap files (subfolders too)"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    m_start = new QPushButton(tr("Start"), this);
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(true);
    m_progress->setFormat(tr("%v of %m"));
    m_progress->setMaximum(1);
    m_progress->setValue(0);

    m_table = new QTableWidget(this);
    const QStringList heads{ tr("Loco"), tr("Day"), tr("Files"), tr("Hours"), tr("km"), tr("Modes"), tr("EB"), tr("FSB"),
                             tr("Brake reasons (commonest)"), tr("Top km/h"), tr("Faults"), tr("Reject frames"), tr("Tags"),
                             tr("Missions"), tr("SLRP lost %"), tr("Outages"), tr("Odometry (m)") };
    m_table->setColumnCount(heads.size());
    m_table->setHorizontalHeaderLabels(heads);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("One row per loco per day, from the run report, radio links and odometry at the tags. "
                           "SLRP lost: in contact; an outage is a gap over 30 s. Odometry: the median error at the tags, "
                           "along the direction of travel. Click a header to sort"));

    m_status = new StatusLine;
    m_export = new QPushButton(tr("Export CSV…"), this);
    m_export->setEnabled(false);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Folder:"), this));
    top->addWidget(m_folder, 1);
    top->addWidget(browse);
    top->addWidget(m_start);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_export);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_progress);
    root->addWidget(m_table, 1);
    root->addLayout(bottom);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder of logs"), m_folder->text());
        if (!d.isEmpty()) m_folder->setText(d);
    });
    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (m_running) stop();
        else run(m_folder->text());
    });
    connect(m_export, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export fleet summary"), QStringLiteral("fleet_summary.csv"),
                                                          tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text) && f.write(Fleet::toCsv(m_rows).toUtf8()) >= 0) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    m_status->state(tr("Pick a folder and Start."));
}

void FleetWindow::run(const QString &dir)
{
    if (m_running) return;
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        m_status->fail(tr("Not a folder: %1").arg(dir));
        return;
    }
    m_folder->setText(dir);
    m_rows.clear();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_export->setEnabled(false);
    const QVector<Fleet::Group> groups = Fleet::scan(dir);
    if (groups.isEmpty()) {
        m_status->warn(tr("No DLConsole .log or loco_*.cap file in %1").arg(dir));
        return;
    }
    m_running = true;
    m_stop = false;
    m_start->setText(tr("Stop"));
    m_progress->setMaximum(groups.size());
    m_progress->setValue(0);
    QPointer<FleetWindow> self(this);
    auto keepGoing = [self]() {
        QApplication::processEvents();
        return self && !self->m_stop;
    };
    for (int i = 0; i < groups.size(); ++i) {
        m_status->state(tr("Reading %1, %2 (%3 file(s))…").arg(groups.at(i).loco, groups.at(i).day.toString(Qt::ISODate))
                            .arg(groups.at(i).files.size()));
        if (!keepGoing()) break;
        const Fleet::Row r = Fleet::summarise(groups.at(i), keepGoing);
        if (!self) return;                                 // closed while reading
        if (m_stop) break;
        m_rows << r;
        addRow(r);
        m_progress->setValue(i + 1);
    }
    m_running = false;
    m_start->setText(tr("Start"));
    m_table->setSortingEnabled(true);
    m_export->setEnabled(!m_rows.isEmpty());
    if (m_stop) m_status->warn(tr("Stopped: %1 of %2 loco-day(s) read").arg(m_rows.size()).arg(groups.size()));
    else m_status->ok(tr("%1 loco-day(s) from %2").arg(m_rows.size()).arg(dir));
}

void FleetWindow::addRow(const Fleet::Row &r)
{
    const int i = m_table->rowCount();
    m_table->insertRow(i);
    QStringList names;
    for (const QString &f : r.files) names << QFileInfo(f).fileName();
    int c = 0;
    m_table->setItem(i, c++, text(r.loco, r.error));
    m_table->setItem(i, c++, text(r.day.toString(Qt::ISODate)));
    m_table->setItem(i, c++, text(QString::number(r.files.size()), names.join(QLatin1Char('\n'))));
    m_table->setItem(i, c++, number(double(r.lastMs - r.firstMs) / 3600000.0, 2));
    m_table->setItem(i, c++, number(r.distanceKm, 1));
    m_table->setItem(i, c++, text(r.modesText()));
    m_table->setItem(i, c++, number(r.eb));
    m_table->setItem(i, c++, number(r.fsb));
    m_table->setItem(i, c++, text(r.brakeReasons, r.brakeReasons));
    m_table->setItem(i, c++, number(r.maxSpeedKmh));
    m_table->setItem(i, c++, number(r.faults));
    m_table->setItem(i, c++, number(r.rejectFrames));
    m_table->setItem(i, c++, number(r.tags));
    m_table->setItem(i, c++, number(r.missions));
    m_table->setItem(i, c++, number(r.slrpLostPercent(), 1));
    m_table->setItem(i, c++, number(r.outages));
    auto *odo = r.odoReads ? number(r.odoMedianM, 1) : text(QString());
    if (r.odoReads) odo->setToolTip(tr("median of %1 tag reads").arg(r.odoReads));
    m_table->setItem(i, c++, odo);
    if (!r.error.isEmpty()) m_table->item(i, 0)->setForeground(UiColor::warning());
}
