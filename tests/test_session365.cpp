#include "testutil.h"

#include "bookmarks.h"
#include "colorrules.h"
#include "dmiclipdialog.h"
#include "incidentreportwindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "radiochart.h"
#include "radiochartdialog.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

// =============================================================================
//  Session 365 — the review tools in the recorded-session (.dlr) viewer: a
//  file sent from the test site gets the DMI clip, the radio chart, notes on
//  rows and the incident pack, as a live tab does. Real frames:
//  replay/loco_1_1_27062026_140226.cap, 14:02-14:05, written to two .dlr
//  files the way Save does: the @dmi frames in one (source 81), the rest in
//  the other (source 21), so the clip has to find the DMI in another tab.
// =============================================================================

namespace {

qint64 at365(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

QString writeDlr365(const QString &path, LogModel *m, quint8 src)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = at365("14:02:00");
    h.sourceId = src;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

// The view of tab `key` in `w` and its LogModel.
QTableView *viewOf(SessionWindow &w, int rows)
{
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->model() && tv->model()->rowCount() == rows) return tv;
    return nullptr;
}

QStringList popupNames;
void grabPopup()
{
    if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
        for (QAction *a : m->actions())
            if (!a->objectName().isEmpty()) popupNames << a->objectName();
        m->close();
    }
}

}  // namespace

