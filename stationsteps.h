#ifndef STATIONSTEPS_H
#define STATIONSTEPS_H
// =====================================================================
//  stationsteps.{h,cpp} -- session 459: the guided steps beside Station
//  Layout's drawing (View ▸ Steps beside the drawing).
//
//  One row per step (stationbuild.h), each with its rows and its problems
//  ("3. Tags (RFID)  42  ⚠ 2"); under it what goes in the chosen step,
//  and its problems: a click on one goes to its row. Back / Next walk
//  the steps; New station… asks Station Layout for the wizard. The
//  panel shows and asks; Station Layout does the work.
// =====================================================================
#include "stationbuild.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class StationStepsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StationStepsPanel(QWidget *parent = nullptr);
    void setSteps(const QVector<StationBuild::StepState> &steps);
    // Without stepChosen: the window following its own tab.
    void setCurrentStep(int step);
    int currentStep() const;
    QListWidget *stepList() const { return m_list; }
    QListWidget *problemList() const { return m_problems; }
    QPushButton *newStationButton() const { return m_new; }
    QLabel *helpLabel() const { return m_help; }

signals:
    void stepChosen(int step);
    void problemChosen(int sheet, int row);
    void newStationRequested();

private:
    void showStep();
    QVector<StationBuild::StepState> m_steps;
    QListWidget *m_list = nullptr;
    QListWidget *m_problems = nullptr;
    QLabel *m_help = nullptr;
    QLabel *m_problemsTitle = nullptr;
    QPushButton *m_new = nullptr, *m_back = nullptr, *m_next = nullptr;
    bool m_quiet = false;
};

#endif // STATIONSTEPS_H
