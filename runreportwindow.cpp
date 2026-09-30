#include "runreportwindow.h"

#include "statusline.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPalette>
#include <QPushButton>
#include <QSaveFile>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

RunReportWindow::RunReportWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
    , m_tabName(tabName)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Run summary — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(820, 760);

    m_view = new QTextBrowser(this);
    m_view->setOpenLinks(false);
    // A document, shown as the paper it prints on: dark ink on white in
    // every theme. Its muted #666 is 5.7:1 on white; on a dark theme's
    // base it would be unreadable.
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, QColor(255, 255, 255));
    paper.setColor(QPalette::Text, QColor(17, 17, 17));
    m_view->setPalette(paper);
    m_status = new StatusLine;

    auto *save = new QPushButton(tr("Save HTML…"), this);
    save->setToolTip(tr("A single file: opens in any browser, prints to PDF from there"));
    auto *copy = new QPushButton(tr("Copy"), this);
    auto *rebuildButton = new QPushButton(tr("Rebuild"), this);
    rebuildButton->setToolTip(tr("Read the tab again (more traffic has arrived)"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildButton);
    buttons->addWidget(copy);
    buttons->addWidget(save);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_view, 1);
    root->addLayout(buttons);

    connect(rebuildButton, &QPushButton::clicked, this, &RunReportWindow::rebuild);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_view->toPlainText());
        m_status->ok(tr("Copied as text"));
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString stamp = QDateTime::fromMSecsSinceEpoch(m_summary.firstMs).toString(QStringLiteral("yyyyMMdd_HHmm"));
        const QString path = QFileDialog::getSaveFileName(this, tr("Save run summary"),
            QStringLiteral("run_summary_%1_%2.html").arg(m_tabKey, stamp), tr("HTML (*.html)"));
        if (path.isEmpty()) return;
        if (!saveHtml(path)) { m_status->fail(tr("Could not write %1").arg(path)); return; }
        m_status->ok(tr("Saved %1").arg(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    rebuild();
}

void RunReportWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_summary = RunReport::summarise(m_model, m_tabKey, m_tabName);
    m_html = RunReport::toHtml(m_summary);
    QApplication::restoreOverrideCursor();
    m_view->setHtml(m_html);
    m_status->state(tr("%1 rows read").arg(m_summary.rows));
}

bool RunReportWindow::saveHtml(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(m_html.toUtf8());
    return file.commit();
}
