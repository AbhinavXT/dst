#include "filterbar.h"
#include "uicolors.h"
#include "logmodel.h"
#include "logquery.h"
#include "namemap.h"
#include "querylineedit.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QTimer>

// =============================================================================
//  Internal proxy that combines a text-on-column filter with a
//  severity/direction chip filter as a logical AND. QSortFilterProxyModel
//  out-of-the-box only supports filtering on a single column; we override
//  filterAcceptsRow to evaluate both.
// =============================================================================
class CombinedProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    enum ChipMode { ChipAll = 0, ChipErrors = 1, ChipWarns = 2,
                    ChipIn   = 3, ChipOut    = 4 };

    void setTextFilter(const QString &pattern, bool regex, int column)
    {
        m_textPattern = pattern;
        m_textRegex   = regex;
        m_textColumn  = column;

        if (regex) {
            m_textRe = QRegularExpression(pattern,
                          QRegularExpression::CaseInsensitiveOption);
        } else {
            // Treat as a fixed-string substring match. We escape regex
            // metacharacters and OR-it as a regex anyway, so the same
            // matcher path serves both modes.
            m_textRe = QRegularExpression(QRegularExpression::escape(pattern),
                          QRegularExpression::CaseInsensitiveOption);
        }
        invalidateFilter();
    }

    void setChip(ChipMode mode)
    {
        m_chip = mode;
        invalidateFilter();
    }

    // Query mode replaces the text/column/regex path with a full boolean
    // expression. The chips still AND on top, so the one-click severity
    // buttons keep working alongside a typed query.
    void setQuery(const QString &text, bool enabled,
                  bool utc = false, qint64 dataEnd = 0)
    {
        m_queryEnabled = enabled;
        m_query.setUtc(utc);
        m_query.setDataEnd(dataEnd);
        m_queryOk = m_query.parse(text);
        m_queryError = m_query.errorString();
        m_queryErrorOffset = m_query.errorOffset();
        m_queryEmpty = m_query.isEmpty();
        invalidateFilter();
    }

    void setNameMap(const NameMap *n) { m_names = n; invalidateFilter(); }

    bool    queryOk()          const { return m_queryOk; }
    QString queryError()       const { return m_queryError; }
    int     queryErrorOffset() const { return m_queryErrorOffset; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override
    {
        const QAbstractItemModel *src = sourceModel();
        if (!src) return true;
        Q_UNUSED(parent);

        // Fast path: if the source model is a LogModel, go directly to
        // the underlying LogEntry instead of bouncing through Qt's
        // role/QVariant machinery. Saves ~3-5x per row on the data()
        // lookups — at 75K rows the difference is what makes the
        // filter feel instant vs sluggish.
        const LogModel *lm = qobject_cast<const LogModel*>(src);

        // ---- Chip filter ----------------------------------------------
        if (m_chip != ChipAll) {
            if (lm) {
                LogEntryPtr e = lm->entryAt(sourceRow);
                if (!e) return true;
                switch (m_chip) {
                case ChipErrors: if (e->severity  != Severity::Error) return false; break;
                case ChipWarns:  if (e->severity  != Severity::Warn)  return false; break;
                case ChipIn:     if (e->direction != Direction::In)   return false; break;
                case ChipOut:    if (e->direction != Direction::Out)  return false; break;
                default: break;
                }
            } else {
                // Fallback for non-LogModel sources (shouldn't happen
                // in practice but defensive).
                const QModelIndex sevIdx = src->index(sourceRow, LogModel::ColSeverity);
                const QModelIndex dirIdx = src->index(sourceRow, LogModel::ColDirection);
                const QString sev = src->data(sevIdx, Qt::DisplayRole).toString();
                const QString dir = src->data(dirIdx, Qt::DisplayRole).toString();
                // contains(), not equality: the Severity column renders a
                // leading glyph ("✕ ERR"), so an exact match would silently
                // never fire. This path is defensive and rarely taken,
                // which is exactly why the breakage would have gone
                // unnoticed.
                switch (m_chip) {
                case ChipErrors: if (!sev.contains("ERR"))  return false; break;
                case ChipWarns:  if (!sev.contains("WARN")) return false; break;
                case ChipIn:     if (!dir.contains("IN"))   return false; break;
                case ChipOut:    if (!dir.contains("OUT"))  return false; break;
                default: break;
                }
            }
        }

        // ---- Query filter ---------------------------------------------
        if (m_queryEnabled) {
            // A query that failed to parse deliberately matches everything.
            // The alternative — hiding every row — is indistinguishable
            // from a quiet bus, which is the worst possible way for a tool
            // like this to report a typo.
            if (!m_queryOk || m_queryEmpty) return true;
            if (!lm) return true;
            LogEntryPtr e = lm->entryAt(sourceRow);
            return e && m_query.match(*e, m_names);
        }

        // ---- Text filter ----------------------------------------------
        if (!m_textPattern.isEmpty() && m_textRe.isValid()) {
            QString cell;
            if (lm) {
                // Direct entry access — for the message column (the
                // common case) we read entry->text directly. For other
                // columns fall back to data().
                if (m_textColumn == LogModel::ColMessage) {
                    LogEntryPtr e = lm->entryAt(sourceRow);
                    if (e) cell = e->text;
                } else {
                    const QModelIndex idx = src->index(sourceRow, m_textColumn);
                    cell = src->data(idx, Qt::DisplayRole).toString();
                }
            } else {
                const QModelIndex idx = src->index(sourceRow, m_textColumn);
                cell = src->data(idx, Qt::DisplayRole).toString();
            }
            if (!m_textRe.match(cell).hasMatch()) return false;
        }

        return true;
    }

private:
    QString             m_textPattern;
    bool                m_textRegex  = false;
    int                 m_textColumn = LogModel::ColMessage;
    QRegularExpression  m_textRe;

    // Query mode state. Parsed once when the text changes, never inside
    // filterAcceptsRow — that runs once per row per keystroke and parsing
    // there would make every keystroke O(rows x query length).
    LogQuery            m_query;
    bool                m_queryEnabled = false;
    bool                m_queryOk      = true;
    bool                m_queryEmpty   = true;
    QString             m_queryError;
    int                 m_queryErrorOffset = -1;
    const NameMap      *m_names        = nullptr;
    ChipMode            m_chip       = ChipAll;
};


