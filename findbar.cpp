#include "findbar.h"
#include "settings.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QDialog>
#include <QBoxLayout>
#include "uicolors.h"
#include "logmodel.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include "querylineedit.h"

#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

FindBar::FindBar(QTableView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
{
    buildWidgets();

    // ---- Wiring -----------------------------------------------------
    connect(m_edit,      &QLineEdit::textChanged,
            this,        &FindBar::onTextChanged);
    connect(m_edit,      &QLineEdit::returnPressed,
            this,        &FindBar::findNext);
    connect(m_caseSens,  &QCheckBox::toggled,
            this,        [this](bool){ if (!m_building) rebuildMatches(); });
    connect(m_wholeWord, &QCheckBox::toggled,
            this,        [this](bool){ if (!m_building) rebuildMatches(); });
    connect(m_modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (m_building) { return; }
        Settings::setFindMode(static_cast<int>(mode()));
        syncModeControls();
        rebuildMatches();
    });
    // Marking is only a rendering choice — no need to re-scan for it.
    connect(m_markAll,   &QCheckBox::toggled,
            this,        [this](bool){ if (!m_building) publishHits(); });
    connect(m_columnBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,        [this](int){ if (!m_building) rebuildMatches(); });
    connect(m_wrapCb, &QCheckBox::toggled, this, [this](bool on) {
        if (m_building) { return; }
        Settings::setFindWrap(on);
        // Nothing to re-scan: wrapping changes what Next does at the end of
        // the list, not which rows matched. Clearing the hint stops a stale
        // "(wrapped)" sitting under a search that no longer wraps.
        m_wrapHint->clear();
    });
    connect(m_limitBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (m_building) { return; }
        Settings::setFindScanLimit(m_limitBox->currentData().toInt());
        rebuildMatches();
    });
    connect(m_advBtn, &QToolButton::toggled, this, [this](bool on) {
        if (m_building) { return; }
        setAdvancedOpen(on);
        Settings::setFindAdvancedOpen(on);
    });

    connect(m_nextBtn,   &QPushButton::clicked, this, &FindBar::findNext);
    connect(m_prevBtn,   &QPushButton::clicked, this, &FindBar::findPrev);
    connect(m_detachBtn, &QPushButton::clicked, this, &FindBar::toggleDetached);
    connect(m_closeBtn,  &QPushButton::clicked, this, &FindBar::close);

    // If the tab goes while this bar is floating, the bar is a child of its
    // own window and would outlive the view it searches — a dangling m_view
    // that the next keystroke would walk into. Re-dock and go with it.
    connect(m_view, &QObject::destroyed, this, [this] {
        m_view = nullptr;
        setDetached(false);
        deleteLater();
    });

    // F3 / Shift+F3 anywhere in the parent tab walks matches.
    auto *nextSc = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(nextSc, &QShortcut::activated, this, &FindBar::findNext);
    auto *prevSc = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(prevSc, &QShortcut::activated, this, &FindBar::findPrev);

    // Esc closes the bar even when the line edit has focus.
    auto *escSc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escSc, &QShortcut::activated, this, &FindBar::close);

    // Debounce timer for typing AND for rebuilds after a filter, re-sort or
    // reset. With a 200ms single-shot timer, the scan runs once after the
    // burst settles. Arriving rows never start it (session 157).
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        // Two very different reasons land on the same timer, and they must
        // not behave the same way. Typing means "show me the first match".
        // A filter or a re-sort means "nothing about my position changed" —
        // re-scanning is required (the row numbers moved), but jumping is
        // not.
        if (m_userRescan) {
            m_userRescan = false;
            rebuildMatches();
        } else {
            rescanKeepingPosition();
        }
    });

    // A filter, a re-sort or a reset schedules a rebuild via the debounce
    // timer. Rows arriving do not: a search covers the rows present when it
    // ran (session 157, see onRowsInserted).
    ensureModelWiring();

    m_building = false;
    syncModeControls();
    applyShape();
    hide();   // start hidden; activate() shows
}

