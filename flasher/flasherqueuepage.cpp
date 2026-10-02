#include "flasherqueuepage.h"

#include "flasherqueuemodel.h"
#include "flasherstyle.h"
#include "statusline.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QEvent>
#include <QStyle>
#include <QSpinBox>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// How long to wait after a file-changed notification before re-hashing. A
// build writes an image in several chunks and some tools delete-and-rename,
// so the first notification usually arrives before the file is complete.
const int kRehashDebounceMs = 400;

// Make a panel: a QFrame with a name the style sheet can address.
QFrame *makeCard(QWidget *parent, const QString &objectName, QList<QFrame *> *registry)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(objectName);
    registry->append(frame);
    return frame;
}

}  // namespace

// =============================================================================
//  FlasherRouteDiagram
// =============================================================================

FlasherRouteDiagram::FlasherRouteDiagram(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(sizeHint().height());
    UiColor::onThemeChange(this, [this]() { update(); });
}

void FlasherRouteDiagram::setIncludedCards(const QSet<int> &cardTypes)
{
    if (m_included == cardTypes) {
        return;
    }
    m_included = cardTypes;
    update();
}

namespace {
// The gap between tile rows, which the connector lines are drawn in. Never
// derived from whatever height the layout hands the widget: that is how
// the VCC tile came to sit on top of the IOA row (session 80) — a short
// window squeezed the left column, (height - 3 tiles) / 2 went negative,
// and the rows overlapped.
int routeGap(int lineHeight) { return qMax(14, lineHeight); }
int routeTileHeight(int lineHeight) { return lineHeight * 2 + 10; }
}  // namespace

QSize FlasherRouteDiagram::sizeHint() const
{
    const int lineHeight = fontMetrics().height();
    // Three rows of tiles (PC, VCC, IOA) with a connector gap between each.
    return QSize(260, routeTileHeight(lineHeight) * 3 + routeGap(lineHeight) * 2 + 2);
}

QSize FlasherRouteDiagram::minimumSizeHint() const
{
    // The layout may not shrink it below this: the left column scrolls
    // instead (buildLeftColumn), so every row keeps its own space.
    return sizeHint();
}

void FlasherRouteDiagram::changeEvent(QEvent *event)
{
    // Text size (Ctrl +/-) changes the font, and with it the height needed.
    if (event->type() == QEvent::FontChange) {
        setMinimumHeight(sizeHint().height());
        updateGeometry();
    }
    QWidget::changeEvent(event);
}

void FlasherRouteDiagram::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor inkColor = palette().color(QPalette::Text);
    const QColor mutedColor = FlasherStyle::muted();
    const QColor lineColor = FlasherStyle::border();
    const QColor primaryColor = FlasherStyle::primary();

    const int lineHeight = fontMetrics().height();
    int tileHeight = routeTileHeight(lineHeight);
    const int gapHeight = routeGap(lineHeight);
    // If it is ever given less than it asked for, the tiles shrink — they
    // never overlap. Below two lines of text the caption is dropped.
    const int available = height() - 2 - gapHeight * 2;
    if (available < tileHeight * 3) {
        tileHeight = qMax(lineHeight + 4, available / 3);
    }
    const bool withCaption = tileHeight >= lineHeight * 2 + 4;
    const int fullWidth = width() - 2;

    // Draw one tile: rounded box, bold name, muted caption. `included` false
    // gives the dashed outline the design uses for cards not in this batch.
    auto drawTile = [&](const QRect &rect, const QString &name, const QString &caption,
                        bool included, bool emphasised) {
        QPainterPath path;
        path.addRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        if (included) {
            if (emphasised) {
                painter.fillPath(path, FlasherStyle::tint(primaryColor, 0.10));
            } else {
                painter.fillPath(path, palette().color(QPalette::Base));
            }
            QPen pen(lineColor);
            if (emphasised) {
                pen.setColor(FlasherStyle::tint(primaryColor, 0.55));
            }
            painter.setPen(pen);
        } else {
            QPen pen(lineColor);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
        }
        painter.drawPath(path);

        QFont nameFont = font();
        nameFont.setBold(true);
        painter.setFont(nameFont);
        if (included) {
            painter.setPen(inkColor);
        } else {
            painter.setPen(mutedColor);
        }
        if (!withCaption) {
            painter.drawText(rect, Qt::AlignCenter, name);
            painter.setFont(font());
            return;
        }
        painter.drawText(rect.adjusted(4, 4, -4, -rect.height() / 2), Qt::AlignCenter, name);
        painter.setFont(font());
        painter.setPen(mutedColor);
        painter.drawText(rect.adjusted(4, rect.height() / 2, -4, -4), Qt::AlignCenter, caption);
    };

    // Row 1: This PC
    const int pcWidth = fullWidth / 2;
    const QRect pcRect((fullWidth - pcWidth) / 2, 0, pcWidth, tileHeight);
    drawTile(pcRect, tr("This PC"), tr("UDP"), true, false);

    // Row 2: VCC. Always drawn solid: IOA images travel through it even when
    // the VCC itself is not being flashed. Emphasised when it is the target.
    const QRect vccRect((fullWidth - pcWidth) / 2, tileHeight + gapHeight, pcWidth, tileHeight);
    drawTile(vccRect, tr("VCC · slot 1"), Flasher::cardIdText(Flasher::CardVcc), true,
             m_included.contains(Flasher::CardVcc));

    // Row 3: the three IOA cards.
    const int ioaTop = (tileHeight + gapHeight) * 2;
    const int ioaGap = 6;
    const int ioaWidth = (fullWidth - ioaGap * 2) / 3;
    const int ioaCards[] = { Flasher::CardInput, Flasher::CardOutput, Flasher::CardAnalog };
    QVector<QRect> ioaRects;
    for (int position = 0; position < 3; ++position) {
        const QRect rect(position * (ioaWidth + ioaGap), ioaTop, ioaWidth, tileHeight);
        ioaRects.append(rect);
        const int cardType = ioaCards[position];
        drawTile(rect, Flasher::cardShortName(cardType), Flasher::cardIdText(cardType),
                 m_included.contains(cardType), m_included.contains(cardType));
    }

    // Connectors: PC -> VCC, and VCC -> each IOA card.
    QPen connectorPen(lineColor);
    connectorPen.setWidthF(1.5);
    painter.setPen(connectorPen);
    painter.drawLine(QPoint(pcRect.center().x(), pcRect.bottom() + 1),
                     QPoint(vccRect.center().x(), vccRect.top() - 1));
    for (int position = 0; position < ioaRects.size(); ++position) {
        QPen pen(connectorPen);
        if (!m_included.contains(ioaCards[position])) {
            pen.setStyle(Qt::DashLine);
        }
        painter.setPen(pen);
        painter.drawLine(QPoint(vccRect.center().x(), vccRect.bottom() + 1),
                         QPoint(ioaRects[position].center().x(), ioaRects[position].top() - 1));
    }
}