// =============================================================================
//  FilterBar
// =============================================================================
FilterBar::FilterBar(LogModel *source, QWidget *parent)
    : QWidget(parent)
    , m_source(source)
{
    auto *proxy = new CombinedProxy(this);
    proxy->setSourceModel(source);
    m_proxy = proxy;

    // ---- Build UI -----------------------------------------------------
    m_clearBtn = new QPushButton(QStringLiteral("✕"));
    m_clearBtn->setFixedWidth(24);
    m_clearBtn->setToolTip(tr("Clear filter"));

    auto *filterLabel = new QLabel(tr("Filter:"));

    m_edit = new QueryLineEdit;
    m_edit->setPlaceholderText(tr("Type to filter; Regex toggle to use patterns"));
    m_edit->setClearButtonEnabled(true);

    m_regexCb = new QCheckBox(tr("Regex"));

    m_columnBox = new QComboBox;
    m_columnBox->addItem(tr("Message"),  LogModel::ColMessage);
    m_columnBox->addItem(tr("Source"),   LogModel::ColSource);
    m_columnBox->addItem(tr("Name"),     LogModel::ColFriendly);
    m_columnBox->addItem(tr("Direction"),LogModel::ColDirection);
    // Time and Severity column filtering would be unusual; left out to
    // keep the dropdown short. Add later if anyone asks.

    // Chip group — mutually exclusive radio buttons styled as chips.
    auto *allChip    = new QPushButton(tr("All"));
    auto *errChip    = new QPushButton(tr("Errors"));
    auto *warnChip   = new QPushButton(tr("Warns"));
    auto *inChip     = new QPushButton(tr("IN"));
    auto *outChip    = new QPushButton(tr("OUT"));
    for (QPushButton *b : { allChip, errChip, warnChip, inChip, outChip }) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
    }
    allChip->setChecked(true);

    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    m_chipGroup->addButton(allChip,   CombinedProxy::ChipAll);
    m_chipGroup->addButton(errChip,   CombinedProxy::ChipErrors);
    m_chipGroup->addButton(warnChip,  CombinedProxy::ChipWarns);
    m_chipGroup->addButton(inChip,    CombinedProxy::ChipIn);
    m_chipGroup->addButton(outChip,   CombinedProxy::ChipOut);

    // Query mode. Off by default so the bar behaves exactly as before for
    // anyone who never touches it; on, the column dropdown and the Regex
    // box become meaningless (fields and /regex/ live inside the query) so
    // they are disabled rather than left looking operable.
    m_queryCb = new QCheckBox(tr("Query"));
    m_queryCb->setToolTip(
        tr("Boolean query mode.\n\n"
           "  sev:error src:33_1 RAD NOT \"No Error\"\n"
           "  time:14:02..14:09   after:9:05   last:15m\n"
           "  after:08-08-2026 14:02   time:23:50..00:10\n"
           "  len>64   len:10..64   sev:warn,error   src!=33_1\n"
           "  TRAIN_SPEED>60   LOCO_MODE=FS,SR   FRAME_NUM&7=3\n"
           "  msg:/Link\\s+\\d+/ hex:0a1b\n\n"
           "Fields: msg src name sev dir hex len time after before last field\n"
           "A time without a date matches that clock time on any day, as the\n"
           "Time column shows it. last: counts back from the newest row.\n"
           "Adjacent terms are ANDed. Use OR, NOT/!/-, and parentheses."));

    // Parse errors are shown here rather than swallowed. An invalid query
    // matches everything, so the view stays populated and this label is the
    // only thing that tells you the query isn't doing what you typed.
    m_queryError = new QLabel;
    m_queryError->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" text-decoration: underline;"));
    m_queryError->setCursor(Qt::PointingHandCursor);
    m_queryError->hide();
    // Clicking the message jumps to and selects the offending term. Reading
    // "col 34" and then counting characters is not a reasonable ask.
    m_queryError->installEventFilter(this);

    m_countLabel = new QLabel(tr("Showing 0 of 0"));
    m_countLabel->setStyleSheet(UiColor::mutedStyle());

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);
    layout->addWidget(m_clearBtn);
    layout->addWidget(filterLabel);
    layout->addWidget(m_edit, 1);   // stretch — the line edit takes free space
    layout->addWidget(m_regexCb);
    layout->addWidget(m_queryCb);
    layout->addWidget(new QLabel(tr("in")));
    layout->addWidget(m_columnBox);
    layout->addWidget(m_queryError);
    layout->addSpacing(8);
    // Session 118: one segmented control rather than five loose buttons. The
    // buttons, their group and their wiring are as before; only their parent
    // is new, and it is what the stylesheet draws as the group.
    auto *segment = new QWidget(this);
    segment->setObjectName(QStringLiteral("filterChips"));
    segment->setProperty("dlRole", QStringLiteral("segmented"));
    segment->setAttribute(Qt::WA_StyledBackground, true);
    auto *segRow = new QHBoxLayout(segment);
    segRow->setContentsMargins(0, 0, 0, 0);
    segRow->setSpacing(0);
    for (QPushButton *b : { allChip, errChip, warnChip, inChip, outChip }) segRow->addWidget(b);
    layout->addWidget(segment);
    layout->addSpacing(8);
    layout->addWidget(m_countLabel);

    // ---- Debounce timer ----------------------------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout,
            this,       &FilterBar::onDebounceTimeout);

    // ---- Wiring -------------------------------------------------------
    connect(m_edit,      &QLineEdit::textChanged,
            this,        &FilterBar::onTextChanged);
    connect(m_queryCb,   &QCheckBox::toggled,
            this, [this](bool on) {
                m_edit->setPlaceholderText(
                    on ? tr("sev:error src:33_1 RAD NOT \"No Error\"")
                       : tr("Type to filter; Regex toggle to use patterns"));
                applyFilter();
            });
    connect(m_regexCb,   &QCheckBox::toggled,
            this,        &FilterBar::onRegexToggled);
    connect(m_columnBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,        &FilterBar::onColumnChanged);
    connect(m_clearBtn,  &QPushButton::clicked,
            this,        &FilterBar::onClearClicked);

    // QButtonGroup signals: Qt 5.15 added a clean idClicked(int). Earlier
    // Qt 5.x has only the overloaded buttonClicked, where the int overload
    // gives us the button id. Pick whichever exists at compile time so
    // the build is clean on every Qt 5 version.
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_chipGroup, &QButtonGroup::idClicked,
            this,        &FilterBar::onChipClicked);