// =============================================================================
//  Building the controls
//
//  Every control is made once here and never rebuilt. applyShape() moves the
//  same widgets between the strip layout and the window layout, so a search
//  in progress survives docking and undocking — the alternative, building a
//  second set of controls for the window, is how the two shapes end up
//  disagreeing about what is being searched for.
// =============================================================================
void FindBar::buildWidgets()
{
    m_findLabel = new QLabel(tr("Find:"), this);

    m_edit = new QueryLineEdit(this);
    m_edit->setObjectName(QStringLiteral("findEdit"));
    m_edit->setClearButtonEnabled(true);

    m_modeLabel = new QLabel(tr("Mode:"), this);
    m_modeBox   = new QComboBox(this);
    m_modeBox->setObjectName(QStringLiteral("findModeBox"));
    m_modeBox->addItem(tr("Text"),               static_cast<int>(Mode::Text));
    m_modeBox->addItem(tr("Extended"),           static_cast<int>(Mode::Extended));
    m_modeBox->addItem(tr("Regular expression"), static_cast<int>(Mode::Regex));
    m_modeBox->addItem(tr("Hex bytes"),          static_cast<int>(Mode::Hex));
    m_modeBox->addItem(tr("Query"),              static_cast<int>(Mode::Query));
    m_modeBox->setToolTip(tr("How the text above is read"));

    m_inLabel   = new QLabel(tr("in"), this);
    m_columnBox = new QComboBox(this);
    m_columnBox->setObjectName(QStringLiteral("findColumnBox"));
    m_columnBox->addItem(tr("Message"),    LogModel::ColMessage);
    m_columnBox->addItem(tr("Source"),     LogModel::ColSource);
    m_columnBox->addItem(tr("Name"),       LogModel::ColFriendly);
    m_columnBox->addItem(tr("Any column"), -1);

    m_caseSens = new QCheckBox(this);
    m_caseSens->setObjectName(QStringLiteral("findCaseBox"));
    m_caseSens->setToolTip(tr("Case sensitive"));

    m_wholeWord = new QCheckBox(this);
    m_wholeWord->setObjectName(QStringLiteral("findWholeWordBox"));
    m_wholeWord->setToolTip(tr("Whole word only — STN stops matching STN_ID"));

    m_markAll = new QCheckBox(this);
    m_markAll->setObjectName(QStringLiteral("findMarkBox"));
    m_markAll->setToolTip(tr("Tint every matching row, not just the current one"));
    m_markAll->setChecked(true);

    m_modeHelp = new QLabel(this);
    m_modeHelp->setObjectName(QStringLiteral("findModeHelp"));
    m_modeHelp->setWordWrap(true);
    m_modeHelp->setStyleSheet(UiColor::mutedStyle());

    // ---- advanced -----------------------------------------------------
    //
    // What goes in here is the things that change how the search BEHAVES, as
    // opposed to what it looks for. Both are answers to "why did Find do
    // that" rather than controls anybody needs on every search, which is
    // exactly what an advanced section is for.
    m_advBtn = new QToolButton(this);
    m_advBtn->setObjectName(QStringLiteral("findAdvancedBtn"));
    m_advBtn->setText(tr("Advanced"));
    m_advBtn->setCheckable(true);
    m_advBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advBtn->setArrowType(Qt::RightArrow);
    m_advBtn->setAutoRaise(true);

    m_advPanel = new QWidget(this);
    m_advPanel->setObjectName(QStringLiteral("findAdvancedPanel"));
    auto *adv = new QVBoxLayout(m_advPanel);
    adv->setContentsMargins(UiStyle::margin(), 0, 0, 0);
    adv->setSpacing(UiStyle::gap());

    m_wrapCb = new QCheckBox(tr("Wrap around at the end of the log"), m_advPanel);
    m_wrapCb->setObjectName(QStringLiteral("findWrapBox"));
    m_wrapCb->setToolTip(
        tr("With this off, Next stops at the last match instead of returning\n"
           "to the first — so a long list cannot be read round twice without\n"
           "noticing."));
    m_wrapCb->setChecked(Settings::findWrap());
    adv->addWidget(m_wrapCb);

    auto *limitRow = new QHBoxLayout;
    limitRow->setSpacing(UiStyle::gap());
    m_limitLabel = new QLabel(tr("Scan at most:"), m_advPanel);
    m_limitBox   = new QComboBox(m_advPanel);
    m_limitBox->setObjectName(QStringLiteral("findScanLimitBox"));
    m_limitBox->addItem(tr("20,000 rows"),      20000);
    m_limitBox->addItem(tr("200,000 rows"),     kScanRowCap);
    m_limitBox->addItem(tr("1,000,000 rows"),   1000000);
    m_limitBox->addItem(tr("every row"),        0);
    m_limitBox->setToolTip(
        tr("A scan runs on the GUI thread, so a very large recording is\n"
           "capped to keep the window responsive. Raising this makes each\n"
           "search slower and complete."));
    limitRow->addWidget(m_limitLabel);
    limitRow->addWidget(m_limitBox);
    limitRow->addStretch(1);
    adv->addLayout(limitRow);

    {
        // Restore the saved limit, falling back to the default rather than
        // to whatever happens to be first in the list.
        const int saved = Settings::findScanLimit();
        int idx = m_limitBox->findData(saved);
        if (idx < 0) { idx = m_limitBox->findData(kScanRowCap); }
        m_limitBox->setCurrentIndex(qMax(0, idx));
    }

    // ---- navigation and status ----------------------------------------
    m_prevBtn = new QPushButton(this);
    m_prevBtn->setObjectName(QStringLiteral("findPrevBtn"));
    m_prevBtn->setToolTip(tr("Previous match (Shift+F3)"));
    m_nextBtn = new QPushButton(this);
    m_nextBtn->setObjectName(QStringLiteral("findNextBtn"));
    m_nextBtn->setToolTip(tr("Next match (F3)"));

    m_count = new QLabel(tr("0 matches"), this);
    m_count->setObjectName(QStringLiteral("findCountLabel"));
    m_count->setStyleSheet(UiColor::mutedStyle());

    m_wrapHint = new QLabel(this);
    m_wrapHint->setStyleSheet(UiColor::accentStyle() + QStringLiteral(" font-style: italic;"));

    // Says out loud that following the tail is on hold while a match is
    // being looked at. Without it the tab simply stops moving and looks
    // broken — which is exactly what the ping-pong fix would otherwise
    // trade one confusion for another one.
    m_holdHint = new QLabel(this);
    m_holdHint->setStyleSheet(UiColor::warningStyle() + QStringLiteral(" font-style: italic;"));

    m_detachBtn = new QPushButton(this);
    m_detachBtn->setObjectName(QStringLiteral("findDetachBtn"));

    m_closeBtn = new QPushButton(this);
    m_closeBtn->setObjectName(QStringLiteral("findCloseBtn"));

    // Session 82: the same search, every tab at once, in the search window.
    m_allTabsBtn = new QPushButton(tr("All tabs"), this);
    m_allTabsBtn->setObjectName(QStringLiteral("findAllTabsBtn"));
    m_allTabsBtn->setToolTip(tr("Search every source for this, results grouped by tab"));
    connect(m_allTabsBtn, &QPushButton::clicked, this, [this]() {
        QString error;
        const QString q = toLogQuery(mode(), searchText(), &error);
        if (q.isEmpty()) {
            m_count->setText(error.isEmpty() ? tr("nothing to search for") : error);
            return;
        }
        emit searchAllTabs(q);
    });

    // The remembered mode, clamped: an ini written by a later build (or by
    // hand) must not leave the box on nothing.
    const int savedMode = Settings::findMode();
    const int modeIdx   = m_modeBox->findData(savedMode);
    m_modeBox->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

    m_advBtn->setChecked(Settings::findAdvancedOpen());
    m_advPanel->setVisible(false);
}