// =============================================================================
//  FlasherQueuePage
// =============================================================================

FlasherQueuePage::FlasherQueuePage(QWidget *parent)
    : QWidget(parent)
{
    m_model = new FlasherQueueModel(this);

    auto *pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(16, 12, 16, 12);   // 12 top and bottom (session 146): height for Linux fonts
    pageLayout->setSpacing(16);
    // The left column scrolls rather than squeezing its cards when the
    // window is short: squeezed, the route diagram overlapped itself and
    // the pre-flight list was cut off mid-sentence (session 80).
    QWidget *leftColumn = buildLeftColumn();
    auto *leftScroll = new QScrollArea(this);
    leftScroll->setObjectName(QStringLiteral("flasherLeftScroll"));
    leftScroll->setWidget(leftColumn);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->viewport()->setAutoFillBackground(false);
    leftColumn->setAutoFillBackground(false);
    leftScroll->setFixedWidth(leftColumn->width()
                              + leftScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2);
    pageLayout->addWidget(leftScroll);
    pageLayout->addWidget(buildQueueColumn(), 1);

    // ---- file watching -----------------------------------------------------
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &FlasherQueuePage::onFileChangedOnDisk);
    m_rehashTimer = new QTimer(this);
    m_rehashTimer->setSingleShot(true);
    m_rehashTimer->setInterval(kRehashDebounceMs);
    connect(m_rehashTimer, &QTimer::timeout, this, &FlasherQueuePage::rehashChangedFiles);

    // ---- anything that can change readiness re-runs the checklist ----------
    connect(m_model, &FlasherQueueModel::queueChanged, this, &FlasherQueuePage::refreshPreflight);
    connect(m_model, &FlasherQueueModel::queueChanged, this, &FlasherQueuePage::updateWatcher);
    connect(m_model, &FlasherQueueModel::queueChanged, this, &FlasherQueuePage::updateDetailStrip);
    connect(m_model, &FlasherQueueModel::fileDropped, this, &FlasherQueuePage::loadImageAndSelect);
    connect(m_ipEdit, &QLineEdit::textChanged, this, [this]() {
        preselectAdapterForTarget();
        refreshPreflight();
        emit targetChanged();
    });
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        refreshPreflight();
        emit targetChanged();
    });
    connect(m_adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FlasherQueuePage::refreshPreflight);

    UiColor::onThemeChange(this, [this]() { restyle(); });
    restyle();
    refreshAdapters();
    refreshPreflight();
    updateDetailStrip();
}

