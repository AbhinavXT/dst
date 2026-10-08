#include "testutil.h"

#include "bignumberpanel.h"
#include "capturedecoder.h"
#include "dmitimetravel.h"
#include "lococonsolewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "replaywindow.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLayout>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 169 — the Live Loco Console follows the cursor, and saves a
//  snapshot. Real frames: replay/loco_1_1_27062026_140226.cap, as a tab
//  (through the dispatcher) and as a replay.
// =============================================================================

namespace {

QString tableRow(QTableWidget *t, int r, int c)
{
    return t && r < t->rowCount() && t->item(r, c) ? t->item(r, c)->text() : QString();
}

}  // namespace

TEST_SUITE(session169)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    const QVariant savedFollow = ini.value(QStringLiteral("lococonsole/followCursor"));
    ini.remove(QStringLiteral("lococonsole/followCursor"));
    ini.sync();
    auto *tt = DmiTimeTravel::instance();
    tt->reset();

    const QString file = QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
    MessageDispatcher disp;
    {
        QFile run(file);
        CHECK(run.open(QIODevice::ReadOnly), "fixture: the run");
        while (!run.atEnd()) {
            const QByteArray l = run.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3) continue;
            disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        }
        disp.drainNow();
    }
    LogModel *model = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(model && model->count() > 1000, "fixture: the tab's rows");
    if (!model) return;

    // ---- the moment carries every type ------------------------------------------------------
    const int rowA = model->count() / 3, rowB = model->count() / 2;
    const DmiMoment mA = dmiMomentFromModels({ model }, model, rowA, QStringLiteral("tab 21_1"));
    {
        QSet<QString> types;
        bool atOrBefore = true;
        for (const DmiFrameAt &f : mA.latest) {
            types.insert(QString::fromLatin1(CaptureDecoder::typeLabel(f.cap.type)));
            atOrBefore &= f.frameMs <= mA.atMs && f.key == QStringLiteral("1_1");
        }
        CHECK(types.size() >= 5, QByteArray("the moment holds the latest of each type (") + QByteArray::number(types.size()) + " types)");
        CHECK(atOrBefore, "each at or before the moment, from loco 1_1");
        // Brute force: for each type, the last row of that type at or before the clicked row.
        bool latest = true;
        for (const DmiFrameAt &f : mA.latest) {
            for (int r = rowA; r >= 0; --r) {
                const CaptureLine c = CaptureDecoder::parseLine(model->entryAt(r)->text);
                if (!c.valid || c.type != f.cap.type) continue;
                latest &= c.bytes == f.cap.bytes && c.seq == f.cap.seq;
                break;
            }
        }
        CHECK(latest, "each is that type's last row at or before the clicked row");
        CHECK(mA.frames.size() == 1 && mA.frameFor(QStringLiteral("1_1")), "the DMI's frames are as before");
    }

    // ---- the console follows ------------------------------------------------------------------
    {
        LocoConsoleWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 700);
        w.show();
        auto *follow = w.findChild<QCheckBox *>(QStringLiteral("locoFollowCursor"));
        CHECK(follow && !follow->isChecked() && !w.followCursor(), "a Follow cursor box, off by default");
        CHECK(w.minimumSizeHint().width() <= 1100, QByteArray("still fits a laptop (minimum width ")
                                                        + QByteArray::number(w.minimumSizeHint().width()) + ")");
        QObject tab;
        tt->offer(&tab, dmiTabResolver({ model }, model, model->entryAt(rowA), rowA, QStringLiteral("tab 21_1")));
        if (follow) follow->setChecked(true);
        QCoreApplication::processEvents();
        CHECK(w.followCursor() && tt->hasFollowers(), "ticking it follows");

        QTableWidget *slrp = w.tableForLabel(QStringLiteral("slrp"));
        const DmiFrameAt *slrpAt = nullptr;
        for (const DmiFrameAt &f : mA.latest) if (f.cap.type == CapType::SLRP) slrpAt = &f;
        CHECK(slrpAt != nullptr, "fixture: an SLRP before the moment");
        if (slrpAt && slrp) {
            const QVector<FieldRow> rows = CaptureDecoder::describe(slrpAt->cap, nullptr, 0, nullptr);
            CHECK(tableRow(slrp, 0, 0).contains(QStringLiteral("frame")) && tableRow(slrp, 0, 1).contains(QStringLiteral("before the moment")),
                  QByteArray("the first row says how old the frame was then: ") + tableRow(slrp, 0, 1).toUtf8());
            bool same = slrp->rowCount() == rows.size() + 1;
            for (int i = 0; same && i < rows.size(); ++i) same = tableRow(slrp, i + 1, 1) == rows.at(i).value;
            CHECK(same, "the SLRP tab is that frame, field for field");
        }

        // Another row: the tables move with it.
        const DmiMoment mB = dmiMomentFromModels({ model }, model, rowB, QStringLiteral("tab 21_1"));
        tt->offer(&tab, dmiTabResolver({ model }, model, model->entryAt(rowB), rowB, QStringLiteral("tab 21_1")));
        QCoreApplication::processEvents();
        QTableWidget *rfid = w.tableForLabel(QStringLiteral("rfid"));
        const DmiFrameAt *rfidB = nullptr;
        for (const DmiFrameAt &f : mB.latest) if (f.cap.type == CapType::Rfid) rfidB = &f;
        if (rfidB && rfid) {
            const QVector<FieldRow> rows = CaptureDecoder::describe(rfidB->cap, nullptr, 0, nullptr);
            CHECK(rows.size() > 3 && tableRow(rfid, 4, 1) == rows.at(3).value && rfid->rowCount() == rows.size() + 1,
                  "a later row: the RFID tab is that moment's tag frame");
        } else {
            CHECK(false, "fixture: an RFID frame before the second moment");
        }

        CHECK(w.minimumSizeHint().width() <= 1100, QByteArray("following, the long clock text does not widen it (minimum ")
                                                        + QByteArray::number(w.minimumSizeHint().width()) + ")");
        if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/console_follow.png"));

        QPushButton *big = nullptr;
        for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == QStringLiteral("Big numbers")) big = b;
        const bool bigWas = big && big->isChecked();
        if (big) big->setChecked(true);
        QMetaObject::invokeMethod(&w, "onRefreshTick");
        bool agesFromMoment = big && w.bigNumbers()->tiles().size() > 0;
        for (int i = 0; agesFromMoment && i < w.bigNumbers()->tiles().size(); ++i)
            agesFromMoment = !w.bigNumbers()->detailTextAt(i).contains(QStringLiteral("d "));
        CHECK(agesFromMoment, QByteArray("the tiles' ages count from the moment, not from today (")
                                  + (big ? w.bigNumbers()->detailTextAt(0).toUtf8() : QByteArray("no button")) + ")");
        if (big) big->setChecked(bigWas);

        // Snapshot: text, CSV, image.
        QTemporaryDir temp;
        const QString txt = temp.filePath(QStringLiteral("snap.txt"));
        const QString csv = temp.filePath(QStringLiteral("snap.csv"));
        const QString png = temp.filePath(QStringLiteral("snap.png"));
        CHECK(w.saveSnapshot(txt) && w.saveSnapshot(csv) && w.saveSnapshot(png), "Save snapshot writes .txt, .csv and .png");
        QFile ft(txt), fc(csv);
        ft.open(QIODevice::ReadOnly);
        fc.open(QIODevice::ReadOnly);
        const QString t = QString::fromUtf8(ft.readAll()), c = QString::fromUtf8(fc.readAll());
        const QString atText = QDateTime::fromMSecsSinceEpoch(mB.atMs).toString(QStringLiteral("HH:mm:ss"));
        CHECK(t.contains(QStringLiteral("loco/ctrl 1_1")) && t.contains(atText) && t.contains(QStringLiteral("== @rfid ==")) && t.contains(QStringLiteral("== @slrp ==")),
              "the text names the loco, the moment, and has a section per packet (RFID, SLRP, ...)");
        CHECK(c.startsWith(QStringLiteral("packet,field,value\n")) && c.contains(QStringLiteral("\"rfid\",")) && c.contains(QStringLiteral("\"frame time\"")),
              "the CSV: one row per field, with each packet's frame time");
        CHECK(!QImage(png).isNull(), "the image is the window");

        // Off: live again.
        if (follow) follow->setChecked(false);
        QCoreApplication::processEvents();
        CHECK(!w.followCursor() && !tt->hasFollowers(), "unticked: live again");
        CHECK(!tableRow(w.tableForLabel(QStringLiteral("slrp")), 0, 0).contains(QStringLiteral("frame")),
              "no moment row when live");
        CHECK(QSettings(Settings::iniPath(), QSettings::IniFormat).value(QStringLiteral("lococonsole/followCursor")).toBool() == false,
              "the choice is remembered");
    }
    tt->reset();

    // ---- a replay's cursor -------------------------------------------------------------------
    {
        ReplayWindow replay(QStringList{ file });
        const int n = replay.recordCount();
        CHECK(n > 1000, "the capture loads in a replay window");
        const int k = n / 2;
        const DmiMoment m = replay.dmiMomentAt(k);
        QSet<int> types;
        bool ok = true;
        for (const DmiFrameAt &f : m.latest) {
            ok &= f.frameMs <= m.atMs && f.key == QStringLiteral("1_1") && !types.contains(int(f.cap.type));
            types.insert(int(f.cap.type));
        }
        CHECK(ok && types.size() >= 5, "the replay's moment has each type once, at or before its cursor");

        LocoConsoleWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setFollowCursor(true);
        tt->offer(&replay, [&replay, k]() { return replay.dmiMomentAt(k); });
        QCoreApplication::processEvents();
        QTableWidget *arp = w.tableForLabel(QStringLiteral("arp"));
        CHECK(arp && arp->rowCount() > 5 && tableRow(arp, 0, 1).contains(QStringLiteral("before the moment")),
              "a loco the console never heard live: picked and shown at the replay's cursor");
        w.setFollowCursor(false);
    }

    tt->reset();
    if (savedFollow.isValid()) ini.setValue(QStringLiteral("lococonsole/followCursor"), savedFollow);
    else ini.remove(QStringLiteral("lococonsole/followCursor"));
}