// =============================================================================
//  The two shapes
//
//  Docked, this is one line above the table and every pixel is spoken for:
//  short labels, glyph buttons, no advanced section. Undocked it is a form,
//  where the input gets a row of its own and the options can say what they
//  are. Same widgets, same state, different arrangement.
// =============================================================================
void FindBar::applyShape()
{
    // Deleting the layout leaves the child widgets alone — QWidgetItem does
    // not own its widget — so this rearranges rather than rebuilds.
    if (QLayout *old = layout()) { delete old; }

    const bool win = isDetached();

    m_findLabel->setText(win ? tr("Find what:") : tr("Find:"));
    m_caseSens ->setText(win ? tr("Case sensitive")     : tr("Aa"));
    m_wholeWord->setText(win ? tr("Whole word only")    : tr("W"));
    m_markAll  ->setText(win ? tr("Highlight all matches") : tr("Mark"));
    m_prevBtn  ->setText(win ? tr("Previous") : QStringLiteral("◀"));
    m_nextBtn  ->setText(win ? tr("Next")     : QStringLiteral("▶"));
    m_closeBtn ->setText(win ? tr("Close")    : QStringLiteral("✕"));
    m_detachBtn->setText(win ? tr("Dock in tab") : QStringLiteral("⧉"));
    m_detachBtn->setToolTip(win ? tr("Put the find bar back in the tab")
                                : tr("Open Find in a window, with more options"));
    m_closeBtn->setToolTip(tr("Close find (Esc)"));

    // Glyph buttons need a fixed width to stay square in the strip; the
    // worded ones must be free to size themselves or the labels are clipped.
    const int narrow = win ? QWIDGETSIZE_MAX : 28;
    m_prevBtn  ->setMaximumWidth(narrow);
    m_nextBtn  ->setMaximumWidth(narrow);
    m_closeBtn ->setMaximumWidth(win ? QWIDGETSIZE_MAX : 24);
    m_detachBtn->setMaximumWidth(win ? QWIDGETSIZE_MAX : 24);

    // Anything not placed by the layout below must be hidden explicitly: a
    // child left out of a layout keeps its last geometry and would otherwise
    // sit on top of the new arrangement.
    m_modeLabel->setVisible(win);
    m_modeHelp ->setVisible(win);
    m_advBtn   ->setVisible(win);
    m_advPanel ->setVisible(win && m_advBtn->isChecked());
    m_findLabel->setVisible(true);
    m_inLabel  ->setVisible(true);

    if (!win) {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(4, 2, 4, 2);
        row->setSpacing(4);
        row->addWidget(m_findLabel);
        row->addWidget(m_edit, 1);
        row->addWidget(m_modeBox);
        row->addWidget(m_inLabel);
        row->addWidget(m_columnBox);
        row->addWidget(m_caseSens);
        row->addWidget(m_wholeWord);
        row->addWidget(m_markAll);
        row->addWidget(m_prevBtn);
        row->addWidget(m_nextBtn);
        row->addWidget(m_count);
        row->addWidget(m_wrapHint);
        row->addWidget(m_holdHint);
        row->addWidget(m_allTabsBtn);
        row->addWidget(m_detachBtn);
        row->addWidget(m_closeBtn);
        m_edit->setMinimumWidth(0);
        return;
    }

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(UiStyle::gap());

    // The input, with room to type in. The strip's box was whatever was left
    // after nine controls had taken their width, which for a query of any
    // length meant typing into a slot.
    auto *textRow = new QHBoxLayout;
    textRow->setSpacing(UiStyle::gap());
    textRow->addWidget(m_findLabel);
    textRow->addWidget(m_edit, 1);
    m_edit->setMinimumWidth(320);
    col->addLayout(textRow);

    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(UiStyle::gap());
    modeRow->addWidget(m_modeLabel);
    modeRow->addWidget(m_modeBox);
    modeRow->addSpacing(UiStyle::gap());
    modeRow->addWidget(m_inLabel);
    modeRow->addWidget(m_columnBox);
    modeRow->addStretch(1);
    col->addLayout(modeRow);

    col->addWidget(m_modeHelp);

    auto *optRow = new QHBoxLayout;
    optRow->setSpacing(UiStyle::margin());
    optRow->addWidget(m_caseSens);
    optRow->addWidget(m_wholeWord);
    optRow->addWidget(m_markAll);
    optRow->addStretch(1);
    col->addLayout(optRow);

    col->addWidget(m_advBtn, 0, Qt::AlignLeft);
    col->addWidget(m_advPanel);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(UiStyle::gap());
    btnRow->addWidget(m_count);
    btnRow->addWidget(m_wrapHint);
    btnRow->addWidget(m_holdHint);
    btnRow->addStretch(1);
    btnRow->addWidget(m_prevBtn);
    btnRow->addWidget(m_nextBtn);
    btnRow->addWidget(m_allTabsBtn);
    btnRow->addWidget(m_detachBtn);
    btnRow->addWidget(m_closeBtn);
    col->addLayout(btnRow);
}

void FindBar::setAdvancedOpen(bool on)
{
    m_advBtn->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_advPanel->setVisible(on && isDetached());
    if (m_host) {
        // Let the window give the panel its room back rather than keeping
        // the height it had while the section was closed.
        m_host->adjustSize();
    }
}

// =============================================================================
//  What each mode accepts, and which controls it leaves meaningful
// =============================================================================
QString FindBar::modeHelp(Mode m)
{
    switch (m) {
    case Mode::Text:
        return tr("Plain text. Case and whole-word apply.");
    case Mode::Extended:
        return tr("Plain text, after escapes: \\n \\r \\t \\0 \\xHH \\uHHHH \\\\");
    case Mode::Regex:
        return tr("A regular expression. Whole word is off — the pattern is yours to write.");
    case Mode::Hex:
        return tr("Bytes of the datagram, not text: 0a 1b, 0A:1B, 0a1b are the same. "
                  "Searches the whole frame, so the column above does not apply.");
    case Mode::Query:
        return tr("The boolean language, as in the filter bar: "
                  "sev:error src:33_1 NOT \"No Error\"");
    }
    return QString();
}

FindBar::Mode FindBar::mode() const
{
    const int raw = m_modeBox ? m_modeBox->currentData().toInt() : 0;
    switch (raw) {
    case 1: return Mode::Extended;
    case 2: return Mode::Regex;
    case 3: return Mode::Hex;
    case 4: return Mode::Query;
    default: return Mode::Text;
    }
}

void FindBar::setMode(Mode m)
{
    const int idx = m_modeBox->findData(static_cast<int>(m));
    if (idx >= 0) { m_modeBox->setCurrentIndex(idx); }
}

QString FindBar::searchText() const { return m_edit ? m_edit->text() : QString(); }

void FindBar::setSearchText(const QString &text) { if (m_edit) m_edit->setText(text); }

void FindBar::syncModeControls()
{
    const Mode m = mode();

    // Which of the plain-text options still mean something. Query carries
    // case and phrase handling inside the expression; Hex is not text at
    // all; a regex author writes their own boundaries.
    const bool textish = (m == Mode::Text || m == Mode::Extended);
    m_caseSens ->setEnabled(textish || m == Mode::Regex);
    m_wholeWord->setEnabled(textish);
    m_columnBox->setEnabled(m != Mode::Query && m != Mode::Hex);
    m_inLabel  ->setEnabled(m_columnBox->isEnabled());

    m_modeHelp->setText(modeHelp(m));

    switch (m) {
    case Mode::Text:
        m_edit->setPlaceholderText(tr("Type to find; Enter = next, Shift+Enter = prev"));
        break;
    case Mode::Extended:
        m_edit->setPlaceholderText(QStringLiteral("STN_ID\\x20""4"));
        break;
    case Mode::Regex:
        m_edit->setPlaceholderText(QStringLiteral("STN_ID\\s+\\d+"));
        break;
    case Mode::Hex:
        m_edit->setPlaceholderText(QStringLiteral("0a 1b 2c"));
        break;
    case Mode::Query:
        m_edit->setPlaceholderText(QStringLiteral("sev:error NOT \"No Error\""));
        break;
    }
}