#else
    connect(m_chipGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this,        &FilterBar::onChipClicked);
#endif

    // Update the counts label on every model row change. We listen on
    // both the source (so M moves up when new messages arrive) and the
    // proxy (so N reflects the filter outcome).
    connect(m_source, &QAbstractItemModel::rowsInserted,
            this,     &FilterBar::updateCountsLabel);
    connect(m_source, &QAbstractItemModel::rowsRemoved,
            this,     &FilterBar::updateCountsLabel);
    connect(m_source, &QAbstractItemModel::modelReset,
            this,     &FilterBar::updateCountsLabel);
    connect(m_proxy,  &QAbstractItemModel::rowsInserted,
            this,     &FilterBar::updateCountsLabel);
    connect(m_proxy,  &QAbstractItemModel::rowsRemoved,
            this,     &FilterBar::updateCountsLabel);
    connect(m_proxy,  &QAbstractItemModel::modelReset,
            this,     &FilterBar::updateCountsLabel);

    // Local/UTC toggle: the Time column header changes, and a clock-time
    // query has to follow it or it stops meaning what the column shows.
    connect(m_source, &QAbstractItemModel::headerDataChanged,
            this, [this]() { if (queryModeOn()) applyFilter(); });

    updateCountsLabel();
}

FilterBar::~FilterBar() = default;

