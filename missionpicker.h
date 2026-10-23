#ifndef MISSIONPICKER_H
#define MISSIONPICKER_H

// =============================================================================
//  missionpicker.h -- "Mission ▾" in the log header (session 339)
//  ---------------------------------------------------------------------------
//  A day's capture holds several missions (session 171). Looking at one of
//  them meant reading its times off the mission report and typing an
//  after: / before: query into each tab. The button lists the missions of
//  the tab on show; choosing one filters every tab to its span, as a
//  visible query, and "All missions" takes that filter off again.
// =============================================================================

#include <QToolButton>

#include <functional>

class LogModel;

class MissionPicker : public QToolButton
{
    Q_OBJECT
public:
    explicit MissionPicker(QWidget *parent = nullptr);

    // Where the list comes from: the model of the tab on show (may return
    // null). Asked when the menu opens, so it is always the tab on show.
    void setSource(std::function<const LogModel *()> source) { m_source = std::move(source); }
    // The chosen mission's number, -1 for all. Changes the label only.
    void setCurrent(int index);
    int current() const { return m_current; }

    // "Mission 2 · 14:02–15:31 · Full_Supervision · 1h 29m", the menu's text.
    static QString itemText(int index, qint64 fromMs, qint64 toMs, const QString &highestMode);

signals:
    void missionChosen(int index, qint64 fromMs, qint64 toMs);
    void allChosen();

private:
    void rebuildMenu();
    std::function<const LogModel *()> m_source;
    int m_current = -1;
};

#endif  // MISSIONPICKER_H
