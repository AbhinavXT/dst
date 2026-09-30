#include "tabpopoutwindow.h"

#include "logentry.h"
#include "logmodel.h"
#include "logtableview.h"
#include "messagedispatcher.h"
#include "tabtags.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

TabPopoutWindow::TabPopoutWindow(const QString &key, const QString &friendlyName, LogModel *model,
                                 MessageDispatcher *dispatcher, QWidget *parent)
    : QMainWindow(parent)
    , m_key(key)
    , m_friendlyName(friendlyName)
{
    WindowGeometry::makeResizableWindow(this);
    resize(900, 560);
    WindowGeometry::restore(this, geometryKey(key));

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- header: tag dot, name, follow, back to the main window -----------------
    auto *header = new QHBoxLayout();
    m_dot = new QLabel(central);
    header->addWidget(m_dot);
    m_title = new QLabel(central);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    header->addWidget(m_title);
    auto *copyNote = new QLabel(tr("live copy of the tab"), central);
    copyNote->setStyleSheet(UiColor::mutedStyle());
    header->addWidget(copyNote);
    header->addStretch(1);
    m_follow = new QCheckBox(tr("Follow latest"), central);
    m_follow->setToolTip(tr("Keep the newest row in view as messages arrive"));
    header->addWidget(m_follow);
    auto *showInMain = new QPushButton(tr("Show in main window"), central);
    showInMain->setToolTip(tr("Bring the main window forward on this tab"));
    connect(showInMain, &QPushButton::clicked, this, [this]() { emit showInMainRequested(m_key); });
    header->addWidget(showInMain);
    layout->addLayout(header);

    // ---- the table: the tab's own model, the tabs' own look ------------------------
    m_view = new QTableView(central);
    m_view->setModel(model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->horizontalHeader()->setStretchLastSection(true);
    // Same density (row height, scaled with the text size), hidden columns
    // and widths as the tabs -- DetachConsoleDialog used a fixed 20 px.
    LogTableView::applyDensity(m_view);
    LogTableView::applyColumnVisibility(m_view);
    LogTableView::applyColumnWidths(m_view);
    layout->addWidget(m_view, 1);
    setCentralWidget(central);

    if (dispatcher != nullptr) {
        connect(dispatcher, &MessageDispatcher::entryAppended, this, &TabPopoutWindow::onEntryAppended);
    }
    connect(TabTags::instance(), &TabTags::changed, this, [this](const QString &changedKey) {
        if (changedKey.isEmpty() || changedKey == m_key) {
            refreshTag();
        }
    });
    UiColor::onThemeChange(this, [this]() { refreshTag(); });
    refreshTag();
}

QString TabPopoutWindow::geometryKey(const QString &key)
{
    return QStringLiteral("popout_") + key;
}

void TabPopoutWindow::setFollowLatest(bool on)
{
    m_follow->setChecked(on);
    if (on) {
        m_view->scrollToBottom();
    }
}

bool TabPopoutWindow::followLatest() const
{
    return m_follow->isChecked();
}

void TabPopoutWindow::refreshTag()
{
    const QString name = TabTags::instance()->decoratedName(m_key, m_friendlyName);
    m_title->setText(name);
    setWindowTitle(name);
    const QIcon dot = TabTags::instance()->dotIcon(m_key);
    // The window icon is also what the minimise chip shows.
    if (dot.isNull()) {
        setWindowIcon(QIcon());
        m_dot->clear();
        m_dot->hide();
    } else {
        setWindowIcon(dot);
        m_dot->setPixmap(dot.pixmap(16, 16));
        m_dot->show();
    }
}

void TabPopoutWindow::onEntryAppended(QString tabKey, QSharedPointer<LogEntry>)
{
    if (tabKey == m_key && m_follow->isChecked()) {
        m_view->scrollToBottom();
    }
}

void TabPopoutWindow::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, geometryKey(m_key));
    emit closedByUser(m_key);
    QMainWindow::closeEvent(event);
}
