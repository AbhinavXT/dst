#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QTimer>

// =============================================================================
//  Session 438 — a new signal on the drawing, its heads chosen at once:
//  right-click its foot tag ▸ New signal at tag…: name, foot tag, sig_id,
//  station, route, main head, calling-on, shunt, route indicator, and what
//  its foot tag says about the way it faces. On station_527.xml.
// =============================================================================

namespace {

// Fills the dialog as an operator would, once it is up.
void answer(const QString &name, int aspects, bool callingOn, int indicator, bool ok, QString *said)
{
    QTimer::singleShot(0, [=]() {
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d || d->objectName() != QLatin1String("newSignalDialog")) return;
        for (QLabel *l : d->findChildren<QLabel *>()) if (l->text().startsWith(QLatin1String("Its foot tag"))) *said = l->text();
        d->findChild<QLineEdit *>(QStringLiteral("formSignalName"))->setText(name);
        auto *a = d->findChild<QComboBox *>(QStringLiteral("formSignalAspects"));
        a->setCurrentIndex(a->findData(aspects));
        d->findChild<QCheckBox *>(QStringLiteral("formSignalCallingOn"))->setChecked(callingOn);
        auto *i = d->findChild<QComboBox *>(QStringLiteral("formSignalIndicator"));
        i->setCurrentIndex(i->findData(indicator));
        d->findChild<QDialogButtonBox *>()->button(ok ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
}

} // namespace

TEST_SUITE(session438)
{
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml");
    const int before = w.station().signalList.size();
    QString said;
    answer(QStringLiteral("S50X"), 4, true, 1, true, &said);
    CHECK(w.newSignalAt(QStringLiteral("2")), "a new signal at tag 2");
    CHECK(said.contains(QStringLiteral("Signal foot, nominal")), "the dialog says what tag 2 says: signal foot, nominal");
    const StationLayout::Signal s = w.station().signalList.last();
    CHECK(w.station().signalList.size() == before + 1 && s.name == QStringLiteral("S50X") && s.footTag == QStringLiteral("2")
              && s.aspects == 4 && s.callingOn && s.indicator == 1 && s.stationId == 527,
          "S50X at tag 2: 4 aspects, calling-on, route indicator 1, station 527");
    answer(QStringLiteral("S51"), 3, false, 0, false, &said);
    CHECK(!w.newSignalAt(QStringLiteral("2")) && w.station().signalList.size() == before + 1, "Cancel: nothing added");
    answer(QString(), 3, false, 0, true, &said);
    CHECK(!w.newSignalAt(QStringLiteral("2")) && w.station().signalList.size() == before + 1, "no name: refused");
    CHECK(w.undo() && w.station().signalList.size() == before, "Undo takes S50X away");
    QMenu *m = w.contextMenuFor(QStringLiteral("2"), QString(), 0, false);
    bool entry = false;
    for (QAction *a : m->actions()) entry = entry || a->text() == QStringLiteral("New signal at tag 2…");
    delete m;
    CHECK(entry, "right-click tag 2: New signal at tag 2…");
}