QWidget *FlasherQueuePage::buildLeftColumn()
{
    auto *column = new QWidget(this);
    column->setFixedWidth(300);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(16);

    // ---- Target --------------------------------------------------------------
    QFrame *targetCard = makeCard(column, QStringLiteral("flasherTargetCard"), &m_cards);
    auto *targetLayout = new QVBoxLayout(targetCard);
    targetLayout->setContentsMargins(18, 16, 18, 18);
    targetLayout->setSpacing(8);
    auto *targetTitle = new QLabel(tr("Target"), targetCard);
    m_sectionLabels.append(targetTitle);
    targetLayout->addWidget(targetTitle);

    auto *ipRow = new QHBoxLayout();
    auto *ipColumn = new QVBoxLayout();
    ipColumn->addWidget(new QLabel(tr("VCC IP address"), targetCard));
    m_ipEdit = new QLineEdit(targetCard);
    m_ipEdit->setFont(UiStyle::monoFont());
    m_ipEdit->setPlaceholderText(QStringLiteral("192.168.25.168"));
    m_ipEdit->setMinimumHeight(34);
    ipColumn->addWidget(m_ipEdit);
    ipRow->addLayout(ipColumn, 1);

    auto *portColumn = new QVBoxLayout();
    portColumn->addWidget(new QLabel(tr("Port"), targetCard));
    m_portSpin = new QSpinBox(targetCard);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(50001);
    m_portSpin->setFont(UiStyle::monoFont());
    m_portSpin->setMinimumHeight(34);
    m_portSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_portSpin->setFixedWidth(80);
    portColumn->addWidget(m_portSpin);
    ipRow->addLayout(portColumn);
    targetLayout->addLayout(ipRow);

    auto *adapterHeader = new QHBoxLayout();
    adapterHeader->addWidget(new QLabel(tr("Send from adapter"), targetCard), 1);
    auto *refreshAdaptersButton = new QToolButton(targetCard);
    refreshAdaptersButton->setText(QStringLiteral("↻"));
    refreshAdaptersButton->setToolTip(tr("Re-read the network adapters (after plugging in a cable)"));
    connect(refreshAdaptersButton, &QToolButton::clicked, this, &FlasherQueuePage::refreshAdapters);
    adapterHeader->addWidget(refreshAdaptersButton);
    targetLayout->addLayout(adapterHeader);

    m_adapterCombo = new QComboBox(targetCard);
    m_adapterCombo->setMinimumHeight(34);
    // Honest about what this control does. The engine opens an unbound UDP
    // socket, so Windows picks the outgoing adapter from its routing table;
    // the picker cannot force a different one. What it does do is check that
    // an adapter on the board's subnet exists and is up, which is the thing
    // that is actually wrong when a flash never gets a reply.
    m_adapterCombo->setToolTip(tr("The adapter this PC should reach the board through.\n"
                                  "The operating system chooses the route; this is checked\n"
                                  "against the board's subnet in the pre-flight list."));
    targetLayout->addWidget(m_adapterCombo);
    columnLayout->addWidget(targetCard);

    // Pre-flight before the route (session 134): it is what says whether
    // Flash will work, and under the route diagram it sat below the fold.
    // ---- Pre-flight ----------------------------------------------------------
    QFrame *preflightCard = makeCard(column, QStringLiteral("flasherPreflightCard"), &m_cards);
    auto *preflightOuter = new QVBoxLayout(preflightCard);
    preflightOuter->setContentsMargins(18, 16, 18, 18);
    preflightOuter->setSpacing(8);
    auto *preflightTitle = new QLabel(tr("Pre-flight"), preflightCard);
    m_sectionLabels.append(preflightTitle);
    preflightOuter->addWidget(preflightTitle);
    m_preflightLayout = new QVBoxLayout();
    m_preflightLayout->setSpacing(6);
    preflightOuter->addLayout(m_preflightLayout);
    preflightOuter->addStretch(1);
    columnLayout->addWidget(preflightCard);

    // ---- Delivery route ------------------------------------------------------
    QFrame *routeCard = makeCard(column, QStringLiteral("flasherRouteCard"), &m_cards);
    auto *routeLayout = new QVBoxLayout(routeCard);
    routeLayout->setContentsMargins(18, 16, 18, 18);
    routeLayout->setSpacing(10);
    auto *routeTitle = new QLabel(tr("Delivery route"), routeCard);
    m_sectionLabels.append(routeTitle);
    routeLayout->addWidget(routeTitle);
    m_route = new FlasherRouteDiagram(routeCard);
    routeLayout->addWidget(m_route);
    auto *routeNote = new QLabel(tr("IOA images are sent to the VCC, which relays them by card ID. "
                                    "One chassis at a time."), routeCard);
    routeNote->setWordWrap(true);
    routeNote->setStyleSheet(UiColor::mutedStyle());
    routeLayout->addWidget(routeNote);
    columnLayout->addWidget(routeCard);
    columnLayout->addStretch(1);

    return column;
}