TEST_SUITE(session365)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        const qint64 from = at365("14:02:00"), to = at365("14:05:00");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < from) continue;
            if (ms >= to) break;
            disp.ingestLocal(l.startsWith("@dmi_") ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && radio->count() > 500 && dmi->count() > 100, "fixture: radio rows in 21, @dmi rows in 81");
    if (!radio || !dmi) return;
    const int radioRows = radio->count(), dmiRows = dmi->count();

    QTemporaryDir tmp;
    const QString p21 = writeDlr365(tmp.filePath(QStringLiteral("21_1.dlr")), radio, 21);
    const QString p81 = writeDlr365(tmp.filePath(QStringLiteral("81_1.dlr")), dmi, 81);

    BookmarkStore marks;
    marks.load(tmp.filePath(QStringLiteral("bookmarks.json")));
    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.setBookmarkStore(&marks);
    CHECK(w.loadFiles({ p21, p81 }) == radioRows + dmiRows, "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QTableView *view = viewOf(w, radioRows);
    CHECK(view != nullptr, "the radio tab's table");
    if (!view) return;
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
    auto *model = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
    CHECK(model != nullptr, "fixture: the view's LogModel");
    if (!model) return;
    const int mid = model->count() / 2;
    const LogEntryPtr row = model->entryAt(mid);
    CHECK(row && !row->text.startsWith(QLatin1String("@dmi")), "fixture: a radio-tab row, not @dmi");
    if (!row) return;

    // ---- Tools ▸ Incident report…, and the row menu ------------------------------------------
    bool haveTool = false;
    for (QAction *m : w.menuBar()->actions())
        if (m->menu())
            for (QAction *a : m->menu()->actions())
                if (a->objectName() == QLatin1String("sessionIncidentReport")) haveTool = true;
    CHECK(haveTool, "Tools ▸ Incident report… in the recorded-session window");

    view->setCurrentIndex(proxy->mapFromSource(model->index(mid, 0)));
    popupNames.clear();
    QTimer::singleShot(0, grabPopup);
    emit view->customContextMenuRequested(view->visualRect(view->currentIndex()).center());
    CHECK(popupNames.contains(QLatin1String("rowNote")) && popupNames.contains(QLatin1String("rowDmiClip"))
              && popupNames.contains(QLatin1String("rowRadioChart"))
              && popupNames.contains(QLatin1String("rowIncidentReport")),
          "the row menu offers Add note…, DMI clip, Radio chart and Incident report: "
              + popupNames.join(QLatin1Char(',')).toUtf8());

    // ---- DMI clip: the @dmi is in the other tab ------------------------------------------------
    {
        QString why;
        DmiClipDialog *clip = w.makeClipDialog(QStringLiteral("21_1"), row, &why);
        CHECK(clip && clip->locos().contains(QStringLiteral("1_1")),
              "a clip from a radio row finds loco 1_1's @dmi in the other tab: " + why.toUtf8());
        if (clip) {
            const QString gif = tmp.filePath(QStringLiteral("clip.gif"));
            CHECK(clip->exportTo(gif) && QFile(gif).size() > 1000,
                  "the clip writes a GIF: " + clip->status()->text().toUtf8());
            delete clip;
        }
    }
    {
        SessionWindow only(&rules, &names, Theme::Light);
        only.setAttribute(Qt::WA_DeleteOnClose, false);
        only.loadFiles({ p21 });
        QString why;
        DmiClipDialog *none = only.makeClipDialog(QStringLiteral("21_1"), row, &why);
        CHECK(!none && why.contains(QLatin1String("no @dmi")), "no @dmi in the recording: refused, and why: " + why.toUtf8());
        delete none;
    }

    // ---- Radio chart ----------------------------------------------------------------------------
    {
        RadioChartDialog *chart = w.makeRadioChart(QStringLiteral("21_1"), row->epochMs);
        CHECK(chart && chart->chart().error.isEmpty() && !chart->chart().messages.isEmpty(),
              "the radio chart around the row has arrows: " + (chart ? chart->chart().summary().toUtf8() : QByteArray()));
        if (chart) {
            const QString svg = tmp.filePath(QStringLiteral("radio.svg"));
            CHECK(chart->exportTo(svg) && QFile(svg).size() > 1000, "and exports as SVG");
            delete chart;
        }
        CHECK(w.makeRadioChart(QStringLiteral("99_9"), row->epochMs) == nullptr, "no such tab: nothing");
    }

    // ---- A note, then the incident pack ---------------------------------------------------------
    CHECK(!row->bookmarked && w.noteOf(QStringLiteral("21_1"), row).isEmpty(), "the row starts unmarked");
    CHECK(w.setRowNote(QStringLiteral("21_1"), row, QStringLiteral("site: EB felt late here")),
          "Add note… keeps the note");
    CHECK(row->bookmarked && marks.has(QStringLiteral("21_1"), row->epochMs)
              && w.noteOf(QStringLiteral("21_1"), row) == QLatin1String("site: EB felt late here"),
          "the row is bookmarked, with the note, in the shared store");
    CHECK(w.setRowNote(QStringLiteral("21_1"), row, QStringLiteral("site: EB late, 2nd look")) && marks.count() == 1,
          "Edit note… changes the note, still one bookmark");

    IncidentReportWindow *pack = w.openIncidentReport(QStringLiteral("21_1"), row->epochMs, 60'000, 30'000);
    CHECK(pack && pack->summary().valid, "the incident pack opens over the recording");
    if (pack) {
        CHECK(pack->summary().notes.size() == 1
                  && pack->summary().notes.first().note == QLatin1String("site: EB late, 2nd look")
                  && pack->html().contains(QLatin1String("site: EB late, 2nd look")),
              "the note made in the viewer is the pack's Operator note");
        CHECK(pack->summary().fromMs == row->epochMs - 60'000 && pack->summary().toMs == row->epochMs + 30'000,
              "the pack's window is the one asked for");
        delete pack;   // WA_DeleteOnClose: deleted here, not closed
    }
    CHECK(w.openIncidentReport(QStringLiteral("99_9"), row->epochMs, 1000, 1000) == nullptr, "no such tab: no pack");

    // Without a store the note is refused out loud, not dropped.
    {
        SessionWindow bare(&rules, &names, Theme::Light);
        bare.setAttribute(Qt::WA_DeleteOnClose, false);
        bare.loadFiles({ p21 });
        CHECK(!bare.setRowNote(QStringLiteral("21_1"), row, QStringLiteral("x")), "no bookmark store: the note is refused");
    }
}