// =============================================================================
//  Extended-mode escapes
//
//  Unknown escapes are an ERROR rather than a literal backslash-plus-letter.
//  Passing them through is what most editors do, and it means \s in extended
//  mode searches for the two characters \ and s, finds nothing, and says
//  nothing about why. Naming the escape costs one line and removes a whole
//  class of "the search is broken" reports.
// =============================================================================
FindBar::Unescaped FindBar::unescapeExtended(const QString &pattern)
{
    Unescaped out;
    QString s;
    s.reserve(pattern.size());

    auto hexRun = [&pattern](int from, int count, bool *ok) -> uint {
        uint v = 0;
        *ok = false;
        if (from + count > pattern.size()) { return 0; }
        for (int i = from; i < from + count; ++i) {
            const int d = QString(pattern.at(i)).toInt(ok, 16);
            if (!*ok) { return 0; }
            v = (v << 4) | uint(d);
        }
        *ok = true;
        return v;
    };

    for (int i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c != QLatin1Char('\\')) { s += c; continue; }

        if (i + 1 >= pattern.size()) {
            out.error = QStringLiteral("the pattern ends with a lone \\ — "
                                       "write \\\\ to search for a backslash");
            out.errorOffset = i;
            return out;
        }

        const int escAt = i;
        const QChar e = pattern.at(++i);
        switch (e.unicode()) {
        case 'n':  s += QLatin1Char('\n'); break;
        case 'r':  s += QLatin1Char('\r'); break;
        case 't':  s += QLatin1Char('\t'); break;
        case 'f':  s += QChar(0x0C); break;
        case 'v':  s += QChar(0x0B); break;
        case 'a':  s += QChar(0x07); break;
        case 'b':  s += QChar(0x08); break;
        case '0':  s += QChar(0x00); break;
        case '\\': s += QLatin1Char('\\'); break;
        case 'x':
        case 'X': {
            bool ok = false;
            const uint v = hexRun(i + 1, 2, &ok);
            if (!ok) {
                out.error = QStringLiteral("\\x needs exactly two hex digits, "
                                           "as in \\x1f");
                out.errorOffset = escAt;
                return out;
            }
            s += QChar(v);
            i += 2;
            break;
        }
        case 'u':
        case 'U': {
            bool ok = false;
            const uint v = hexRun(i + 1, 4, &ok);
            if (!ok) {
                out.error = QStringLiteral("\\u needs exactly four hex digits, "
                                           "as in \\u00b5");
                out.errorOffset = escAt;
                return out;
            }
            s += QChar(v);
            i += 4;
            break;
        }
        default:
            out.error = QStringLiteral("\\%1 is not an escape this mode knows — "
                                       "try \\n \\r \\t \\0 \\xHH \\uHHHH or \\\\")
                            .arg(e);
            out.errorOffset = escAt;
            return out;
        }
    }

    out.text = s;
    out.ok   = true;
    return out;
}

void FindBar::setDetached(bool on)
{
    if (on == isDetached()) { return; }

    if (on) {
        // Remember the seat so it can be put back in the same place. A bar
        // that re-docks at the bottom of the tab, or not at all, is worse
        // than one that never detached.
        m_dockLayout = qobject_cast<QBoxLayout *>(
            parentWidget() ? parentWidget()->layout() : nullptr);
        m_dockIndex  = m_dockLayout ? m_dockLayout->indexOf(this) : -1;

        // The host must never be parented to THIS widget. Undocking reparents
        // the bar INTO the host, so a host whose parent is the bar closes a
        // cycle in the widget tree — and QWidget::nativeParentWidget(), which
        // Qt walks on every reparent, follows parents until it finds a native
        // window and never returns. It hangs; it does not crash, which is
        // worse to diagnose.
        //
        // window() is what made that reachable: a bar with no parent widget
        // of its own is its own window, and reports itself. Anchor on the
        // VIEW's window instead — the console this bar searches, which is
        // where the find window belongs anyway — and fall back to no parent
        // rather than to something that could be us.
        QWidget *anchor = m_view ? m_view->window() : parentWidget();
        if (anchor == this) { anchor = nullptr; }
        m_host = new QDialog(anchor);
        m_host->setWindowTitle(m_contextLabel.isEmpty()
                                   ? tr("Find")
                                   : tr("Find — %1").arg(m_contextLabel));
        // Qt::Tool: stays above the console it searches without taking a
        // taskbar entry of its own, which is what every editor's find window
        // does and what makes it usable while scrolling the log behind it.
        m_host->setWindowFlags(Qt::Tool | Qt::WindowTitleHint
                               | Qt::WindowCloseButtonHint);
        auto *hl = new QVBoxLayout(m_host);
        hl->setContentsMargins(UiStyle::margin(), UiStyle::margin(),
                               UiStyle::margin(), UiStyle::margin());
        hl->addWidget(this);

        applyShape();                       // the window form, not the strip
        setAdvancedOpen(m_advBtn->isChecked());
        Settings::setFindDetached(true);    // remember the shape they chose

        m_host->resize(520, m_host->sizeHint().height());
        WindowGeometry::restore(m_host, QStringLiteral("findBar"));
        connect(m_host, &QDialog::finished, this, [this](int) { setDetached(false); });

        m_host->show();
        show();
        m_edit->setFocus();
        return;
    }

    QDialog *host = m_host;
    m_host = nullptr;                 // isDetached() false from here on

    if (host) { WindowGeometry::save(host, QStringLiteral("findBar")); }

    if (m_dockLayout) {
        m_dockLayout->insertWidget(m_dockIndex >= 0 ? m_dockIndex : 0, this);
    } else if (m_view) {
        // No layout to go back to — better parented to the view's window
        // than left owned by a dialog about to be deleted.
        setParent(m_view->window());
    }

    applyShape();                     // back to the strip
    // Only when the operator docked it deliberately. close() re-docks a
    // floating bar on its way out, and that must not be read as "I prefer
    // the bar": Esc means done searching, not change the layout.
    if (!m_closing) { Settings::setFindDetached(false); }

    if (host) {
        host->hide();
        host->deleteLater();          // not delete: we are inside its signal
    }
}