void FilterBar::onTextChanged()
{
    // Restart the debounce timer on every keystroke. The actual filter
    // application happens in onDebounceTimeout 150 ms after typing stops.
    m_debounce->start();
}

void FilterBar::onDebounceTimeout()
{
    applyFilter();
}

void FilterBar::onRegexToggled(bool)
{
    applyFilter();
}

void FilterBar::onColumnChanged(int)
{
    applyFilter();
}

void FilterBar::onChipClicked(int id)
{
    m_activeChip = id;
    auto *proxy = static_cast<CombinedProxy*>(m_proxy);
    proxy->setChip(static_cast<CombinedProxy::ChipMode>(id));
}

void FilterBar::onClearClicked()
{
    m_edit->clear();
    m_regexCb->setChecked(false);
    // Reset chip to "All". The QButtonGroup remembers its current id; we
    // explicitly set the All button checked.
    QAbstractButton *all = m_chipGroup->button(CombinedProxy::ChipAll);
    if (all) all->setChecked(true);
    m_activeChip = CombinedProxy::ChipAll;
    auto *proxy = static_cast<CombinedProxy*>(m_proxy);
    proxy->setChip(CombinedProxy::ChipAll);
    applyFilter();
}

void FilterBar::applyFilter()
{
    auto *proxy = static_cast<CombinedProxy*>(m_proxy);
    const bool queryMode = m_queryCb && m_queryCb->isChecked();

    m_regexCb  ->setEnabled(!queryMode);
    m_columnBox->setEnabled(!queryMode);

    if (queryMode) {
        // Clock times are read in the zone the Time column is showing, and
        // last: counts back from the newest row — so a replay from last
        // week filters by what is on screen, not by today's clock.
        proxy->setQuery(m_edit->text(), true, m_source->showUtc(), m_source->newestMs());
        if (!proxy->queryOk()) {
            // Column, not just a message: with several terms on one line,
            // "unexpected ')'" alone does not say which one.
            const int off = proxy->queryErrorOffset();
            m_queryError->setText(off >= 0
                                      ? tr("col %1: %2").arg(off + 1)
                                            .arg(proxy->queryError())
                                      : proxy->queryError());
            m_queryError->setToolTip(tr("Click to select the offending term"));
            m_queryError->show();
            m_edit->setQueryError(proxy->queryError(), off);
        } else {
            m_queryError->hide();
            m_edit->clearQueryError();
            m_edit->rememberCurrent();
        }
        return;
    }

    proxy->setQuery(QString(), false);
    m_queryError->hide();
    m_edit->clearQueryError();
    proxy->setTextFilter(m_edit->text(),
                         m_regexCb->isChecked(),
                         m_columnBox->currentData().toInt());
}

void FilterBar::setQuery(const QString &expression)
{
    if (m_queryCb) m_queryCb->setChecked(true);
    if (m_edit)    m_edit->setText(expression);
    applyFilter();
}

bool FilterBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_queryError && event->type() == QEvent::MouseButtonRelease) {
        m_edit->selectErrorRange();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QString FilterBar::filterText() const
{
    return m_edit ? m_edit->text() : QString();
}

bool FilterBar::queryModeOn() const
{
    return m_queryCb && m_queryCb->isChecked();
}

void FilterBar::setNameMap(const NameMap *names)
{
    static_cast<CombinedProxy*>(m_proxy)->setNameMap(names);
}

void FilterBar::updateCountsLabel()
{
    if (!m_source || !m_proxy) return;
    const int total = m_source->rowCount();
    const int shown = m_proxy->rowCount();
    if (total == shown) {
        m_countLabel->setText(tr("Showing %1").arg(total));
    } else {
        m_countLabel->setText(tr("Showing %1 of %2").arg(shown).arg(total));
        m_countLabel->setStyleSheet(UiColor::accentStyle() + QStringLiteral(" font-weight: bold;"));
        return;
    }
    m_countLabel->setStyleSheet(UiColor::mutedStyle());
}
