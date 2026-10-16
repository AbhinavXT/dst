#ifndef VCCLISTEDIT_H
#define VCCLISTEDIT_H
// =============================================================================
//  VccListEdit -- the Firmware Flasher's list of VCCs (session 292)
//  ---------------------------------------------------------------------------
//  One row per VCC: a tick box (untick to leave it out of this run), its IP
//  and a × that removes it. "+ Add VCC" adds the next address (.168 after
//  .167); "Add range…" adds several at once ("192.168.25.168-175"). A row
//  whose address is wrong, or listed twice, is drawn in the danger colour
//  with the reason in its tooltip. At most Flasher::kMaxParallelVccs rows.
//
//  The rest of the flasher still speaks the session-291 text form: text()
//  is the ticked rows joined by ", ", and setText() takes the same list or
//  range syntax a profile stores. One row is the single-VCC flasher as it
//  always was.
// =============================================================================
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

class VccListEdit : public QWidget
{
    Q_OBJECT
public:
    explicit VccListEdit(QWidget *parent = nullptr);

    // A list in the text form ("a, b", "a.b.c.168-175", "ip:port"): one
    // row each, all ticked. An entry that is not an address keeps its own
    // row, marked, so a typo in a profile is shown rather than dropped.
    void    setText(const QString &text);
    QString text() const;                 // the ticked, non-blank rows, joined by ", "

    int  rowCount() const { return m_rows.size(); }
    int  tickedCount() const;
    QLineEdit *edit(int row) const;
    QCheckBox *tick(int row) const;
    bool rowIsMarked(int row) const;      // drawn as wrong (bad address, or a duplicate)
    QString summary() const;              // "3 of 4 VCCs ticked — flashed at once"

    // The buttons' work, public for the tests.
    void addRow(const QString &ip = QString(), bool ticked = true);
    void removeRow(int row);              // the last row is cleared, not removed
    void addNext();                       // + Add VCC
    bool addRange(const QString &range, QString *error);   // Add range… after its prompt

signals:
    void changed();

private:
    struct Row {
        QWidget     *widget = nullptr;
        QCheckBox   *tick = nullptr;
        QLineEdit   *edit = nullptr;
        QToolButton *remove = nullptr;
        bool         marked = false;
    };
    void    refresh();                    // marks, summary, buttons; emits changed()
    void    askForRange();
    QString nextAddress() const;

    QVector<Row>  m_rows;
    QVBoxLayout  *m_rowsLayout = nullptr;
    QPushButton  *m_addButton = nullptr;
    QPushButton  *m_rangeButton = nullptr;
    QLabel       *m_summary = nullptr;
    bool          m_quiet = false;        // inside setText: one changed() at the end
};

#endif  // VCCLISTEDIT_H