void FindBar::refreshModelBinding()
{
    ensureModelWiring();
    if (isVisible()) {
        // Re-scan against the new model, but do not jump: the operator changed
        // the source, they did not ask to be taken anywhere in it.
        m_userRescan = false;
        m_debounce->start();
    }
    updateCountLabel();
    refreshHold();
}

void FindBar::activate()
{
    ensureModelWiring();

    // Ctrl+F gives whichever shape was used last — a floating window by
    // default, because that is what every editor does and what people reach
    // for. Docking the bar is what switches it back; see close(), which
    // re-docks without counting as a choice.
    if (!isDetached() && Settings::findDetached()) {
        setDetached(true);
        m_edit->setFocus();
        m_edit->selectAll();       // typing replaces the previous query
        rebuildMatches();
        return;
    }

    show();
    if (m_host) {
        // Already floating: bring the window forward rather than opening a
        // second one, and put the caret where the operator expects it.
        m_host->show();
        m_host->raise();
        m_host->activateWindow();
    }
    m_edit->setFocus();
    m_edit->selectAll();
    rebuildMatches();
}

void FindBar::close()
{
    // Esc means "done searching" whether the bar is docked or floating, so
    // a floating bar comes home first rather than being left on screen with
    // nothing to search.
    m_closing = true;
    if (m_host) { setDetached(false); }

    m_wrapHint->clear();
    m_holdHint->clear();
    hide();
    // The tint goes with the bar. Rows left coloured after it is gone have
    // nothing on screen to explain them.
    publishHits();
    // Closing releases the view: holdsView() is false once we are hidden,
    // so the owner hears holdChanged(false) and resumes following the tail
    // if scroll lock is on. Esc therefore means "back to live", which is
    // what everyone tries first.
    refreshHold();
    if (m_view) m_view->setFocus();
    m_closing = false;
}

bool FindBar::holdsView() const
{
    return isVisible() && m_cursor >= 0 && m_cursor < m_matches.size();
}

void FindBar::setScrollLockActive(bool on)
{
    m_scrollLockActive = on;
    updateHoldHint();
}

void FindBar::refreshHold()
{
    const bool held    = holdsView();
    const bool changed = (held != m_hold);
    m_hold = held;
    updateHoldHint();
    if (changed) emit holdChanged(m_hold);
}

void FindBar::updateHoldHint()
{
    if (m_hold && m_scrollLockActive) {
        m_holdHint->setText(tr("following paused"));
        m_holdHint->setToolTip(
            tr("This tab is set to follow new messages, but Find is parked on\n"
               "a match, so the view is staying put. Close the find bar (Esc)\n"
               "to jump back to the end of the log and resume following."));
    } else {
        m_holdHint->clear();
        m_holdHint->setToolTip(QString());
    }
}

void FindBar::onTextChanged()
{
    // Route through the debounce timer rather than scanning immediately.
    // Previously this comment claimed sub-100ms scans even at 200k rows;
    // that wasn't true in practice — at ~75k rows the scan stalled the
    // GUI thread between keystrokes hard enough to make typing feel
    // stuck. With 200ms debounce + direct-LogEntry access in scanMatches,
    // scans run once after the user stops typing and complete in tens of
    // milliseconds at most.
    m_userRescan = true;      // typing = "take me to the first match"
    m_debounce->start();

    // Clear the count label visually so the user sees that something
    // is happening even before the debounce expires. Otherwise the
    // stale "1 of 47" lingers on screen while typing the next query
    // and feels broken.
    if (m_edit->text().isEmpty()) {
        m_matches.clear();
        m_cursor = -1;
        m_lastScanCapped = false;
        updateCountLabel();
        refreshHold();        // no match to sit on: let the tail run again
    } else {
        m_count->setText(tr("searching…"));
        m_count->setStyleSheet(UiColor::mutedStyle() + QStringLiteral(" font-style: italic;"));
    }
}

void FindBar::rebuildMatches()
{
    m_wrapHint->clear();
    scanMatches();
    publishHits();
    if (!m_matches.isEmpty()) {
        m_cursor = 0;
        jumpToCurrentMatch();
    } else {
        m_cursor = -1;
    }
    // Update the count label AFTER cursor is set so it shows e.g. "1 of 3"
    // rather than "0 of 3" on the first match.
    updateCountLabel();
    refreshHold();
}

void FindBar::rescanKeepingPosition()
{
    // Anchor on the ENTRY, not on the proxy row. Rows renumber under us for
    // two ordinary reasons — the model evicts the oldest entries once the
    // per-tab cap is reached, and a filter can drop rows above the cursor —
    // so a remembered row number silently becomes a different message.
    LogEntryPtr anchor;
    if (m_cursor >= 0 && m_cursor < m_matches.size()) {
        anchor = entryAtProxyRow(m_matches[m_cursor]);
    }

    scanMatches();
    publishHits();

    if (m_matches.isEmpty()) {
        m_cursor = -1;
    } else if (anchor) {
        m_cursor = -1;
        for (int i = 0; i < m_matches.size(); ++i) {
            if (entryAtProxyRow(m_matches[i]) == anchor) { m_cursor = i; break; }
        }
        // The anchored row is gone (evicted, or filtered out). Point at the
        // first match so the counter and F3 still work, but do NOT move the
        // view: the operator did not ask to go anywhere.
        if (m_cursor < 0) m_cursor = 0;
    } else {
        // No anchor to preserve — a query that had no matches has just
        // acquired some. Arm the cursor without scrolling; F3 takes them
        // there when they want to look.
        m_cursor = 0;
    }

    updateCountLabel();
    refreshHold();
}

LogEntryPtr FindBar::entryAtProxyRow(int proxyRow) const
{
    // A view may show a LogModel through a filter proxy (the log tabs and
    // the session viewer) or bind one directly (the compare panes, which
    // cast view->model() to LogModel in half a dozen places and would not
    // survive a proxy being slipped underneath them). Both are handled here
    // rather than by giving the compare window a find of its own.
    QAbstractItemModel *m = m_view ? m_view->model() : nullptr;
    if (!m) { return LogEntryPtr(); }
    if (proxyRow < 0 || proxyRow >= m->rowCount()) { return LogEntryPtr(); }

    if (auto *sortProxy = qobject_cast<QSortFilterProxyModel *>(m)) {
        const auto *lm = qobject_cast<const LogModel *>(sortProxy->sourceModel());
        if (!lm) { return LogEntryPtr(); }
        const QModelIndex sIdx = sortProxy->mapToSource(sortProxy->index(proxyRow, 0));
        return lm->entryAt(sIdx.row());
    }
    if (const auto *lm = qobject_cast<const LogModel *>(m)) {
        return lm->entryAt(proxyRow);
    }
    return LogEntryPtr();
}

