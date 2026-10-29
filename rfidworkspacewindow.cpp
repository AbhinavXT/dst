#include "rfidworkspacewindow.h"

#include "locosimwindow.h"
#include "rfidtag.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "tageditdialog.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QScrollArea *scrolled(QWidget *inner, QWidget *parent)
{
    auto *s = new QScrollArea(parent);
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    if (inner) s->setWidget(inner);
    return s;
}

}  // namespace

RfidWorkspaceWindow::RfidWorkspaceWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("rfidWorkspace"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    resize(1500, 940);

    // ---- the layout, as a pane ------------------------------------------------
    m_layout = new StationLayoutWindow(nullptr);
    m_layout->setWindowFlags(Qt::Widget);
    m_layout->setAttribute(Qt::WA_DeleteOnClose, false);   // the workspace's, gone with it
    m_layout->setLocoSimulator([this]() { return takeSimulator(); });
    connect(m_layout, &StationLayoutWindow::tagSelected, this, &RfidWorkspaceWindow::onLayoutTag);
    connect(m_layout, &StationLayoutWindow::stationChanged, this, [this]() {
        refreshTitle();
        if (!m_editedTag.isEmpty()) rebuildEditor();   // the tag as it is now (an Undo, a move)
    });
    connect(m_layout, &QWidget::windowTitleChanged, this, [this]() { refreshTitle(); });

    // ---- the simulator's pane: empty until it is taken -------------------------
    m_simPlaceholder = new QLabel(tr("The Loco Simulator comes here: Simulator ▸ Line … in the layout runs a "
                                     "line in it, or Run the simulator below."), this);
    m_simPlaceholder->setWordWrap(true);
    m_simPlaceholder->setAlignment(Qt::AlignCenter);
    m_simPlaceholder->setStyleSheet(UiColor::mutedStyle());
    m_simScroll = scrolled(nullptr, this);
    m_simScroll->setObjectName(QStringLiteral("rfidWorkspaceSimPane"));
    auto *simPane = new QWidget(this);
    auto *simLayout = new QVBoxLayout(simPane);
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->addWidget(m_simPlaceholder, 1);
    auto *takeBtn = new QPushButton(tr("Open the Loco Simulator here"), simPane);
    takeBtn->setObjectName(QStringLiteral("rfidWorkspaceTakeSim"));
    connect(takeBtn, &QPushButton::clicked, this, [this, takeBtn]() {
        if (takeSimulator()) takeBtn->hide();
    });
    simLayout->addWidget(takeBtn, 0, Qt::AlignHCenter);
    simLayout->addWidget(m_simScroll, 1);
    m_simScroll->hide();

    m_left = new QSplitter(Qt::Vertical, this);
    m_left->setObjectName(QStringLiteral("rfidWorkspaceLeft"));
    m_left->addWidget(scrolled(m_layout, this));
    m_left->addWidget(simPane);
    m_left->setStretchFactor(0, 1);
    m_left->setStretchFactor(1, 1);
    m_left->setSizes({ 560, 380 });

    // ---- the tag editor ------------------------------------------------------------
    m_editorHost = new QWidget(this);
    m_editorHost->setObjectName(QStringLiteral("rfidWorkspaceEditor"));
    m_editorLayout = new QVBoxLayout(m_editorHost);
    m_editorHead = new QLabel(tr("Pick a tag in the drawing, the Tags table or the simulator's Tags: its fields come here."),
                              m_editorHost);
    m_editorHead->setObjectName(QStringLiteral("rfidWorkspaceEditorHead"));
    m_editorHead->setWordWrap(true);
    m_editorLayout->addWidget(m_editorHead);
    m_editorLayout->addStretch(1);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_left);
    split->addWidget(scrolled(m_editorHost, this));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({ 1110, 390 });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->addWidget(split);
    refreshTitle();
}

RfidWorkspaceWindow::~RfidWorkspaceWindow()
{
    giveBackSimulator();
}

LocoSimWindow *RfidWorkspaceWindow::takeSimulator()
{
    if (m_sim) return m_sim;
    LocoSimWindow *sim = m_takeSim ? m_takeSim() : nullptr;
    if (!sim) return nullptr;
    m_sim = sim;
    m_simFlags = sim->windowFlags();
    m_simScroll->setWidget(sim);   // reparents it; then a widget, not a window
    sim->setWindowFlags(Qt::Widget);
    sim->show();
    m_simPlaceholder->hide();
    m_simScroll->show();
    if (auto *take = findChild<QPushButton *>(QStringLiteral("rfidWorkspaceTakeSim"))) take->hide();
    // Its RFID tab: the route's tags, the one in step with the layout.
    for (QTabWidget *tabs : sim->findChildren<QTabWidget *>(QString(), Qt::FindDirectChildrenOnly))
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == LocoSimWindow::tr("RFID")) tabs->setCurrentIndex(i);
    connect(sim, &LocoSimWindow::routeTagSelected, this, &RfidWorkspaceWindow::onSimTag);
    if (!m_editedTag.isEmpty()) sim->selectRouteTag(m_editedTag);
    return sim;
}

