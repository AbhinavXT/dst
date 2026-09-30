#include "testutil.h"

#include "emptystate.h"
#include "uicolors.h"
#include "theme.h"

#include <QApplication>
#include <QLabel>
#include <QStandardItemModel>
#include <QTableView>
#include <QListWidget>
#include <QSortFilterProxyModel>

// =============================================================================
//  Empty states.
//
//  A blank table meant three different things in this program — nothing has
//  arrived, the filter matched nothing, the search found nothing — and the
//  operator had to work out which from context. During acceptance testing
//  "no rows" versus "no traffic" is not a detail, it is the observation.
//
//  So the checks here are mostly about WHICH sentence appears. A test that
//  only asserted "something is shown" would pass while a log tab said
//  "waiting for traffic" with forty thousand rows sitting behind a filter.
// =============================================================================

TEST_SUITE(emptystate)
{
    // ---- appears and disappears with the rows ------------------------------
    {
        QStandardItemModel model;
        QTableView view;
        view.setModel(&model);
        view.resize(300, 200);
        view.show();
        QCoreApplication::processEvents();
        EmptyState::attach(&view, QStringLiteral("nothing here"));
        CHECK(EmptyState::message(&view) == QLatin1String("nothing here"),
              "the message is the one that was attached");
        CHECK(EmptyState::isShowing(&view), "and it shows while the model is empty");

        model.appendRow(new QStandardItem(QStringLiteral("a row")));
        QCoreApplication::processEvents();
        CHECK(!EmptyState::isShowing(&view), "a row arrives and it goes away");

        model.removeRow(0);
        QCoreApplication::processEvents();
        CHECK(EmptyState::isShowing(&view), "the row goes and it comes back");
    }
    // ---- the case the whole thing exists for -------------------------------
    // Source model full, proxy empty: that is a filter that matches nothing,
    // NOT an absence of traffic, and the two sentences must differ.
    {
        QStandardItemModel source;
        for (int i = 0; i < 5; ++i) {
            source.appendRow(new QStandardItem(QStringLiteral("lsrp %1").arg(i)));
        }
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);

        QTableView view;
        view.setModel(&proxy);
        view.resize(300, 200);
        view.show();

        EmptyState::attach(&view, [&source]() -> QString {
            return source.rowCount() == 0 ? QStringLiteral("waiting for traffic")
                                          : QStringLiteral("no rows match the filter");
        });
        QCoreApplication::processEvents();
        CHECK(!EmptyState::isShowing(&view), "nothing shown while rows are visible");

        proxy.setFilterFixedString(QStringLiteral("nothing matches this"));
        QCoreApplication::processEvents();
        CHECK(EmptyState::isShowing(&view), "filtering everything out shows the overlay");
        CHECK(EmptyState::message(&view) == QLatin1String("no rows match the filter"),
              "and it says the filter is hiding them, not that nothing arrived");

        proxy.setFilterFixedString(QString());
        source.clear();
        QCoreApplication::processEvents();
        CHECK(EmptyState::message(&view) == QLatin1String("waiting for traffic"),
              "with the source emptied, the sentence changes to the other one");
    }
    // ---- re-attaching replaces rather than stacks --------------------------
    {
        QStandardItemModel model;
        QTableView view;
        view.setModel(&model);
        view.show();
        EmptyState::attach(&view, QStringLiteral("first"));
        EmptyState::attach(&view, QStringLiteral("second"));
        CHECK(EmptyState::message(&view) == QLatin1String("second"),
              "a second attach replaces the message");
        CHECK(view.viewport()->findChildren<QLabel *>().size() == 1,
              "and does not leave two overlays stacked on each other");

        EmptyState::attach(&view, QString());
        CHECK(EmptyState::message(&view).isEmpty(), "an empty message removes it");
        CHECK(view.viewport()->findChildren<QLabel *>().isEmpty(),
              "taking the overlay away really deletes it");
    }
    // ---- it must not swallow clicks ----------------------------------------
    // The overlay covers the whole viewport. Without WA_TransparentForMouse-
    // Events it eats the click on the first row that arrives while it is
    // still on screen — which is exactly when someone is most likely to click.
    {
        QStandardItemModel model;
        QTableView view;
        view.setModel(&model);
        view.show();
        EmptyState::attach(&view, QStringLiteral("empty"));
        auto *label = view.viewport()->findChild<QLabel *>();
        CHECK(label != nullptr, "the overlay exists");
        CHECK(label && label->testAttribute(Qt::WA_TransparentForMouseEvents),
              "and is transparent to the mouse");
    }
    // ---- it follows the theme ----------------------------------------------
    {
        ThemeUtil::apply(Theme::Light);
        QStandardItemModel model;
        QListWidget list;
        list.show();                 // hidden widgets are not polished, and an
        QCoreApplication::processEvents();   // unpolished one gets no palette event
        EmptyState::attach(&list, QStringLiteral("nothing"));
        auto *label = list.viewport()->findChild<QLabel *>();
        CHECK(label != nullptr, "a QListWidget takes an overlay too");
        const QString lightSheet = label ? label->styleSheet() : QString();
        CHECK(lightSheet.contains(UiColor::muted().name()),
              "drawn in the muted colour, not a hardcoded grey");

        ThemeUtil::apply(Theme::Dark);
        QCoreApplication::processEvents();
        CHECK(label && label->styleSheet() != lightSheet,
              "and it recolours when the theme changes");
        ThemeUtil::apply(Theme::Light);
    }
}
