#ifndef FRAMEDIFFWINDOW_H
#define FRAMEDIFFWINDOW_H
// =====================================================================
//  framediffwindow.{h,cpp} -- "what changed between these two frames?"
//
//  Two inputs (a pasted capture line, bare hex + a type, or two rows sent
//  here from the log), decoded through the same path the log view uses,
//  and rendered as a field-by-field table: unchanged rows greyed, changed
//  rows highlighted with both values side by side, rows present on only
//  one side marked as such.
//
//  All the comparison logic lives in framediff.{h,cpp}; this is the
//  renderer. Show-only-differences is on by default, because the reason
//  to open this window is a frame with one unexpected field in it.
// =====================================================================
#include <QMainWindow>

#include "statusline.h"

#include "logentry.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QGroupBox;
class QTableWidget;

class FrameDiffWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit FrameDiffWindow(QWidget *parent = nullptr);

    // How many frames can be compared at once. Six is a working limit, not a
    // technical one: past that the table is wider than the answer.
    static int maxFrames();

    // Load one frame. Adds input boxes as needed, so setSide(2, …) on a
    // two-frame window grows it to three.
    void setSide(int side, const QString &text);

    // Two frames, the common entry point from the log.
    void setEntries(const LogEntryPtr &left, const LogEntryPtr &right);

    // Any number of rows, up to maxFrames(). Three answers the question two
    // cannot: which of these is the odd one out.
    void setEntries(const QVector<LogEntryPtr> &entries);

    int frameCount() const { return m_in.size(); }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void rebuild();               // re-decode both sides and redraw the table

    void addFrameColumn();        // one more input box
    void removeFrameColumn();

    QVector<QPlainTextEdit *> m_in;
    QVector<QComboBox *>      m_type;
    QVector<QLabel *>         m_head;
    QVector<QGroupBox *>      m_box;
    class QHBoxLayout *m_inputs = nullptr;
    class QPushButton *m_addBtn = nullptr;
    class QPushButton *m_delBtn = nullptr;
    QCheckBox      *m_onlyDiff = nullptr;
    QTableWidget   *m_table    = nullptr;
    StatusLine     *m_status   = nullptr;
    bool            m_updating = false;
};

#endif  // FRAMEDIFFWINDOW_H