void RfidWorkspaceWindow::giveBackSimulator()
{
    if (!m_sim) return;
    LocoSimWindow *sim = m_sim;
    m_sim = nullptr;
    disconnect(sim, &LocoSimWindow::routeTagSelected, this, nullptr);
    m_simScroll->takeWidget();
    sim->setParent(parentWidget(), m_simFlags);   // MainWindow's, its own window again
    sim->hide();
}

void RfidWorkspaceWindow::onLayoutTag(const QString &name)
{
    if (m_syncing) return;
    m_syncing = true;
    showTag(name);
    if (m_sim) m_sim->selectRouteTag(name);
    m_syncing = false;
}

void RfidWorkspaceWindow::onSimTag(const QString &name)
{
    if (m_syncing) return;
    m_syncing = true;
    m_layout->selectTags({ name });   // the drawing follows the table
    showTag(name);
    m_syncing = false;
}

void RfidWorkspaceWindow::showTag(const QString &name)
{
    if (name == m_editedTag && m_editor) return;
    m_editedTag = name;
    rebuildEditor();
}

void RfidWorkspaceWindow::rebuildEditor()
{
    if (m_editor) {
        m_editorLayout->removeWidget(m_editor);
        m_editor->deleteLater();
        m_editor = nullptr;
    }
    QByteArray bytes;
    for (const StationLayout::Tag &t : m_layout->station().tags)
        if (t.name == m_editedTag) bytes = RfidTag::fromPages(t.pageX, t.pageY);
    if (m_editedTag.isEmpty() || bytes.size() != RfidTag::TagBytes) {
        m_editorHead->setText(m_editedTag.isEmpty()
                                  ? tr("Pick a tag in the drawing, the Tags table or the simulator's Tags: its fields come here.")
                                  : tr("Tag %1 is not in the layout (or its page_x / page_y make no tag).").arg(m_editedTag));
        return;
    }
    m_editorHead->setText(tr("<b>Tag %1</b> · Apply puts it in the layout (Undo there); a line the simulator runs "
                             "from the layout follows.").arg(m_editedTag.toHtmlEscaped()));
    auto *editor = new TagEditDialog(bytes, tr("Tag %1").arg(m_editedTag), tr("Apply to the layout"), m_editorHost);
    editor->setWindowFlags(Qt::Widget);
    if (QPushButton *ok = editor->okButton()) ok->setObjectName(QStringLiteral("rfidWorkspaceApply"));
    // Accepted: into the layout. Rejected (Cancel, Esc): the layout's tag again.
    connect(editor, &QDialog::accepted, this, [this]() { applyEditor(); });
    connect(editor, &QDialog::rejected, this, [this]() { rebuildEditor(); }, Qt::QueuedConnection);
    m_editorLayout->insertWidget(1, editor);
    editor->show();
    m_editor = editor;
}

LocoSimWindow *RfidWorkspaceWindow::simulator() const
{
    return m_sim;
}

TagEditDialog *RfidWorkspaceWindow::editor() const
{
    return m_editor;
}

bool RfidWorkspaceWindow::applyEditor()
{
    if (!m_editor || m_editedTag.isEmpty()) return false;
    const QByteArray bytes = m_editor->tag();
    const bool ok = m_layout->applyTag(m_editedTag, bytes);
    // Its name may change with its fields (the unique id): follow it.
    if (ok) m_editedTag = RfidTag::nameOf(bytes);
    QMetaObject::invokeMethod(this, [this]() { rebuildEditor(); }, Qt::QueuedConnection);
    return ok;
}

void RfidWorkspaceWindow::refreshTitle()
{
    // The layout's own title is "Station Layout — <file>[ *]": the file part.
    QString file = m_layout ? m_layout->windowTitle() : QString();
    const int dash = file.indexOf(QStringLiteral(" \u2014 "));
    if (dash >= 0) file = file.mid(dash + 3);
    setWindowTitle(file.isEmpty() ? tr("RFID Workspace") : tr("RFID Workspace · %1").arg(file));
}

void RfidWorkspaceWindow::closeEvent(QCloseEvent *e)
{
    // The layout asks first when it holds edits not saved.
    if (!m_layout->close()) {
        e->ignore();
        return;
    }
    giveBackSimulator();
    e->accept();
}
