#include "testutil.h"

#include <QAction>

#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

// =============================================================================
//  Session 437 — a form beside the Station Layout drawing for what is
//  selected: a tag (its location, re-encoded on Apply; type, kind, TIN, CRC,
//  line shown; Edit in Tag Builder), a signal (name, foot tag, sig_id,
//  station, route, main head, calling-on, shunt, route indicator), a point
//  (name, lines, locations, tags; thrown from there when the station's state
//  is on the drawing). Apply: one Undo step. Form (top bar) hides it.
//  On station_527.xml (real: LPI).
// =============================================================================

TEST_SUITE(session437)
{
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml");
    w.resize(1280, 820);
    w.show();
    QWidget *f = w.formPanel();
    auto *btn = w.findChild<QAction *>(QStringLiteral("stationLayoutFormButton"));
    CHECK(f && !f->isVisible() && btn && !btn->isChecked(), "hidden at first: the drawing keeps its width");
    const int wide = w.canvas()->parentWidget()->width();
    if (btn) btn->trigger();
    CHECK(f && f->isVisible() && w.formKind() == 0, "Form: beside the drawing, nothing selected yet");

    // ---- a signal, clicked on the drawing ----
    int s48 = -1;
    for (int i = 0; i < w.station().signalList.size(); ++i) if (w.station().signalList.at(i).name == QStringLiteral("S48")) { s48 = i; break; }
    emit w.canvas()->signalClicked(s48);
    CHECK(w.formKind() == 2 && w.formIndex() == s48, "a signal clicked: its form");
    auto *name = f->findChild<QLineEdit *>(QStringLiteral("formSignalName"));
    auto *aspects = f->findChild<QComboBox *>(QStringLiteral("formSignalAspects"));
    auto *ind = f->findChild<QComboBox *>(QStringLiteral("formSignalIndicator"));
    auto *callOn = f->findChild<QCheckBox *>(QStringLiteral("formSignalCallingOn"));
    auto *foot = f->findChild<QComboBox *>(QStringLiteral("formSignalFoot"));
    CHECK(name && name->text() == QStringLiteral("S48") && foot && foot->currentText() == QStringLiteral("12") && aspects
              && aspects->currentData().toInt() == -1 && ind && ind->currentData().toInt() == 0,
          "its fields: S48 at tag 12, head from its name, no route indicator");
    if (aspects && ind && callOn) {
        aspects->setCurrentIndex(aspects->findData(3));
        ind->setCurrentIndex(ind->findData(2));
        callOn->setChecked(true);
    }
    CHECK(w.applyForm(), "Apply");
    const StationLayout::Signal now = w.station().signalList.at(s48);
    CHECK(now.aspects == 3 && now.indicator == 2 && now.callingOn && now.name == QStringLiteral("S48"), "S48: 3 aspects, indicator 2, calling-on");
    CHECK(w.undo() && w.station().signalList.at(s48).aspects == -1 && w.formKind() == 2
              && f->findChild<QComboBox *>(QStringLiteral("formSignalAspects"))->currentData().toInt() == -1,
          "Undo: one step, and the form shows it");
    name = f->findChild<QLineEdit *>(QStringLiteral("formSignalName"));
    foot = f->findChild<QComboBox *>(QStringLiteral("formSignalFoot"));
    if (foot) foot->setCurrentText(QStringLiteral("99999"));
    CHECK(!w.applyForm() && w.station().signalList.at(s48).footTag == QStringLiteral("12"), "a foot tag not in the layout: refused");

    // ---- a tag ----
    w.selectSheetRow(0, 0);
    auto *loc = f->findChild<QLineEdit *>(QStringLiteral("formTagLocation"));
    CHECK(w.formKind() == 1 && loc && loc->text() == QStringLiteral("163960"), "tag 981: its location");
    bool crc = false;
    for (QLabel *l : f->findChildren<QLabel *>()) crc = crc || l->text() == QStringLiteral("good");
    CHECK(crc, "and its CRC-30 good");
    if (loc) loc->setText(QStringLiteral("163970"));
    CHECK(w.applyForm() && StationLayout::info(w.station().tags.at(0)).absLoc == 163970 && StationLayout::info(w.station().tags.at(0)).crcOk,
          "Apply: tag 981 re-encoded at 163970 m, CRC-30 recomputed");

    // ---- a point ----
    int p19 = -1;
    for (int i = 0; i < w.station().points.size(); ++i) if (w.station().points.at(i).name == QStringLiteral("P19")) p19 = i;
    emit w.canvas()->pointClicked(p19);
    auto *loc2 = f->findChild<QLineEdit *>(QStringLiteral("formPointLoc2"));
    CHECK(w.formKind() == 3 && loc2 && loc2->text() == QStringLiteral("161409"), "point P19: its form, to DM at 161409 m");
    if (loc2) loc2->setText(QStringLiteral("161420"));
    CHECK(w.applyForm() && w.station().points.at(p19).loc2 == 161420, "Apply: P19 reaches DM at 161420 m");
    CHECK(w.setLivePoint(QStringLiteral("P19"), true), "the station's state on the drawing (what-if), P19 Reverse");
    bool thr = false;
    for (QPushButton *b : f->findChildren<QPushButton *>()) thr = thr || b->text() == QStringLiteral("Reverse: throw to Normal");
    CHECK(thr, "the form throws it from there");

    // ---- Form off ----
    if (btn) btn->trigger();
    QCoreApplication::processEvents();
    CHECK(btn && !f->isVisible() && w.canvas()->parentWidget()->width() >= wide - 2, "Form off: hidden, the drawing its width again");
}