int FindBar::scanLimitRows() const
{
    const int chosen = m_limitBox ? m_limitBox->currentData().toInt() : kScanRowCap;
    return chosen < 0 ? kScanRowCap : chosen;   // 0 means "everything"
}

// =============================================================================
//  Following the view's model, rather than the one it had at construction.
//
//  A log tab sets its model before its find bar exists, so wiring once in the
//  constructor worked there. A COMPARE PANE does not: the view is built empty
//  and given a model when a source is picked, and given a different one every
//  time the operator changes that pick. A bar wired once would then be
//  listening to a model the view no longer shows — Find would never re-scan as
//  traffic arrived, and after a source switch the match list would still hold
//  row numbers belonging to the previous source.
//
//  So the wiring is re-checked rather than assumed. The check is a pointer
//  comparison, which is why it is safe to call on every scan.
// =============================================================================
void FindBar::ensureModelWiring()
{
    QAbstractItemModel *m = m_view ? m_view->model() : nullptr;
    if (m == m_wiredModel) { return; }

    for (const QMetaObject::Connection &c : m_modelConns) { disconnect(c); }
    m_modelConns.clear();

    // The model changed under us. Whatever was matched belongs to the old one,
    // and a row number means something different now, so it goes rather than
    // being carried across and pointing at unrelated messages.
    if (m_wiredModel) {
        m_matches.clear();
        m_cursor = -1;
        m_lastScanCapped = false;
        m_scanDirty = true;
    }
    m_wiredModel = m;
    if (!m) { return; }

    auto scheduleRebuild = [this]() {
        // Row numbers may mean something different now, so the next scan
        // must be a full one: narrowing rescans only the PREVIOUS match
        // rows, which is only sound while the rows underneath hold still.
        m_scanDirty = true;
        if (isVisible()) m_debounce->start();
    };

    // Rows arriving or falling off the front (session 157) start no scan and
    // do not touch the debounce: under steady traffic a restarted timer
    // never fired, and the search the operator typed waited for a gap in
    // the packets. They only move the row numbers of the matches.
    m_modelConns << connect(m, &QAbstractItemModel::rowsInserted,  this, &FindBar::onRowsInserted);
    m_modelConns << connect(m, &QAbstractItemModel::rowsRemoved,   this, &FindBar::onRowsRemoved);
    m_modelConns << connect(m, &QAbstractItemModel::modelReset,    this, scheduleRebuild);
    m_modelConns << connect(m, &QAbstractItemModel::layoutChanged, this, scheduleRebuild);
}

void FindBar::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid()) { return; }
    const int n = last - first + 1;
    // Appended at the end, which is what traffic does: nothing searched moved,
    // and the new rows wait for the next search. Inserted among searched rows
    // (a filter letting a row back in, a sorted view): the matches below move
    // down, and the unsearched row in the middle means the next search cannot
    // be a narrowing of this one.
    for (int &r : m_matches) {
        if (r >= first) { r += n; }
    }
    if (m_lastScanRowsTotal >= 0 && first < m_lastScanRowsTotal) {
        m_lastScanRowsTotal += n;
        m_scanDirty = true;
    }
}

void FindBar::onRowsRemoved(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid()) { return; }
    const int n = last - first + 1;
    if (m_lastScanRowsTotal > first) {
        m_lastScanRowsTotal -= qMin(last + 1, m_lastScanRowsTotal) - first;
    }
    if (m_matches.isEmpty()) { return; }

    // The rest are still matches; only their numbers change. The cursor
    // stays on its row, or on the next match if its row is the one gone.
    QVector<int> kept;
    kept.reserve(m_matches.size());
    int cursor = -1;
    for (int i = 0; i < m_matches.size(); ++i) {
        const int r = m_matches[i];
        if (i == m_cursor) { cursor = kept.size(); }
        if (r >= first && r <= last) { continue; }
        kept.append(r > last ? r - n : r);
    }
    if (kept.size() == m_matches.size()) {
        m_matches = kept;
        return;
    }
    m_matches = kept;
    m_cursor = m_matches.isEmpty() ? -1 : qMin(cursor, m_matches.size() - 1);
    publishHits();            // the removed entries must not stay tinted
    updateCountLabel();
    refreshHold();
}

