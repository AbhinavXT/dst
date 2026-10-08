#include "iotimelinewindow.h"

#include "faulttimelinewindow.h"
#include "iotimeline.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

IoTimelineWindow::IoTimelineWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Cab inputs and outputs — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 600);

    m_note = new QLabel(tr("One row per DIO input / output that changes in this tab; a bar where it reads other than its "
                           "usual value (a button pressed, the cab switched, traction cut off). Hover a bar for the value."), this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(UiColor::mutedStyle());
    m_canvas = new FaultTimelineCanvas;
    m_canvas->setObjectName(QStringLiteral("ioTimelineCanvas"));
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_canvas);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_status = new StatusLine;
    auto *rebuildBtn = new QPushButton(tr("Rebuild"), this);
    auto *save = new QPushButton(tr("Save image…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildBtn);
    buttons->addWidget(save);
    auto *root = new QVBoxLayout(this);
    root->addWidget(m_note);
    root->addWidget(scroll, 1);
    root->addLayout(buttons);
    connect(rebuildBtn, &QPushButton::clicked, this, &IoTimelineWindow::rebuild);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save cab inputs and outputs"),
                                                          QStringLiteral("cab_io_%1.png").arg(m_tabKey), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (m_canvas->grab().save(path, "PNG")) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(m_canvas, &FaultTimelineCanvas::timeClicked, this, [this](qint64 ms) { emit jumpRequested(m_tabKey, ms); });
    rebuild();
}

void IoTimelineWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_t = IoTimeline::build(m_model);
    QApplication::restoreOverrideCursor();
    m_canvas->setTimeline(m_t);
    if (m_t.rows.isEmpty()) m_status->state(tr("No @dip / @dop that changes in this tab (they arrive over the serial terminal)."));
    else m_status->state(tr("%1 changing signal(s), %2 bar(s)").arg(m_t.rows.size()).arg(m_t.bars.size()));
}