QWidget *FlasherQueuePage::buildQueueColumn()
{
    auto *column = new QWidget(this);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    // 10, not 16 (session 146): with Linux's fonts the window was 709 px
    // tall at its minimum, over the 700 a 768-px laptop leaves.
    columnLayout->setSpacing(10);

    QFrame *queueCard = makeCard(column, QStringLiteral("flasherQueueCard"), &m_cards);
    auto *queueLayout = new QVBoxLayout(queueCard);
    queueLayout->setContentsMargins(0, 12, 0, 0);
    queueLayout->setSpacing(6);

    // ---- title row ---------------------------------------------------------------
    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(20, 0, 20, 0);
    auto *titleColumn = new QVBoxLayout();
    auto *title = new QLabel(tr("Flash queue"), queueCard);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.35, true));
    titleColumn->addWidget(title);
    // One line at any width the window allows (session 134): wrapped to two,
    // its extra height was not in the card's minimum and the detail strip
    // landed on the table's last row. The full procedure is in the action
    // bar beside Flash.
    auto *subtitle = new QLabel(tr("One card per run: select it, press Flash, power-cycle"), queueCard);
    subtitle->setStyleSheet(UiColor::mutedStyle());
    subtitle->setWordWrap(true);
    titleColumn->addWidget(subtitle);
    titleRow->addLayout(titleColumn, 1);
    auto *loadFolderButton = new QPushButton(tr("Load images from folder…"), queueCard);
    loadFolderButton->setMinimumHeight(34);
    loadFolderButton->setToolTip(tr("Search a build folder (and its subfolders) for .appimage files\n"
                                    "and put each one on the card its name matches."));
    connect(loadFolderButton, &QPushButton::clicked, this, &FlasherQueuePage::loadImagesFromFolder);
    titleRow->addWidget(loadFolderButton);
    queueLayout->addLayout(titleRow);

    // ---- the table -------------------------------------------------------------
    m_table = new QTableView(queueCard);
    m_table->setObjectName(QStringLiteral("flasherQueueTable"));
    m_table->setModel(m_model);
    m_table->setItemDelegate(new FlasherQueueDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->hide();
    m_table->setWordWrap(false);
    // Files can be dropped onto a row; rows themselves are not dragged.
    m_table->setDragEnabled(false);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::DropOnly);
    m_table->setDefaultDropAction(Qt::CopyAction);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMouseTracking(true);

    QHeaderView *header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(FlasherQueueModel::ColumnImage, QHeaderView::Stretch);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setColumnHidden(FlasherQueueModel::ColumnHandle, true);
    // Every fixed column measured to what it holds (session 134). At pixel
    // widths (180 / 110 / 120 / 190) they left the Image column ~100 px in
    // a 1100-px window: "KAV…age".
    {
        const QFontMetrics fm(m_table->font());
        QFont bold = m_table->font();
        bold.setBold(true);
        const QFontMetrics bfm(bold);
        const QFontMetrics mono(UiStyle::monoFont());
        QFont badgeFont = bold;
        badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.92);
        const QFontMetrics bm(badgeFont);
        auto header = [&](int col) {
            return bfm.horizontalAdvance(m_model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString()) + 24;
        };
        int card = 0;
        for (int c : { Flasher::CardVcc, Flasher::CardInput, Flasher::CardOutput, Flasher::CardAnalog })
            card = qMax(card, bfm.horizontalAdvance(Flasher::cardName(c)));
        // The check circle and its gap come first in that column.
        m_table->setColumnWidth(FlasherQueueModel::ColumnCard, qMax(header(FlasherQueueModel::ColumnCard), card + 26 + 10 + 24));
        m_table->setColumnWidth(FlasherQueueModel::ColumnSize,
                                qMax(header(FlasherQueueModel::ColumnSize),
                                     qMax(fm.horizontalAdvance(QStringLiteral("888.8 KB")),
                                          fm.horizontalAdvance(QStringLiteral("8888 blocks"))) + 24));
        m_table->setColumnWidth(FlasherQueueModel::ColumnCrc,
                                qMax(header(FlasherQueueModel::ColumnCrc), mono.horizontalAdvance(QStringLiteral("0x88888888")) + 24));
        int badge = 0;
        for (const QString &t : { tr("Name matches"), tr("Name doesn't say"), tr("Unreadable") })
            badge = qMax(badge, bm.horizontalAdvance(t));
        for (int c : { Flasher::CardVcc, Flasher::CardInput, Flasher::CardOutput, Flasher::CardAnalog })
            badge = qMax(badge, bm.horizontalAdvance(QStringLiteral("Looks like %1").arg(Flasher::cardShortName(c))));
        m_table->setColumnWidth(FlasherQueueModel::ColumnCheck, qMax(header(FlasherQueueModel::ColumnCheck), badge + 20 + 24));
    }
    m_table->setColumnWidth(FlasherQueueModel::ColumnRemove, 32);
    // Their values are on the Image cell's second line (FlasherQueueModel).
    m_table->setColumnHidden(FlasherQueueModel::ColumnSize, true);
    m_table->setColumnHidden(FlasherQueueModel::ColumnCrc, true);
    const int rowHeight = fontMetrics().height() * 2 + 16;   // all four cards in view, in less height
    m_table->verticalHeader()->setDefaultSectionSize(rowHeight);
    m_table->setMinimumHeight(rowHeight * 4 + header->sizeHint().height() + 4);

    connect(m_table, &QTableView::clicked, this, &FlasherQueuePage::onTableClicked);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &FlasherQueuePage::onSelectionChanged);
    queueLayout->addWidget(m_table, 1);

    // ---- detail strip for the selected row -----------------------------------
    m_detailStrip = new QFrame(queueCard);
    m_detailStrip->setObjectName(QStringLiteral("flasherDetailStrip"));
    auto *detailLayout = new QGridLayout(m_detailStrip);
    detailLayout->setContentsMargins(20, 8, 20, 10);
    detailLayout->setHorizontalSpacing(16);
    detailLayout->setVerticalSpacing(2);
    m_detailTitle = new QLabel(m_detailStrip);
    m_detailTitle->setFont(FlasherStyle::scaledFont(m_detailTitle->font(), 1.0, true));
    detailLayout->addWidget(m_detailTitle, 0, 0, 1, 2);

    auto addDetailRow = [this, detailLayout](int row, const QString &label) -> QLabel * {
        auto *key = new QLabel(label, m_detailStrip);
        key->setStyleSheet(UiColor::mutedStyle());
        detailLayout->addWidget(key, row, 0, Qt::AlignTop);
        auto *value = new QLabel(m_detailStrip);
        value->setFont(UiStyle::monoFont());
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        detailLayout->addWidget(value, row, 1);
        return value;
    };
    m_detailPath = addDetailRow(1, tr("Full path"));
    m_detailModified = addDetailRow(2, tr("Modified"));
    // The hash on a line of its own, across the strip (session 134): 64 hex
    // digits have nowhere to wrap, and beside its key it alone made the
    // queue at least 1185 px wide. Kept whole rather than grouped, so a
    // copy compares cleanly; the key says what it is.
    m_detailSha = addDetailRow(3, tr("SHA-256"));
    {
        QLayoutItem *keyItem = detailLayout->itemAtPosition(3, 0);
        QWidget *key = keyItem ? keyItem->widget() : nullptr;
        detailLayout->removeWidget(m_detailSha);
        if (key) {
            detailLayout->removeWidget(key);
            detailLayout->addWidget(key, 3, 0, 1, 2);
        }
        detailLayout->addWidget(m_detailSha, 4, 0, 1, 2);
    }
    m_detailSha->setWordWrap(false);
    // A size smaller (session 146): in Linux's wider mono the 64 digits made
    // the window 1144 px wide at its minimum.
    {
        QFont shaFont = UiStyle::monoFont();
        shaFont.setPointSizeF(shaFont.pointSizeF() * 0.88);
        m_detailSha->setFont(shaFont);
    }
    m_detailModified->setFont(font());
    detailLayout->setColumnStretch(1, 1);
    queueLayout->addWidget(m_detailStrip);

    columnLayout->addWidget(queueCard, 1);

    // ---- status line (folder loads, unreadable files) ------------------------
    m_status = new StatusLine(column);
    columnLayout->addWidget(m_status);

    // ---- action bar ------------------------------------------------------------
    QFrame *actionBar = makeCard(column, QStringLiteral("flasherActionBar"), &m_cards);
    auto *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(20, 8, 20, 8);
    actionLayout->setSpacing(16);

    // The procedure, stated where the button is. The updater listens for
    // only 5 s after power-on, so the flasher starts sending first and the
    // operator power-cycles while it waits.
    m_procedureNote = new QLabel(actionBar);
    m_procedureNote->setWordWrap(true);
    m_procedureNote->setStyleSheet(UiColor::mutedStyle());
    actionLayout->addWidget(m_procedureNote, 1);

    m_blockerLabel = new QLabel(actionBar);
    m_blockerLabel->setObjectName(QStringLiteral("flasherBlocker"));
    m_blockerLabel->setWordWrap(true);
    m_blockerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    actionLayout->addWidget(m_blockerLabel, 1);

    m_flashButton = new QPushButton(actionBar);
    m_flashButton->setObjectName(QStringLiteral("flasherPrimaryButton"));
    m_flashButton->setMinimumHeight(46);
    m_flashButton->setMinimumWidth(170);
    m_flashButton->setDefault(true);
    connect(m_flashButton, &QPushButton::clicked, this, &FlasherQueuePage::flashRequested);
    actionLayout->addWidget(m_flashButton);

    columnLayout->addWidget(actionBar);
    return column;
}

