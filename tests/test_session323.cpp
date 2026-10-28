#include "testutil.h"
#include "flasherfakeboard.h"

#include "blockmapwidget.h"
#include "flashercore.h"
#include "flasherflashingpage.h"
#include "flasherqueuepage.h"
#include "flasherwindow.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

// =============================================================================
//  Session 323 — two field reports from Abhinav.
//
//  1. Loco Configuration: a value could not be typed while @linfo arrived
//     over Ethernet. Each @linfo told the table that every cell had changed,
//     Value included, so the open editor was reloaded with the old value.
//  2. Firmware Flasher: with some of the listed VCCs not answering, the ones
//     that did were verified but their block map stayed part-empty. COMPLETE
//     carries no bitmap; the map kept the last checkpoint's.
//
//  Real @linfo: replay/loco_81_1_14102026_110915.cap (loco 1, 2026-10-14).
// =============================================================================

using namespace FlasherTest;

namespace {

QString firstLinfo()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_81_1_14102026_110915.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@linfo_")) return QString::fromLatin1(l);
    }
    return QString();
}

}  // namespace

TEST_SUITE(session323)
{
    // ---- Loco Configuration: typing survives an incoming @linfo ----------------------
    {
        const QString line = firstLinfo();
        CHECK(!line.isEmpty(), "fixture: a real @linfo line");
        QTemporaryDir temp;
        LocoConfigWindow window(nullptr, temp.path());
        QVector<LocoInfo::SendTarget> targets(1);
        targets[0].ip = QStringLiteral("192.168.25.16");
        window.setTargets(targets);
        window.show();
        QTableView *table = window.findChild<QTableView *>();
        LocoFieldModel *model = window.fieldModel();
        QPointer<QLineEdit> editor;
        for (int row = 0; table && row < table->model()->rowCount() && !editor; ++row) {
            const QModelIndex index = table->model()->index(row, LocoFieldModel::ColumnValue);
            if (!(index.flags() & Qt::ItemIsEditable)) continue;
            table->setCurrentIndex(index);
            table->edit(index);
            editor = table->viewport()->findChild<QLineEdit *>();
            if (!editor) table->closePersistentEditor(index);
        }
        CHECK(editor != nullptr, "a Value cell opens a line editor");
        if (editor && !line.isEmpty()) {
            editor->setText(QStringLiteral("12345"));
            window.observeCaptureLine(line, QStringLiteral("192.168.25.16"));
            QCoreApplication::processEvents();
            CHECK(model->index(0, LocoFieldModel::ColumnCount).data().toString() != QStringLiteral("—"),
                  "the @linfo reached the VCC column");
            CHECK(editor && editor->text() == QStringLiteral("12345"),
                  QByteArray("what the operator typed is still there: ")
                      + (editor ? editor->text().toUtf8() : QByteArray("(editor closed)")));
        }
        window.close();   // before it is destroyed: closing commits the open editor
    }

    // ---- Flasher: one VCC answers, one does not; the verified map is full -------------
    {
        QTemporaryDir temp;
        FakeBoard present, absent;   // `absent` is never powered on
        const QStringList targets = { QStringLiteral("127.0.0.1:%1").arg(present.port()),
                                      QStringLiteral("127.0.0.1:%1").arg(absent.port()) };
        const QString vccPath = writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                          patternBytes(300 * 1450 + 17, 323));
        {
            Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
            Flasher::FlashProfile profile;
            profile.name = QStringLiteral("Two locos");
            profile.vccIp = targets.join(QStringLiteral(", "));
            profile.port = 9;
            profile.defaultImages.insert(Flasher::CardVcc, vccPath);
            profile.tuning.poll_give_up_ms = 1500;
            profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
            store.upsert(profile, store.names().first());
            store.setActiveName(profile.name);
            store.save();
        }
        FlasherWindow window(nullptr, temp.path());
        window.queuePage()->selectCard(Flasher::CardVcc);

        QEventLoop loop;
        bool finished = false;
        QObject::connect(&window, &FlasherWindow::batchFinished, &loop, [&]() {
            finished = true;
            loop.quit();
        });
        QTimer::singleShot(40000, &loop, &QEventLoop::quit);
        CHECK(window.startBatch(false), "the batch starts");
        QTimer::singleShot(400, &loop, [&]() { present.powerOn(); });
        if (!finished) loop.exec();
        CHECK(finished, "the two-VCC run finishes");

        const Flasher::BatchPlan &plan = window.plan();
        CHECK(plan.size() == 2 && plan.entries().at(0).outcome == Flasher::CardOutcome::Accepted
                  && plan.entries().at(1).outcome == Flasher::CardOutcome::Failed,
              "the VCC that answered is verified, the other failed");
        CHECK(window.flashingPage()->blockMap()->repairedCount() == 0,
              "the verified VCC's map is all blue while the other is still failing");
        window.flashingPage()->showEntry(0);
        const int held = window.flashingPage()->blockMap()->heldCount();
        CHECK(held == 300 + 1, QByteArray("the verified VCC's map is full: ") + QByteArray::number(held) + " of 301");
    }
}