void FindBar::scanMatches()
{
    ensureModelWiring();

    // Kept for the narrowing check below, then dropped.
    const QVector<int> previous     = m_matches;
    const QString      previousText = m_scanText;
    const Mode         previousMode = m_scanMode;
    const bool         previousCs   = m_scanCase;
    const int          previousCol  = m_scanColumn;
    const bool         wasDirty     = m_scanDirty;
    m_scanDirty = false;
    m_scanText.clear();

    m_matches.clear();
    m_lastScanCapped = false;
    m_lastScanRows   = 0;

    if (!isVisible()) return;
    const QString text = m_edit->text();
    if (text.isEmpty()) { m_edit->clearQueryError(); return; }

    QAbstractItemModel *proxy = m_view ? m_view->model() : nullptr;
    if (!proxy) return;
    const int rows = proxy->rowCount();
    const int cols = proxy->columnCount();
    if (rows == 0) return;

    const Mode md = mode();

    // ---- what are we looking for ------------------------------------------
    //
    // Parsed ONCE, here, rather than per row. Every failure below marks the
    // input box with the offset that caused it — including a bad regular
    // expression, which used to return silently and leave "no matches" as
    // the only clue that the pattern would not compile.
    QString    plain;         // Text / Extended
    QByteArray wanted;        // Hex
    QRegularExpression re;
    bool useRegex = false;

    const Qt::CaseSensitivity cs = m_caseSens->isChecked() ? Qt::CaseSensitive
                                                           : Qt::CaseInsensitive;
    const QRegularExpression::PatternOptions reOpts =
        (cs == Qt::CaseInsensitive) ? QRegularExpression::CaseInsensitiveOption
                                    : QRegularExpression::NoPatternOption;

    switch (md) {
    case Mode::Query: {
        m_queryOk = m_query.parse(text);
        if (!m_queryOk) {
            m_edit->setQueryError(m_query.errorString(), m_query.errorOffset());
            return;            // no matches; the box shows why
        }
        m_edit->clearQueryError();
        m_edit->rememberCurrent();
        break;
    }
    case Mode::Hex: {
        const HexPattern hp = parseHexPattern(text);
        if (!hp.ok) {
            m_edit->setQueryError(hp.error, hp.errorOffset);
            return;
        }
        m_edit->clearQueryError();
        wanted = hp.bytes;
        break;
    }
    case Mode::Extended: {
        const Unescaped u = unescapeExtended(text);
        if (!u.ok) {
            m_edit->setQueryError(u.error, u.errorOffset);
            return;
        }
        m_edit->clearQueryError();
        plain = u.text;
        if (plain.isEmpty()) { return; }
        break;
    }
    case Mode::Regex: {
        re.setPattern(text);
        re.setPatternOptions(reOpts);
        if (!re.isValid()) {
            m_edit->setQueryError(re.errorString(), int(re.patternErrorOffset()));
            return;
        }
        m_edit->clearQueryError();
        useRegex = true;
        break;
    }
    case Mode::Text:
        m_edit->clearQueryError();
        plain = text;
        break;
    }

    // Whole word is implemented as a regex around the escaped text rather
    // than as a second matching path, so "STN" stops matching "STN_ID"
    // without a hand-rolled boundary test to get wrong. It applies to the
    // text modes only; in Regex the pattern is the operator's to write, and
    // Hex and Query have no notion of a word.
    if ((md == Mode::Text || md == Mode::Extended) && m_wholeWord->isChecked()) {
        re.setPattern(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(plain)));
        re.setPatternOptions(reOpts);
        if (re.isValid()) { useRegex = true; }
    }

    auto matchOne = [&](const QString &cell) -> bool {
        if (useRegex) return re.match(cell).hasMatch();
        return cell.contains(plain, cs);
    };

    const int colSel = m_columnBox->currentData().toInt();

    // Fast path: if the view's model is a QSortFilterProxyModel over a
    // LogModel, we can use mapToSource → entryAt(row) to access the
    // underlying LogEntry directly. This skips Qt's role/QVariant
    // machinery which dominates scan time at scale (~3-5x speedup on
    // the data() lookups).
    auto *sortProxy = qobject_cast<QSortFilterProxyModel*>(proxy);
    const LogModel *lm = sortProxy
        ? qobject_cast<const LogModel*>(sortProxy->sourceModel())
        : qobject_cast<const LogModel*>(proxy);   // bound directly, no filter

    // Query and Hex both match the whole ENTRY rather than a rendered cell:
    // a query evaluates against its fields, and Hex searches the datagram.
    // Neither can say anything about a view that is not showing a LogModel.
    const bool needsEntry = (md == Mode::Query || md == Mode::Hex);
    if (needsEntry && !lm) { return; }

    // Row cap: stop scanning after the operator's limit so the GUI thread
    // never stalls on a pathologically large model. The default is above
    // the per-tab capacity, so under normal use everything is scanned; the
    // advanced section can raise or remove it. If we hit the cap,
    // m_lastScanCapped is set so updateCountLabel can mention it.
    const int limit     = scanLimitRows();
    const int scanLimit = (limit > 0) ? qMin(rows, limit) : rows;
    if (scanLimit < rows) m_lastScanCapped = true;
    m_lastScanRows = scanLimit;

    m_matches.reserve(qMin(scanLimit, 4096));

    // The per-row entry lookup, using the sortProxy/lm resolved ABOVE the
    // loop rather than re-deriving them for every row.
    //
    // entryAtProxyRow does the right thing for one row and the wrong thing
    // a hundred thousand times: per call it re-reads the view's model, calls
    // rowCount(), runs two qobject_casts and returns a shared pointer by
    // value — an atomic increment and decrement for an entry that is read
    // and dropped immediately. None of that varies across a scan.
    auto entryFast = [&](int row) -> const LogEntry * {
        if (!lm) { return nullptr; }
        if (!sortProxy) { return lm->entryPtrAt(row); }
        return lm->entryPtrAt(sortProxy->mapToSource(sortProxy->index(row, 0)).row());
    };

    // ---- can this be a narrowing rather than a full scan? -----------------
    //
    // Typing is the common case, and each keystroke used to rescan every row
    // from the top. It does not have to: with a plain "contains" search, a
    // row that does not contain "STN" cannot contain "STN_ID", so the
    // matches for a longer pattern are a SUBSET of the matches for a shorter
    // one it contains. Rescanning only the previous match rows turns the
    // second and later keystrokes of a word into a scan of the hits rather
    // than of the model.
    //
    // Every condition here is load-bearing:
    //   – plain contains only. Regex and whole-word (which is a regex) have
    //     no subset property at all: /STN.$/ matches rows that /STN.*$/ may
    //     not. Query and Hex match the whole entry, not the cell.
    //   – same mode, case sensitivity and column, or "the same search" is
    //     not the same question.
    //   – nothing moved underneath. Rows arriving, a filter changing or a
    //     re-sort all renumber rows, and a stale row number would point at
    //     an unrelated message.
    //   – the previous scan was not capped, or rows past the cap were never
    //     examined and cannot be ruled out now.
    const bool canNarrow =
        !wasDirty
        && !useRegex
        && !previousText.isEmpty()
        && (md == Mode::Text || md == Mode::Extended)
        && md == previousMode
        && (cs == Qt::CaseSensitive) == previousCs
        && colSel == previousCol
        && colSel == LogModel::ColMessage
        && lm && !previous.isEmpty()
        && rows == m_lastScanRowsTotal
        && !m_lastScanWasCapped
        && plain.contains(previousText, cs);

    // Remembered for the NEXT scan's decision, whichever way this one goes.
    m_scanText   = plain;
    m_scanMode   = md;
    m_scanCase   = (cs == Qt::CaseSensitive);
    m_scanColumn = colSel;
    m_lastScanRowsTotal = rows;

    if (canNarrow) {
        m_lastScanRows = scanLimit;
        for (int r : previous) {
            if (r < 0 || r >= scanLimit) { continue; }
            const LogEntry *e = entryFast(r);
            if (e && matchOne(e->text)) { m_matches.append(r); }
        }
        m_lastScanWasCapped = m_lastScanCapped;
        return;
    }

    for (int r = 0; r < scanLimit; ++r) {
        bool hit = false;

        if (needsEntry) {
            // One lookup serving both entry-shaped modes.
            const LogEntry *e = entryFast(r);
            if (!e) continue;
            hit = (md == Mode::Query) ? m_query.match(*e, nullptr)
                                      : e->rawBytes.contains(wanted);
        } else if (lm && colSel == LogModel::ColMessage) {
            // Direct LogEntry fast path — the by-far most common case
            // (default column = Message). Avoids ALL QVariant boxing.
            const LogEntry *e = entryFast(r);
            if (e) hit = matchOne(e->text);
        } else if (colSel < 0) {
            // "Any column" — fall back to proxy data() but try to
            // hit the message column first since it has the most
            // hits in practice. Short-circuits on first hit so we
            // pay for it only on misses.
            for (int c = 0; c < cols; ++c) {
                const QString cell = proxy->data(proxy->index(r, c),
                                                  Qt::DisplayRole).toString();
                if (matchOne(cell)) { hit = true; break; }
            }
        } else {
            // Specific non-message column — uses proxy data(). Rare
            // enough that we don't bother optimizing.
            const QString cell = proxy->data(proxy->index(r, colSel),
                                              Qt::DisplayRole).toString();
            hit = matchOne(cell);
        }

        if (hit) m_matches.append(r);
    }
    m_lastScanWasCapped = m_lastScanCapped;
}