void FlasherQueuePage::restyle()
{
    for (QFrame *card : m_cards) {
        card->setStyleSheet(FlasherStyle::cardSheet(card->objectName()));
    }
    for (QLabel *label : m_sectionLabels) {
        FlasherStyle::makeSectionLabel(label);
    }
    // The detail strip is the design's "panel tint": the alternate-row colour,
    // with a rule above it to separate it from the table.
    const QPalette pal = palette();
    m_detailStrip->setStyleSheet(
        QStringLiteral("QFrame#flasherDetailStrip { background-color:%1; border-top:1px solid %2;"
                       " border-bottom-left-radius:10px; border-bottom-right-radius:10px; }")
            .arg(pal.color(QPalette::AlternateBase).name(),
                 FlasherStyle::border().name(QColor::HexArgb)));
    m_table->setStyleSheet(QStringLiteral("QTableView#flasherQueueTable { background: transparent; }"));

    // The primary button: accent fill, Base-coloured text. Accent passes
    // 4.5:1 against Base in both themes (contrastaudit), so the reverse holds.
    const QColor accent = FlasherStyle::primary();
    const QColor baseColor = pal.color(QPalette::Base);
    m_flashButton->setStyleSheet(
        QStringLiteral("QPushButton#flasherPrimaryButton { background-color:%1; color:%2;"
                       " border:none; border-radius:8px; padding:0 22px; font-weight:600; }"
                       "QPushButton#flasherPrimaryButton:disabled { background-color:%3; color:%4; }")
            .arg(accent.name(), baseColor.name(),
                 pal.color(QPalette::AlternateBase).name(),
                 pal.color(QPalette::Disabled, QPalette::Text).name()));
    refreshPreflight();
}

// -----------------------------------------------------------------------------
//  Mode and profile
// -----------------------------------------------------------------------------

void FlasherQueuePage::setMode(Flasher::Mode mode)
{
    m_mode = mode;
    refreshPreflight();
}

void FlasherQueuePage::applyProfile(const Flasher::FlashProfile &profile)
{
    m_profile = profile;
    {
        // One preflight refresh at the end, not one per field.
        const QSignalBlocker ipBlocker(m_ipEdit);
        const QSignalBlocker portBlocker(m_portSpin);
        m_ipEdit->setText(profile.vccIp);
        m_portSpin->setValue(profile.port);
    }
    preselectAdapterForTarget();

    for (int cardType : Flasher::defaultCardOrder()) {
        const int rowIndex = m_model->rowForCard(cardType);
        const QString defaultPath = profile.defaultImages.value(cardType);
        if (defaultPath.isEmpty()) {
            m_model->clearImage(rowIndex);
        } else {
            loadImageIntoRow(rowIndex, defaultPath);
        }
    }
    refreshPreflight();
    emit targetChanged();
}

// -----------------------------------------------------------------------------
//  Reading the page
// -----------------------------------------------------------------------------

QString FlasherQueuePage::vccIp() const
{
    return m_ipEdit->text().trimmed();
}

quint16 FlasherQueuePage::port() const
{
    return static_cast<quint16>(m_portSpin->value());
}

QString FlasherQueuePage::adapterDescription() const
{
    return m_adapterCombo->currentText();
}

quint32 FlasherQueuePage::selectedAdapterIp() const
{
    const QVariantList entry = m_adapterCombo->currentData().toList();
    if (entry.size() != 2) {
        return 0;
    }
    return entry.at(0).toUInt();
}

quint32 FlasherQueuePage::selectedAdapterMask() const
{
    const QVariantList entry = m_adapterCombo->currentData().toList();
    if (entry.size() != 2) {
        return 0;
    }
    return entry.at(1).toUInt();
}

