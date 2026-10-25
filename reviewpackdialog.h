#ifndef REVIEWPACKDIALOG_H
#define REVIEWPACKDIALOG_H

// =============================================================================
//  File ▸ Export review pack… (session 367) — the dialog over reviewpack.h.
//  Who and where (remembered for next time), what to look at, which tabs,
//  the span (everything, or From / To), and whether the open test run goes
//  in. It says, as they change, how many frames and notes that is; Export…
//  writes the .zip and says what went in.
// =============================================================================

#include <QDialog>

#include "reviewpack.h"

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class StatusLine;

class ReviewPackDialog : public QDialog
{
    Q_OBJECT
public:
    // `captures`: every tab that can go in, the one in view first (checked
    // with the rest). `base`: schema, config files and test run, filled in by
    // the caller; the dialog adds who, what, tabs and span.
    ReviewPackDialog(const QVector<ReviewPack::Capture> &captures, const ReviewPack::Input &base,
                     QWidget *parent = nullptr);

    ReviewPack::Input input() const;
    // Write the pack to `path` (what Export does after its file dialog).
    // False, with the reason on the status line, when refused.
    bool exportTo(const QString &path);

    void setWho(const QString &site, const QString &tester, const QString &loco);
    void setWhat(const QString &text);
    void setSpan(qint64 fromMs, qint64 toMs);   // 0, 0: everything
    void setTabChecked(const QString &tabKey, bool on);
    QLabel *countLabel() const { return m_count; }
    StatusLine *status() const { return m_status; }

private:
    void refreshCount();

    QVector<ReviewPack::Capture> m_captures;
    ReviewPack::Input m_base;
    QLineEdit *m_site = nullptr, *m_tester = nullptr, *m_loco = nullptr;
    QPlainTextEdit *m_what = nullptr;
    QListWidget *m_tabs = nullptr;
    QRadioButton *m_all = nullptr, *m_range = nullptr;
    QDateTimeEdit *m_from = nullptr, *m_to = nullptr;
    QCheckBox *m_run = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_export = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // REVIEWPACKDIALOG_H
