#ifndef FILTERBAR_H
#define FILTERBAR_H

// =============================================================================
//  FilterBar
//  -----------------------------------------------------------------------------
//  The strip of controls that sits above each tab's QTableView. It owns the
//  QSortFilterProxyModel that the view actually displays, so MainWindow
//  doesn't have to track per-tab proxies separately — they live with the
//  filter widget.
//
//  Controls (left to right):
//    [ ✕ Clear ]  [ Filter:  ⏎ text input … ]  [ ☐ Regex ]
//    [ Match: ▾ Message ]  [ All | Errors | Warns | IN | OUT ]
//    [ Showing N of M ]
//
//  Behaviour:
//    – Free-text filter is debounced 150 ms so typing each character does
//      not re-filter a 200,000-row table immediately.
//    – Regex toggle changes the proxy from FixedString matching to RegExp.
//    – Match-column dropdown sets which column the text filter targets.
//      Default is Message; sometimes operators want to filter by Source or
//      Direction instead.
//    – Severity / direction chips are an AND with the text filter:
//      "Errors only" + "RAD" shows Errors that mention RAD.
//    – Counts label updates whenever the proxy model's row count changes.
//
//  This widget does NOT own the source LogModel — that's the dispatcher's
//  property. It owns ONLY the proxy. When the FilterBar is destroyed the
//  proxy goes with it; the underlying model lives on.
// =============================================================================

#include <QWidget>

class LogModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QueryLineEdit;
class QPushButton;
class NameMap;
class QSortFilterProxyModel;
class QTimer;
class QButtonGroup;

class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(LogModel *source, QWidget *parent = nullptr);

    // Supplies friendly names to name:/bare-term matching in query mode.
    void setNameMap(const NameMap *names);

protected:
    // Turns a click on the error label into "select the bad term".
    bool eventFilter(QObject *watched, QEvent *event) override;

public:

    // Whatever is currently typed in the filter box. Used to seed the
    // cross-tab search window, so "now show me everywhere" doesn't mean
    // retyping the expression.
    // Programmatically set the box to a query expression and switch to
    // query mode. Used by the timeline ribbon's range drag.
    void setQuery(const QString &expression);

    QString filterText() const;
    bool    queryModeOn() const;
    ~FilterBar() override;

    // The proxy that the QTableView should be set on. Owned by this widget.
    QSortFilterProxyModel *proxyModel() const { return m_proxy; }

private slots:
    void onTextChanged();        // debounce trigger
    void onDebounceTimeout();    // debounced apply
    void onRegexToggled(bool);
    void onColumnChanged(int);
    void onChipClicked(int id);
    void onClearClicked();
    void updateCountsLabel();

private:
    void applyFilter();

    LogModel              *m_source = nullptr;
    QSortFilterProxyModel *m_proxy  = nullptr;

    QueryLineEdit *m_edit       = nullptr;
    QCheckBox  *m_regexCb    = nullptr;
    QCheckBox  *m_queryCb    = nullptr;
    QLabel     *m_queryError = nullptr;
    QComboBox  *m_columnBox  = nullptr;
    QButtonGroup *m_chipGroup = nullptr;
    QLabel     *m_countLabel = nullptr;
    QPushButton *m_clearBtn  = nullptr;

    QTimer     *m_debounce   = nullptr;

    // Chip selection (mirrors the QButtonGroup id of whichever chip is
    // checked). 0 = All, 1 = Errors, 2 = Warns, 3 = IN, 4 = OUT.
    int m_activeChip = 0;
};

#endif // FILTERBAR_H
