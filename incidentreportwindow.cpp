#include "incidentreportwindow.h"

#include "statusline.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QImage>
#include <QRegularExpression>
#include <QTextDocument>
#include <QHBoxLayout>
#include <QPalette>
#include <QPushButton>
#include <QSaveFile>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

IncidentReportWindow::IncidentReportWindow(LogModel *model, const QString &tabKey, const QString &tabName,
                                           qint64 atMs, const IncidentReport::Options &options, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
    , m_tabName(tabName)
    , m_atMs(atMs)
    , m_options(options)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Incident report — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 720);

    m_view = new QTextBrowser(this);
    m_view->setOpenLinks(false);
    QPalette paper = m_view->palette();
    paper.setColor(QPalette::Base, QColor(255, 255, 255));
    paper.setColor(QPalette::Text, QColor(17, 17, 17));
    m_view->setPalette(paper);
    m_status = new StatusLine;

    auto *save = new QPushButton(tr("Save HTML…"), this);
    save->setToolTip(tr("A single file, DMI panels and the speed plot included: opens in any browser, prints to PDF from there"));
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

    connect(rebuildButton, &QPushButton::clicked, this, &IncidentReportWindow::rebuild);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_view->toPlainText());
        m_status->ok(tr("Copied as text"));
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString stamp = QDateTime::fromMSecsSinceEpoch(m_atMs).toString(QStringLiteral("yyyyMMdd_HHmm"));
        const QString path = QFileDialog::getSaveFileName(this, tr("Save incident report"),
            QStringLiteral("incident_%1_%2.html").arg(m_tabKey, stamp), tr("HTML (*.html)"));
        if (path.isEmpty()) return;
        if (!saveHtml(path)) { m_status->fail(tr("Could not write %1").arg(path)); return; }
        m_status->ok(tr("Saved %1").arg(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    rebuild();
}

void IncidentReportWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_summary = IncidentReport::build(m_model, m_tabKey, m_tabName, m_atMs, m_options);
    m_html = IncidentReport::toHtml(m_summary, m_options);
    QApplication::restoreOverrideCursor();
    m_view->setHtml(forViewer(m_html, m_view->document()));
    if (!m_summary.valid) {
        m_status->state(tr("No rows in this window"));
    } else {
        auto count = [](int n, const QString &one, const QString &many) {
            return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
        };
        m_status->state(count(m_summary.rawFrameTotal, tr("raw frame"), tr("raw frames")) + QStringLiteral(" \u00B7 ")
                        + count(m_summary.keyMoments.size(), tr("DMI moment"), tr("DMI moments")));
    }
}

QString IncidentReportWindow::forViewer(const QString &html, QTextDocument *doc)
{
    // The saved file embeds its images as data: URIs, which every browser
    // shows and QTextBrowser does not (session 133: every DMI panel and the
    // plot were blank space in this window). For the viewer only, each one
    // becomes a named resource of the document; the file is left as it is.
    static const QRegularExpression re(QStringLiteral("src=\"data:image/png;base64,([A-Za-z0-9+/=]+)\""));
    QString out;
    out.reserve(html.size());
    int last = 0, n = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QImage img;
        img.loadFromData(QByteArray::fromBase64(m.captured(1).toLatin1()), "PNG");
        const QUrl name(QStringLiteral("dlimg://%1").arg(n++));
        doc->addResource(QTextDocument::ImageResource, name, img);
        out += html.mid(last, m.capturedStart() - last);
        out += QStringLiteral("src=\"%1\"").arg(name.toString());
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

bool IncidentReportWindow::saveHtml(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(m_html.toUtf8());
    return file.commit();
}