Flasher::PreflightReport FlasherQueuePage::preflight() const
{
    Flasher::PreflightInput input;
    input.mode = m_mode;
    input.vccIp = vccIp();
    input.port = port();

    uint32_t targetIp = 0;
    const bool targetParsed = kflash::parse_ipv4(input.vccIp.toStdString(), &targetIp);
    const quint32 adapterIp = selectedAdapterIp();
    const quint32 adapterMask = selectedAdapterMask();
    input.adapterDescription = adapterDescription();
    if (targetParsed) {
        input.adapterOnSubnet = Flasher::sameSubnet(adapterIp, adapterMask, targetIp);
        // Describe the subnet the TARGET is on, with the adapter's mask when
        // there is one and a /24 otherwise (the usual Kavach bench layout).
        quint32 maskForText = adapterMask;
        if (maskForText == 0 || !input.adapterOnSubnet) {
            maskForText = 0xFFFFFF00u;
        }
        input.subnetText = Flasher::subnetDescription(targetIp, maskForText);
    }
    input.updaterWaitSeconds = m_profile.updaterWaitSeconds;

    for (const FlasherQueueRow &row : m_model->rows()) {
        Flasher::PreflightRow preflightRow;
        preflightRow.cardType = row.cardType;
        preflightRow.ticked = row.ticked;
        preflightRow.hasPath = row.hasImage();
        preflightRow.loaded = row.image.loaded;
        preflightRow.sizeBytes = row.image.sizeBytes;
        preflightRow.loadError = row.image.error;
        preflightRow.fileName = QFileInfo(row.image.path).fileName();
        preflightRow.nameCheck = row.nameCheck();
        preflightRow.shaPinMismatch = row.shaPinMismatch;
        preflightRow.engineProblem = m_engineProblem.value(row.cardType);
        input.rows.append(preflightRow);
    }
    return Flasher::evaluatePreflight(input);
}

QVector<Flasher::BatchEntry> FlasherQueuePage::batchEntries() const
{
    QVector<Flasher::BatchEntry> entries;
    for (const FlasherQueueRow &row : m_model->rows()) {
        if (!row.ticked || !row.image.loaded) {
            continue;
        }
        Flasher::BatchEntry entry;
        entry.cardType = row.cardType;
        entry.image = row.image;
        entries.append(entry);
    }
    return entries;
}

void FlasherQueuePage::selectCard(int cardType)
{
    const int rowIndex = m_model->rowForCard(cardType);
    if (rowIndex >= 0) {
        m_model->setTicked(rowIndex, true);
        m_table->selectRow(rowIndex);
    } else {
        const int selected = m_model->tickedRow();
        if (selected >= 0) {
            m_model->setTicked(selected, false);
        }
    }
}

void FlasherQueuePage::setImageForCard(int cardType, const QString &path)
{
    loadImageIntoRow(m_model->rowForCard(cardType), path);
}

// -----------------------------------------------------------------------------
//  Pre-flight display
// -----------------------------------------------------------------------------