void FindBar::publishHits()
{
    QAbstractItemModel *m = m_view ? m_view->model() : nullptr;
    auto *sortProxy = qobject_cast<QSortFilterProxyModel *>(m);
    auto *lm = sortProxy ? qobject_cast<LogModel *>(sortProxy->sourceModel())
                         : qobject_cast<LogModel *>(m);
    if (!lm) { return; }

    if (!isVisible() || !m_markAll || !m_markAll->isChecked() || m_matches.isEmpty()) {
        lm->clearFindHits();
        return;
    }

    QSet<const LogEntry *> hits;
    hits.reserve(m_matches.size());
    for (int proxyRow : m_matches) {
        if (const LogEntryPtr e = entryAtProxyRow(proxyRow)) { hits.insert(e.data()); }
    }
    lm->setFindHits(hits);
}

void FindBar::findNext()
{
    if (m_matches.isEmpty()) return;
    const int last = m_matches.size() - 1;

    if (m_cursor < 0) {
        m_cursor = 0;
        m_wrapHint->clear();
    } else if (m_cursor < last) {
        ++m_cursor;
        m_wrapHint->clear();
    } else if (m_wrapCb->isChecked()) {
        m_cursor = 0;
        m_wrapHint->setText(tr("(wrapped)"));
    } else {
        // Wrapping is off: stay where we are and say so, rather than
        // silently doing nothing — a dead Next button reads as a bug.
        m_wrapHint->setText(tr("(last match)"));
    }

    jumpToCurrentMatch();
    updateCountLabel();
    refreshHold();
}

void FindBar::findPrev()
{
    if (m_matches.isEmpty()) return;

    if (m_cursor > 0) {
        --m_cursor;
        m_wrapHint->clear();
    } else if (m_wrapCb->isChecked()) {
        m_cursor = m_matches.size() - 1;
        m_wrapHint->setText(tr("(wrapped)"));
    } else {
        if (m_cursor < 0) { m_cursor = 0; }
        m_wrapHint->setText(tr("(first match)"));
    }

    jumpToCurrentMatch();
    updateCountLabel();
    refreshHold();
}

void FindBar::jumpToCurrentMatch()
{
    if (m_cursor < 0 || m_cursor >= m_matches.size()) return;
    const int row = m_matches[m_cursor];
    QAbstractItemModel *proxy = m_view ? m_view->model() : nullptr;
    if (!proxy) return;
    const QModelIndex idx = proxy->index(row, LogModel::ColMessage);
    m_view->setCurrentIndex(idx);
    m_view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
}

void FindBar::updateCountLabel()
{
    // Reset style first — onTextChanged sets italic gray "searching…"
    // while the debounce is running; we need to clear that styling when
    // the scan completes and we're showing real results.
    m_count->setStyleSheet(UiColor::mutedStyle());

    if (m_edit->text().isEmpty()) {
        m_count->setText(tr("0 matches"));
        return;
    }
    if (m_matches.isEmpty()) {
        m_count->setText(m_lastScanCapped
                         ? tr("no matches in first %1 rows").arg(m_lastScanRows)
                         : tr("no matches"));
        m_count->setStyleSheet(UiColor::errorStyle());
        return;
    }
    // Show "X of Y" or "X of Y+" when capped. The "+" hints to the
    // operator that more matches likely exist beyond what we scanned.
    const QString text = m_lastScanCapped
        ? tr("%1 of %2+ (first %3 rows)")
              .arg(m_cursor + 1)
              .arg(m_matches.size())
              .arg(m_lastScanRows)
        : tr("%1 of %2")
              .arg(m_cursor + 1)
              .arg(m_matches.size());
    m_count->setText(text);
}

void FindBar::setContextLabel(const QString &label)
{
    m_contextLabel = label;
    if (m_host) {
        m_host->setWindowTitle(label.isEmpty() ? tr("Find")
                                               : tr("Find — %1").arg(label));
    }
}

QString FindBar::toLogQuery(Mode m, const QString &text, QString *error)
{
    if (error) error->clear();
    const QString t = text.trimmed();
    if (t.isEmpty()) return QString();
    auto phrase = [error](const QString &s) -> QString {
        // A quoted phrase cannot hold a quote; a regex can.
        if (!s.contains(QLatin1Char('"'))) return QStringLiteral("\"%1\"").arg(s);
        if (s.contains(QLatin1Char('/'))) {
            if (error) *error = QObject::tr("text with both \" and / cannot be passed to All tabs");
            return QString();
        }
        return QStringLiteral("/%1/").arg(QRegularExpression::escape(s));
    };
    switch (m) {
    case Mode::Text:
        return phrase(text);
    case Mode::Extended: {
        const Unescaped u = unescapeExtended(text);
        if (!u.ok) { if (error) *error = u.error; return QString(); }
        return phrase(u.text);
    }
    case Mode::Regex:
        if (t.contains(QLatin1Char('/'))) {
            if (error) *error = QObject::tr("a regex containing / cannot be passed to All tabs");
            return QString();
        }
        return QStringLiteral("/%1/").arg(t);
    case Mode::Hex:
        return QStringLiteral("hex:\"%1\"").arg(t);
    case Mode::Query:
        return t;
    }
    return QString();
}
