#include "testutil.h"

#include "flashercore.h"
#include "flasherflashingpage.h"
#include "flasherqueuepage.h"
#include "flasherwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolButton>
#include <QRegularExpression>

// =============================================================================
//  Session 146 — the Linux CI gate, red since patch 134.
//
//  With Linux's fonts the Flasher's minimum was 1144 x 709: over the 1100 x
//  700 that session 134 pins. Its width came from the Flashing page (the
//  legend and the block-map caption on one unwrappable line) and its app
//  bar (the caution beside the target), not from the queue; its height from
//  spacing. And the recorded-session window's minimum grew on the first row
//  clicked, because the raw panel's header label did. Checked here by what
//  each fix does, since the fonts that failed are not on this machine.
// =============================================================================

namespace {

QString writeImage(const QString &dir, const QString &name, int size)
{
    QFile f(QDir(dir).filePath(name));
    if (f.open(QIODevice::WriteOnly)) {
        QByteArray b(size, '\0');
        for (int i = 0; i < size; ++i) b[i] = char((i * 31 + 7) & 0xFF);
        f.write(b);
    }
    return f.fileName();
}

void settle(int ms = 50)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QToolButton *buttonNamed(QWidget *root, const QString &text)
{
    for (QToolButton *b : root->findChildren<QToolButton *>())
        if (b->text() == text) return b;
    return nullptr;
}

}  // namespace

TEST_SUITE(session146)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QTemporaryDir temp;
    CHECK(temp.isValid(), "fixture: a temporary data directory");
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("Bench 2");
        profile.vccIp = QStringLiteral("127.0.0.1");
        profile.port = 9;   // discard: nothing answers
        profile.defaultImages.insert(Flasher::CardInput, writeImage(temp.path(), QStringLiteral("KAVACH_Input_Card_v5.appimage"), 40 * 1450));
        profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
        store.upsert(profile, store.names().first());
        store.setActiveName(profile.name);
        store.save();
    }
    FlasherWindow w(nullptr, temp.path());
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.queuePage()->selectCard(Flasher::CardInput);
    w.resize(1100, 720);
    w.show();
    settle(200);

    // ---- the queue: the hash a size smaller ------------------------------------------------
    {
        QLabel *sha = nullptr;
        for (QLabel *l : w.findChildren<QLabel *>())
            if (l->text().size() == 64 && l->text().contains(QRegularExpression(QStringLiteral("^[0-9a-f]+$")))) sha = l;
        CHECK(sha != nullptr, "fixture: the SHA-256 line");
        if (sha)
            CHECK(sha->font().pointSizeF() < UiStyle::monoFont().pointSizeF(),
                  "the 64 hex digits are a size smaller than the other mono values (they set the queue's width)");
    }

    // ---- flashing: the legend, the caption and the caution are not the floor ----------------
    CHECK(w.startBatch(false), "a run starts");
    settle(800);
    CHECK(w.isFlashing(), "on the flashing page");
    {
        int legend = 0;
        for (QLabel *l : w.flashingPage()->findChildren<QLabel *>()) {
            const QString t = l->text();
            if (t.contains(QLatin1String("Acknowledged")) || t.contains(QLatin1String("Resent this round"))
                || t.contains(QLatin1String("Still missing")) || t.contains(QLatin1String("per block"))) {
                ++legend;
                CHECK(l->minimumSizeHint().width() <= 40 || l->minimumWidth() == 40,
                      QByteArray("not the card's floor: ") + t.left(40).toUtf8());
            }
        }
        CHECK(legend == 4, "fixture: three legend entries and the caption");
    }
    {
        auto *bar = w.findChild<QFrame *>(QStringLiteral("flasherAppBar"));
        QLabel *badge = nullptr, *target = nullptr;
        for (QLabel *l : bar ? bar->findChildren<QLabel *>() : QList<QLabel *>()) {
            if (l->text().startsWith(QLatin1String("Flashing"))) badge = l;
            if (l->text() == QLatin1String("127.0.0.1:9")) target = l;
        }
        CHECK(badge && target, "fixture: the caution and the target in the bar");
        if (badge && target) {
            CHECK(badge->heightForWidth(badge->width()) <= bar->height(),
                  "at 1100 px the caution fits its bar: one line, or two where the fonts are wider");
            // A real target, not the discard port: 21 characters of mono.
            target->setText(QStringLiteral("192.168.100.200:50000"));
            settle(50);
            CHECK(bar->minimumSizeHint().width() <= 1000,
                  QByteArray("with a real target the bar still leaves room (minimum ")
                      + QByteArray::number(bar->minimumSizeHint().width()) + ")");
            CHECK(w.minimumSizeHint().width() <= 1100, "and the window still fits 1100 px");
        }
    }
    w.abortCurrent();
    settle(200);
}