void FlasherQueuePage::refreshPreflight()
{
    if (m_preflightLayout == nullptr || m_flashButton == nullptr) {
        return;   // still constructing
    }
    const Flasher::PreflightReport report = preflight();

    // Rebuild the checklist rows. There are at most a dozen; rebuilding is
    // simpler and cheaper to get right than diffing.
    while (QLayoutItem *item = m_preflightLayout->takeAt(0)) {
        if (item->widget() != nullptr) {
            // Hidden first (session 134): out of the layout but not yet
            // deleted, an old row stayed visible at the card's top-left
            // corner -- the stray "⚠" over PRE-FLIGHT.
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (const Flasher::PreflightItem &item : report.items) {
        auto *row = new QWidget();
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto *glyph = new QLabel(row);
        auto *text = new QLabel(item.text, row);
        text->setWordWrap(true);
        // Glyph and colour together: colour alone is not a signal.
        if (item.state == Flasher::PreflightItem::State::Ok) {
            glyph->setText(QStringLiteral("✓"));
            glyph->setStyleSheet(UiColor::style(FlasherStyle::primary()));
        } else {
            glyph->setText(QStringLiteral("⚠"));
            glyph->setStyleSheet(UiColor::style(FlasherStyle::attention()));
            text->setStyleSheet(UiColor::style(FlasherStyle::attention()));
            if (item.blocksFlash) {
                QFont bold = text->font();
                bold.setBold(true);
                text->setFont(bold);
            }
        }
        glyph->setAlignment(Qt::AlignTop);
        glyph->setFixedWidth(16);
        rowLayout->addWidget(glyph);
        rowLayout->addWidget(text, 1);
        m_preflightLayout->addWidget(row);
    }

    // The route diagram mirrors the ticked cards.
    QSet<int> included;
    for (const FlasherQueueRow &row : m_model->rows()) {
        if (row.ticked) {
            included.insert(row.cardType);
        }
    }
    m_route->setIncludedCards(included);

    // The action bar: button label, enabled state, first blocker inline.
    const int selectedRow = m_model->tickedRow();
    if (selectedRow >= 0) {
        m_flashButton->setText(tr("Flash %1").arg(Flasher::cardName(m_model->row(selectedRow).cardType)));
    } else {
        m_flashButton->setText(tr("Flash"));
    }
    m_procedureNote->setText(tr("Press Flash, then power-cycle the chassis. The updater listens for "
                                "%1 s after power-on; the flasher keeps calling it for up to %2 s.")
                                 .arg(Flasher::kUpdaterListenSeconds)
                                 .arg(m_profile.updaterWaitSeconds));
    m_flashButton->setEnabled(report.ready);
    // Hidden when empty (session 134): an empty label still took half the
    // bar, the procedure note wrapped to five lines, and the bar's height
    // pushed the detail strip over the queue's last row.
    m_blockerLabel->setVisible(!report.ready);
    if (report.ready) {
        m_blockerLabel->clear();
    } else {
        m_blockerLabel->setText(QStringLiteral("⚠ ") + report.firstBlocker);
        m_blockerLabel->setStyleSheet(UiColor::style(FlasherStyle::attention()));
    }
}

// -----------------------------------------------------------------------------
//  Adapters
// -----------------------------------------------------------------------------

void FlasherQueuePage::refreshAdapters()
{
    const QVariant previous = m_adapterCombo->currentData();
    const QSignalBlocker blocker(m_adapterCombo);
    m_adapterCombo->clear();

    // Every IPv4 address on every adapter that is up. Loopback is listed last
    // rather than hidden: it is how the board simulator is reached.
    QList<QPair<QString, QVariantList>> physical;
    QList<QPair<QString, QVariantList>> loopback;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        const QNetworkInterface::InterfaceFlags interfaceFlags = networkInterface.flags();
        if (!(interfaceFlags & QNetworkInterface::IsUp)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            const QString label = QStringLiteral("%1 · %2")
                                      .arg(networkInterface.humanReadableName(), entry.ip().toString());
            const QVariantList data{ entry.ip().toIPv4Address(), entry.netmask().toIPv4Address() };
            if (interfaceFlags & QNetworkInterface::IsLoopBack) {
                loopback.append(qMakePair(label, data));
            } else {
                physical.append(qMakePair(label, data));
            }
        }
    }
    for (const auto &entry : physical) {
        m_adapterCombo->addItem(entry.first, entry.second);
    }
    for (const auto &entry : loopback) {
        m_adapterCombo->addItem(entry.first, entry.second);
    }
    if (m_adapterCombo->count() == 0) {
        m_adapterCombo->addItem(tr("No network adapter is up"), QVariant());
    }

    const int previousIndex = m_adapterCombo->findData(previous);
    if (previousIndex >= 0) {
        m_adapterCombo->setCurrentIndex(previousIndex);
    }
    preselectAdapterForTarget();
    refreshPreflight();
}

void FlasherQueuePage::preselectAdapterForTarget()
{
    uint32_t targetIp = 0;
    if (!kflash::parse_ipv4(vccIp().toStdString(), &targetIp)) {
        return;
    }
    // Keep the current choice if it already reaches the board.
    if (Flasher::sameSubnet(selectedAdapterIp(), selectedAdapterMask(), targetIp)) {
        return;
    }
    for (int index = 0; index < m_adapterCombo->count(); ++index) {
        const QVariantList entry = m_adapterCombo->itemData(index).toList();
        if (entry.size() == 2 && Flasher::sameSubnet(entry.at(0).toUInt(), entry.at(1).toUInt(), targetIp)) {
            const QSignalBlocker blocker(m_adapterCombo);
            m_adapterCombo->setCurrentIndex(index);
            return;
        }
    }
}

// -----------------------------------------------------------------------------
//  Images
// -----------------------------------------------------------------------------

void FlasherQueuePage::onTableClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    if (index.column() == FlasherQueueModel::ColumnImage) {
        browseForRow(index.row());
    } else if (index.column() == FlasherQueueModel::ColumnRemove) {
        if (m_model->row(index.row()).hasImage()) {
            m_model->clearImage(index.row());
        }
    }
}

void FlasherQueuePage::browseForRow(int rowIndex)
{
    const FlasherQueueRow &row = m_model->row(rowIndex);
    QString startDirectory;
    if (row.hasImage()) {
        startDirectory = QFileInfo(row.image.path).absolutePath();
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose the %1 image").arg(Flasher::cardName(row.cardType)), startDirectory,
        tr("Firmware images (*.appimage);;All files (*)"));
    if (!path.isEmpty()) {
        loadImageAndSelect(rowIndex, path);
    }
}

void FlasherQueuePage::loadImageAndSelect(int rowIndex, const QString &path)
{
    // Choosing a file for a card by hand means "this is the one to flash".
    loadImageIntoRow(rowIndex, path);
    if (rowIndex >= 0 && rowIndex < m_model->rowCount()) {
        m_model->setTicked(rowIndex, true);
    }
}

void FlasherQueuePage::loadImageIntoRow(int rowIndex, const QString &path)
{
    if (rowIndex < 0 || rowIndex >= m_model->rowCount()) {
        return;
    }
    const int cardType = m_model->row(rowIndex).cardType;
    const Flasher::ImageInfo image = Flasher::loadImage(path);

    // Ask the engine now, once, whether it would accept this image, rather
    // than on every checklist refresh (validate() wants the bytes in a
    // std::vector, which is a copy of up to 16 MB). The IP and port here are
    // placeholders; the real target is checked separately.
    m_engineProblem.remove(cardType);
    if (image.loaded) {
        kflash::FlashConfig config;
        config.receiver_ip = "10.0.0.1";
        config.port = 1;
        config.card_type = cardType;
        config.image.assign(image.bytes.constBegin(), image.bytes.constEnd());
        config.tuning = m_profile.tuning;
        const std::string problem = kflash::FlashEngine::validate(config);
        if (!problem.empty()) {
            m_engineProblem.insert(cardType, QString::fromStdString(problem));
        }
    } else {
        m_status->warn(tr("%1: %2").arg(QDir::toNativeSeparators(path), image.error));
    }

    // SHA pin: only meaningful when this is the profile's own default file.
    bool pinMismatch = false;
    if (m_profile.pinBySha && image.loaded) {
        const QString pinned = m_profile.pinnedSha.value(cardType);
        const QString defaultPath = m_profile.defaultImages.value(cardType);
        const bool isDefaultFile =
            !defaultPath.isEmpty()
            && QFileInfo(defaultPath).absoluteFilePath() == QFileInfo(path).absoluteFilePath();
        if (isDefaultFile && !pinned.isEmpty() && pinned != image.shaHex()) {
            pinMismatch = true;
        }
    }

    m_model->setImage(rowIndex, image);
    m_model->setShaPinMismatch(rowIndex, pinMismatch);

    // Select the row just loaded so its detail strip is what the operator sees.
    m_table->selectRow(rowIndex);
}

