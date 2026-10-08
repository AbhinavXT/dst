#include "testutil.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 167 — DMI window: up to four locos, each panel's decoded fields
//  under it.
//  Real frames: replay/loco_1_1_27062026_140226.cap and
//  replay/loco_2_1_27062026_140226.cap. replay/ holds @dmi from two locos
//  only, so panels C and D get real loco-1 / loco-2 frames (different
//  moments) re-tagged @dmi_3_1 / @dmi_4_1: the bytes are real, only the
//  capture key is changed.
// =============================================================================

namespace {

QString nthDmi167(const QString &file, int n)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + file);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    int seen = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@dmi_") && seen++ == n) return QString::fromLatin1(l);
    }
    return QString();
}

QString retag(QString line, const QString &key)
{
    const int sp = line.indexOf(QLatin1Char(' '));
    return QStringLiteral("@dmi_") + key + line.mid(sp);
}

QRect geomIn(const QWidget *w, const QWidget *top) { return QRect(w->mapTo(top, QPoint(0, 0)), w->size()); }

QString shotDir167() { return QString::fromLocal8Bit(qgetenv("DL_SHOTS")); }

}  // namespace

TEST_SUITE(session167)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    const QStringList keysSaved = { QStringLiteral("dmi/twoLocos"), QStringLiteral("dmi/panels"),
                                    QStringLiteral("dmi/followCursor") };
    QHash<QString, QVariant> saved;
    for (const QString &k : keysSaved) saved.insert(k, ini.value(k));
    ini.remove(QStringLiteral("dmi/panels"));
    ini.setValue(QStringLiteral("dmi/twoLocos"), false);
    ini.setValue(QStringLiteral("dmi/followCursor"), false);
    ini.sync();
    DmiTimeTravel::instance()->reset();

    const QStringList keys = { QStringLiteral("1_1"), QStringLiteral("2_1"), QStringLiteral("3_1"), QStringLiteral("4_1") };
    const QStringList lines = {
        nthDmi167(QStringLiteral("loco_1_1_27062026_140226.cap"), 300),
        nthDmi167(QStringLiteral("loco_2_1_27062026_140226.cap"), 300),
        retag(nthDmi167(QStringLiteral("loco_1_1_27062026_140226.cap"), 900), keys[2]),
        retag(nthDmi167(QStringLiteral("loco_2_1_27062026_140226.cap"), 900), keys[3]),
    };
    QVector<CaptureLine> caps;
    for (const QString &l : lines) caps << CaptureDecoder::parseLine(l);
    bool fixturesOk = true;
    for (int i = 0; i < 4; ++i) fixturesOk &= caps[i].valid && caps[i].key() == keys[i];
    CHECK(fixturesOk, "fixture: four @dmi frames, keyed 1_1, 2_1, 3_1, 4_1");
    CHECK(dmiStateFromCapture(caps[0]).time != dmiStateFromCapture(caps[2]).time,
          "fixture: C's frame is another moment than A's");

    {
        DmiWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(900, 760);
        w.show();
        QApplication::processEvents();
        auto *count = w.findChild<QComboBox *>(QStringLiteral("dmiPanels"));
        CHECK(count && count->count() == 4 && w.panelCount() == 1, "a Panels picker, 1 to 4; one panel by default");
        CHECK(!w.findChild<QCheckBox *>(QStringLiteral("dmiTwoLocos")), "it replaces the Two locos tick box");

        for (int i = 0; i < 4; ++i) w.observeLine(keys[i], lines[i]);
        w.setSelectedSource(keys[0]);
        if (count) count->setCurrentIndex(3);
        QApplication::processEvents();
        CHECK(w.panelCount() == 4, "picking 4 gives four panels");
        bool allShown = true;
        for (int i = 0; i < 4; ++i) allShown &= w.panelView(i)->isVisible();
        CHECK(allShown, "all four are on screen");
        const QRect a = geomIn(w.panelView(0), &w), b = geomIn(w.panelView(1), &w);
        const QRect c = geomIn(w.panelView(2), &w), d = geomIn(w.panelView(3), &w);
        CHECK(b.left() >= a.right() && qAbs(b.top() - a.top()) < 4 && c.top() >= a.bottom()
                  && qAbs(c.left() - a.left()) < 4 && d.left() >= c.right() && d.top() >= b.bottom(),
              "four as a 2 x 2 grid: A B over C D");
        QSet<QString> shown;
        for (int i = 0; i < 4; ++i) shown.insert(w.panelSource(i));
        CHECK(shown.size() == 4, "each panel starts on a loco no other panel shows");
        bool own = true;
        for (int i = 0; i < 4; ++i) {
            const int k = keys.indexOf(w.panelSource(i));
            own &= k >= 0 && w.panelView(i)->state().time == dmiStateFromCapture(caps[k]).time
                   && w.panelStatus(i).startsWith(keys[k]);
        }
        CHECK(own, "live: each panel draws its own loco's frame, with its own status line");

        // Fields: one table under each panel.
        auto *fieldsBtn = w.findChild<QPushButton *>(QStringLiteral("dmiFieldsButton"));
        if (fieldsBtn) fieldsBtn->setChecked(true);
        QApplication::processEvents();
        bool under = true, filled = true;
        for (int i = 0; i < 4; ++i) {
            static const char *const suffix[4] = { "", "B", "C", "D" };
            auto *t = w.findChild<QTableWidget *>(QStringLiteral("dmiFieldsTable") + QLatin1String(suffix[i]));
            if (!t || !t->isVisible()) { under = false; continue; }
            const QRect tv = geomIn(t, &w), v = geomIn(w.panelView(i), &w);
            under &= tv.top() >= v.bottom() && tv.left() < v.right() && tv.right() > v.left();
            filled &= w.fieldNames(i).size() > 40;
        }
        CHECK(under, "each panel's fields table sits under that panel");
        CHECK(filled, "each table lists its frame's decoded fields");
        bool distinct = true;
        for (int i = 0; i < 4; ++i) {
            const int k = keys.indexOf(w.panelSource(i));
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(caps[k], nullptr, 0, &raw);
            const QString val = w.fieldValue(QStringLiteral("  train_speed"), i);
            distinct &= !val.isEmpty() && val.startsWith(QString::number(raw.value(QStringLiteral("train_speed"))));
        }
        CHECK(distinct, "each table shows its own panel's frame (train_speed bit for bit)");
        auto *notes = w.findChild<QLabel *>(QStringLiteral("dmiFieldNotes"));
        CHECK(notes && notes->isVisible() && notes->text().contains(QStringLiteral("Where each region comes from")),
              "the region notes appear once, under the panels");
        if (!shotDir167().isEmpty()) w.grab().save(QDir(shotDir167()).filePath(QStringLiteral("dmi_four_fields.png")));
        auto *host = w.findChild<QWidget *>(QStringLiteral("dmiPanelsHost"));
        auto *scroll = w.findChild<QScrollArea *>(QStringLiteral("dmiPanelsScroll"));
        const int hostMin = host ? host->minimumSizeHint().width() : 99999;
        CHECK(hostMin <= 1366 - 40, QByteArray("four panels fit a 1366 px screen side by side (grid minimum ")
                                        + QByteArray::number(hostMin) + ")");
        CHECK(scroll && (scroll->viewport()->width() >= hostMin || scroll->horizontalScrollBar()->isVisible()),
              "a window narrower than the grid scrolls sideways rather than clipping the right-hand panels");
        const QSize minFour = w.minimumSizeHint();
        CHECK(minFour.width() <= 1366 && minFour.height() <= 700,
              QByteArray("four panels with fields still fit a 1366 x 700 screen (minimum ")
                  + QByteArray::number(minFour.width()) + " x " + QByteArray::number(minFour.height()) + ")");

        // At one moment: every panel at the same instant.
        DmiMoment m;
        m.valid = true;
        m.origin = QStringLiteral("test");
        m.preferredKey = keys[0];
        for (int i = 0; i < 4; ++i) {
            const qint64 ms = caps[i].rtc.toMSecsSinceEpoch();
            m.atMs = qMax(m.atMs, ms);
            m.frames << DmiFrameAt{ keys[i], caps[i], ms };
        }
        w.setFollowCursor(true);
        w.setPanelSource(2, keys[0]);              // C on A's loco: the moment moves it
        w.showMoment(m);
        shown.clear();
        for (int i = 0; i < 4; ++i) shown.insert(w.panelSource(i));
        CHECK(w.panelSource(0) == keys[0] && shown.size() == 4, "at a moment: A on the loco pointed at, the others on the rest");
        bool atMoment = true;
        for (int i = 0; i < 4; ++i) atMoment &= w.panelStatus(i).contains(w.panelSource(i) + QStringLiteral("'s @dmi"));
        CHECK(atMoment, "each status names its panel's frame at that moment");

        QTemporaryDir temp;
        const QString png = temp.filePath(QStringLiteral("four.png"));
        CHECK(w.saveImage(png), "Save image");
        const QImage img(png);
        const qreal dpr = w.panelView(0)->grab().devicePixelRatio();
        CHECK(img.width() >= 2 * w.panelView(0)->width() * dpr - 4 && img.height() >= 2 * w.panelView(0)->height() * dpr - 4,
              "the image holds the 2 x 2 grid");

        if (count) count->setCurrentIndex(2);
        QApplication::processEvents();
        const QRect a3 = geomIn(w.panelView(0), &w), c3 = geomIn(w.panelView(2), &w);
        CHECK(w.panelCount() == 3 && !w.panelView(3)->isVisible() && qAbs(c3.top() - a3.top()) < 4 && c3.left() > a3.right(),
              "three: in a row, D hidden");
        if (!shotDir167().isEmpty()) w.grab().save(QDir(shotDir167()).filePath(QStringLiteral("dmi_three_fields.png")));
        if (fieldsBtn) fieldsBtn->setChecked(false);
        CHECK(!w.findChild<QTableWidget *>(QStringLiteral("dmiFieldsTable"))->isVisible(), "Fields off: the tables go");

        w.setPanelCount(4);
        w.close();
    }
    {
        DmiWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(again.panelCount() == 4, "the count is remembered for the next window");
        again.setPanelCount(1);
        CHECK(!again.twoLocos() && !again.viewB()->isVisible(), "back to one");
        again.setFollowCursor(false);
    }
    {
        // A dlconsole.ini from before 167: only dmi/twoLocos.
        ini.remove(QStringLiteral("dmi/panels"));
        ini.setValue(QStringLiteral("dmi/twoLocos"), true);
        ini.sync();
        DmiWindow older(nullptr);
        older.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(older.panelCount() == 2, "an older settings file's Two locos opens two panels");
        older.setPanelCount(1);
    }

    for (const QString &k : keysSaved) {
        if (saved.value(k).isValid()) ini.setValue(k, saved.value(k));
        else ini.remove(k);
    }
    DmiTimeTravel::instance()->reset();
}
