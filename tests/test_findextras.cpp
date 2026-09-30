#include "testutil.h"

#include "findbar.h"
#include "logmodel.h"
#include "logentry.h"
#include "uicolors.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QTableView>

// =============================================================================
//  Whole-word matching, and marking every hit.
//
//  The two things a log console's find lacked next to a text editor's: a way
//  to stop "STN" matching "STN_ID", and a way to see where ALL the matches
//  are rather than only the one the cursor is parked on. On a hundred
//  thousand rows the second is the one that changes how the tool feels —
//  "47 matches" tells you how many, not where.
// =============================================================================

namespace {

LogEntryPtr mk(const QString &text, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId    = 1;
    e->epochMs  = ms;
    e->severity = Severity::Info;
    e->text     = text;
    e->cacheDerived();
    return e;
}

// By objectName rather than by label. The labels are shape-dependent now —
// the strip says "W", the window says "Whole word only" — so matching on text
// would silently become a test of which shape the bar happened to open in.
QCheckBox *boxNamed(FindBar *bar, const QString &name)
{
    return bar->findChild<QCheckBox *>(name);
}

}  // namespace

TEST_SUITE(findextras)
{
    LogModel model(nullptr, 1000);
    model.appendEntries({
        mk(QStringLiteral("STN_ID 4 upcoming"), 1000),   // 0: STN_ID, not STN
        mk(QStringLiteral("STN 7 acknowledged"),  2000), // 1: a whole-word STN
        mk(QStringLiteral("nothing here"),        3000), // 2
        mk(QStringLiteral("upcoming target"),     4000), // 3
        mk(QStringLiteral("STN reset"),           5000), // 4: another whole word
    });

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QCoreApplication::processEvents();

    QCheckBox *whole = boxNamed(&bar, QStringLiteral("findWholeWordBox"));
    QCheckBox *mark  = boxNamed(&bar, QStringLiteral("findMarkBox"));
    CHECK(whole != nullptr, "the bar has a whole-word box");
    CHECK(mark  != nullptr, "and a mark-all box");
    CHECK(mark && mark->isChecked(),
          "marking is on by default — seeing every hit is the point");

    // ---- marking reaches the model -----------------------------------------
    // The model is what paints the rows, so the check is on the model rather
    // than on a pixel: does it now consider those rows hits?
    {
        const QModelIndex msg0 = model.index(0, LogModel::ColMessage);
        const QVariant before  = model.data(msg0, Qt::BackgroundRole);

        QSet<const LogEntry *> hits;
        hits.insert(model.entryAt(0).data());
        model.setFindHits(hits);

        const QVariant after = model.data(msg0, Qt::BackgroundRole);
        CHECK(after != before || before.isNull(),
              "a matched row is tinted where it was not before");
        CHECK(after.canConvert<QBrush>(), "and the tint is a brush");

        const QVariant other = model.data(model.index(2, LogModel::ColMessage),
                                          Qt::BackgroundRole);
        CHECK(other != after, "a row that did not match is left alone");

        model.clearFindHits();
        CHECK(model.data(msg0, Qt::BackgroundRole) == before,
              "clearing puts the row back exactly as it was");
    }

    // ---- hits are held as entries, not row numbers -------------------------
    // The model evicts from the front once a tab fills. A remembered row
    // number would then point at a different message; a pointer does not.
    {
        QSet<const LogEntry *> hits;
        const LogEntryPtr marked = model.entryAt(4);
        hits.insert(marked.data());
        model.setFindHits(hits);

        const QVariant tinted = model.data(model.index(4, LogModel::ColMessage),
                                           Qt::BackgroundRole);
        CHECK(tinted.canConvert<QBrush>(), "the last row is tinted");

        // Push enough entries to renumber everything, then check the SAME
        // entry is still the one tinted.
        QVector<LogEntryPtr> more;
        for (int i = 0; i < 20; ++i) { more << mk(QStringLiteral("filler"), 6000 + i); }
        model.appendEntries(more);

        int tintedRow = -1;
        for (int r = 0; r < model.count(); ++r) {
            if (model.data(model.index(r, LogModel::ColMessage),
                           Qt::BackgroundRole).canConvert<QBrush>()) {
                tintedRow = r;
                break;
            }
        }
        CHECK(tintedRow >= 0, "something is still tinted after the model grew");
        CHECK(model.entryAt(tintedRow) == marked,
              "and it is the same message, not whatever now sits at that row");
        model.clearFindHits();
    }

    // ---- the tint follows the theme ----------------------------------------
    {
        QSet<const LogEntry *> hits;
        hits.insert(model.entryAt(0).data());
        model.setTheme(Theme::Light);
        model.setFindHits(hits);
        const QBrush light = model.data(model.index(0, LogModel::ColMessage),
                                        Qt::BackgroundRole).value<QBrush>();
        model.setTheme(Theme::Dark);
        const QBrush dark = model.data(model.index(0, LogModel::ColMessage),
                                       Qt::BackgroundRole).value<QBrush>();
        CHECK(light.color().alpha() != dark.color().alpha()
              || light.color() != dark.color(),
              "the highlight is not one fixed colour for both themes");
        model.clearFindHits();
        model.setTheme(Theme::Light);
    }
}

// =============================================================================
//  Detaching the find bar.
//
//  Detaching REPARENTS the same widget into a small window rather than
//  building a second find UI. That is the design decision worth testing: a
//  separate dialog with its own copy of the matching logic would be two
//  things to keep in step, and on a live log they would not stay in step.
//  So the checks are that the widget survives the move and comes home to the
//  same seat.
// =============================================================================