void FlasherQueuePage::onSelectionChanged()
{
    updateDetailStrip();
}

void FlasherQueuePage::updateDetailStrip()
{
    if (m_detailStrip == nullptr) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        m_detailTitle->setText(tr("Select a row to see its full path, SHA-256 and modified time"));
        m_detailPath->clear();
        m_detailSha->clear();
        m_detailModified->clear();
        return;
    }
    const FlasherQueueRow &row = m_model->row(selected.first().row());
    QString fileName = QFileInfo(row.image.path).fileName();
    if (fileName.isEmpty()) {
        fileName = tr("no image");
    }
    m_detailTitle->setText(tr("Selected · %1 · %2").arg(Flasher::cardName(row.cardType), fileName));
    if (!row.hasImage()) {
        m_detailPath->setText(QStringLiteral("—"));
        m_detailSha->setText(QStringLiteral("—"));
        m_detailModified->setText(QStringLiteral("—"));
        return;
    }
    m_detailPath->setText(QDir::toNativeSeparators(row.image.path));
    if (row.image.loaded) {
        m_detailSha->setText(row.image.shaHex());
        m_detailModified->setText(tr("%1 · re-hashed if the file changes")
                                      .arg(row.image.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    } else {
        m_detailSha->setText(QStringLiteral("—"));
        m_detailModified->setText(row.image.error);
    }
}

void FlasherQueuePage::loadImagesFromFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder containing .appimage builds"));
    if (folder.isEmpty()) {
        return;
    }

    // For each card, the newest .appimage whose NAME matches that card and no
    // other. An ambiguous name (mentions two cards) is not guessed at.
    QHash<int, QFileInfo> newestPerCard;
    QDirIterator iterator(folder, QStringList{ QStringLiteral("*.appimage") }, QDir::Files,
                          QDirIterator::Subdirectories);
    int scanned = 0;
    while (iterator.hasNext()) {
        const QFileInfo candidate(iterator.next());
        ++scanned;
        const QList<int> named = Flasher::cardsNamedIn(candidate.fileName());
        if (named.size() != 1) {
            continue;
        }
        const int cardType = named.first();
        if (!newestPerCard.contains(cardType)
            || candidate.lastModified() > newestPerCard.value(cardType).lastModified()) {
            newestPerCard.insert(cardType, candidate);
        }
    }

    for (auto entry = newestPerCard.constBegin(); entry != newestPerCard.constEnd(); ++entry) {
        loadImageIntoRow(m_model->rowForCard(entry.key()), entry.value().absoluteFilePath());
    }

    if (newestPerCard.isEmpty()) {
        m_status->warn(tr("No .appimage in %1 names a card (looked at %2 files)")
                           .arg(QDir::toNativeSeparators(folder)).arg(scanned));
    } else {
        QStringList cards;
        for (int cardType : Flasher::defaultCardOrder()) {
            if (newestPerCard.contains(cardType)) {
                cards.append(Flasher::cardShortName(cardType));
            }
        }
        m_status->ok(tr("Loaded %1 from %2").arg(cards.join(QStringLiteral(", ")),
                                                QDir::toNativeSeparators(folder)));
    }
}

// -----------------------------------------------------------------------------
//  Files changing on disk
// -----------------------------------------------------------------------------

void FlasherQueuePage::updateWatcher()
{
    QSet<QString> wanted;
    for (const FlasherQueueRow &row : m_model->rows()) {
        if (row.hasImage() && QFileInfo::exists(row.image.path)) {
            wanted.insert(row.image.path);
        }
    }
    const QStringList watched = m_watcher->files();
    for (const QString &path : watched) {
        if (!wanted.contains(path)) {
            m_watcher->removePath(path);
        }
    }
    for (const QString &path : wanted) {
        if (!watched.contains(path)) {
            m_watcher->addPath(path);
        }
    }
}

void FlasherQueuePage::onFileChangedOnDisk(const QString &path)
{
    m_pendingRehash.insert(path);
    m_rehashTimer->start();   // restarts the debounce on every notification
}

void FlasherQueuePage::rehashChangedFiles()
{
    const QSet<QString> paths = m_pendingRehash;
    m_pendingRehash.clear();
    for (const QString &path : paths) {
        for (int rowIndex = 0; rowIndex < m_model->rowCount(); ++rowIndex) {
            if (m_model->row(rowIndex).image.path == path) {
                loadImageIntoRow(rowIndex, path);
                m_status->say(tr("%1 changed on disk — re-hashed").arg(QFileInfo(path).fileName()));
            }
        }
    }
    // A save-by-rename drops the path from the watcher; put it back.
    updateWatcher();
}

// -----------------------------------------------------------------------------
//  Visibility
// -----------------------------------------------------------------------------

void FlasherQueuePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshPreflight();
}