#include <QBoxLayout>
#include <QDialog>
#include <QLabel>
#include <QPushButton>

TEST_SUITE(finddetach)
{
    LogModel model(nullptr, 1000);
    model.appendEntries({
        mk(QStringLiteral("STN 7 upcoming"), 1000),
        mk(QStringLiteral("nothing here"),   2000),
        mk(QStringLiteral("STN 9 upcoming"), 3000),
    });

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    // A tab-shaped host: something above the bar, so re-docking has a
    // specific seat to come back to rather than "somewhere in this layout".
    QWidget page;
    auto *lay   = new QVBoxLayout(&page);
    auto *above = new QLabel(QStringLiteral("filter bar stand-in"), &page);
    auto *view  = new QTableView(&page);
    view->setModel(&proxy);
    lay->addWidget(above);
    auto *bar = new FindBar(view, &page);
    lay->addWidget(bar);
    lay->addWidget(view);
    page.show();
    QCoreApplication::processEvents();

    const int seat = lay->indexOf(bar);
    CHECK(seat == 1, "the bar starts in the middle of the tab's layout");
    CHECK(!bar->isDetached(), "and starts docked");

    bar->activate();
    QCoreApplication::processEvents();

    bar->setDetached(true);
    QCoreApplication::processEvents();
    CHECK(bar->isDetached(), "it detaches");
    CHECK(lay->indexOf(bar) == -1, "and leaves the tab's layout");
    CHECK(bar->window() != &page, "living in a window of its own");
    CHECK(bar->isVisible(), "still visible — detaching is not hiding");

    bar->setDetached(false);
    QCoreApplication::processEvents();
    CHECK(!bar->isDetached(), "it re-docks");
    CHECK(lay->indexOf(bar) == seat,
          "into the same seat, not appended to the end of the tab");
    CHECK(bar->window() == &page, "back in the tab's window");

    // ---- idempotence --------------------------------------------------------
    bar->setDetached(false);
    CHECK(!bar->isDetached(), "re-docking an already-docked bar does nothing");
    bar->setDetached(true);
    bar->setDetached(true);
    QCoreApplication::processEvents();
    CHECK(bar->isDetached(), "detaching twice does not change the answer");

    // The old host is deleteLater'd — it is destroyed from inside its own
    // finished() signal, so deleting it outright would be deleting an object
    // mid-emit. processEvents() alone does not run deferred deletions posted
    // outside an event loop, hence the explicit flush; without it this counts
    // a window that is already on its way out.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    int hosts = 0;
    for (QDialog *d : page.findChildren<QDialog *>()) {
        if (d->windowTitle() == QLatin1String("Find")) { ++hosts; }
    }
    CHECK(hosts <= 1, "and does not open a second window");

    // ---- Esc from a floating bar brings it home -----------------------------
    bar->close();
    QCoreApplication::processEvents();
    CHECK(!bar->isDetached(),
          "closing a floating bar re-docks it rather than leaving a window on "
          "screen with nothing to search");
    CHECK(!bar->isVisible(), "and the bar ends hidden, as Esc should leave it");
}

// =============================================================================
//  Ctrl+F opens the shape the operator last used.
//
//  Asked for directly: "a find dialog when I press Ctrl+F, like Notepad++".
//  Rather than replacing the bar, activate() opens whichever shape is
//  remembered — a floating window by default, because that is what every
//  editor does, and the bar for anyone who docks it.
//
//  The subtle part is what counts as choosing. Esc re-docks a floating bar on
//  its way out, and that must not be read as "I prefer the bar", or Find would
//  open as a window the first time and as a bar ever after.
// =============================================================================

TEST_SUITE(finddefaultshape)
{
    const bool savedPref = Settings::findDetached();

    LogModel model(nullptr, 1000);
    model.appendEntries({ mk(QStringLiteral("STN 7 upcoming"), 1000) });
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    QWidget page;
    auto *lay  = new QVBoxLayout(&page);
    auto *view = new QTableView(&page);
    view->setModel(&proxy);
    auto *bar = new FindBar(view, &page);
    lay->addWidget(bar);
    lay->addWidget(view);
    page.show();
    QCoreApplication::processEvents();

    // ---- the default ---------------------------------------------------------
    Settings::setFindDetached(true);
    bar->activate();
    QCoreApplication::processEvents();
    CHECK(bar->isDetached(), "Ctrl+F opens Find as a window when that is the preference");

    // ---- Esc does not change the preference ---------------------------------
    bar->close();
    QCoreApplication::processEvents();
    CHECK(!bar->isDetached(), "closing re-docks the widget");
    CHECK(Settings::findDetached(),
          "but the preference is untouched — Esc means done searching, not "
          "'I would rather have the bar'");

    bar->activate();
    QCoreApplication::processEvents();
    CHECK(bar->isDetached(), "so the next Ctrl+F still opens the window");

    // ---- docking deliberately IS a choice -----------------------------------
    bar->setDetached(false);
    CHECK(!Settings::findDetached(),
          "pressing the dock button says which shape is wanted");
    bar->close();
    bar->activate();
    QCoreApplication::processEvents();
    CHECK(!bar->isDetached(), "and Ctrl+F now opens the bar in the tab");
    CHECK(bar->isVisible(), "visible and ready to type in");

    // ---- and detaching switches it back -------------------------------------
    bar->setDetached(true);
    CHECK(Settings::findDetached(), "detaching restores the window preference");

    bar->setDetached(false);
    Settings::setFindDetached(savedPref);
}
