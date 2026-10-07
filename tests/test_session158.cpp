#include "testutil.h"

#include "lococonfigcore.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <memory>

// =============================================================================
//  Session 158
//    - Icons drawn for one theme are drawn again on a theme change
//      (UiIcons::bind): the rail kept its dark-theme icons after a switch
//      back to a light theme.
//    - Loco Configuration: vcc_crc in the send confirmation's question,
//      every time; and fields locked per configuration.
//  (Show on DMI is checked in the menu audit, on the real MainWindow.)
// =============================================================================

namespace {

// The colour the icon is drawn in: its most opaque pixel.
QColor inkOf(const QIcon &icon, int px)
{
    const QImage img = icon.pixmap(px, px).toImage().convertToFormat(QImage::Format_ARGB32);
    QColor best;
    int bestAlpha = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() > bestAlpha) { bestAlpha = c.alpha(); best = c; }
        }
    return best;
}

bool near(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) <= 8 && qAbs(a.green() - b.green()) <= 8 && qAbs(a.blue() - b.blue()) <= 8;
}

// The next modal message box: what it said, then `which` pressed.
struct BoxSeen {
    bool    seen = false;
    QString text;
    QString informative;
    Qt::TextFormat format = Qt::AutoText;
};

void answerNextBox(BoxSeen *out, QMessageBox::StandardButton which)
{
    auto *poll = new QTimer;
    poll->setInterval(10);
    auto tries = std::make_shared<int>(0);
    QObject::connect(poll, &QTimer::timeout, [poll, out, which, tries]() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) {
            if (++*tries > 500) { poll->stop(); poll->deleteLater(); }   // 5 s: give up
            return;
        }
        poll->stop();
        poll->deleteLater();
        out->seen = true;
        out->text = box->text();
        out->informative = box->informativeText();
        out->format = box->textFormat();
        if (QAbstractButton *b = box->button(which)) b->click();
        else box->reject();
    });
    poll->start();
}

QString statusOf(const LocoConfigWindow &w)
{
    const StatusLine *s = w.findChild<StatusLine *>();
    return s ? s->text() : QString();
}

}  // namespace

// =============================================================================
TEST_SUITE(session158icons)
{
    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();

    QToolButton button;
    UiIcons::bind(&button, QStringLiteral("dmi"));
    QLabel label;
    UiIcons::bind(&label, QStringLiteral("search"), 16, []() { return UiColor::muted(); });

    const QColor darkText = qApp->palette().color(QPalette::WindowText);
    CHECK(near(inkOf(button.icon(), 18), darkText), "bound in the dark theme: drawn in its (light) text colour");

    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QColor lightText = qApp->palette().color(QPalette::WindowText);
    CHECK(!near(darkText, lightText), "fixture: the two themes' text colours differ");
    CHECK(near(inkOf(button.icon(), 18), lightText),
          "back to the light theme: the icon is drawn again in the light theme's text colour (the report)");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap pm = label.pixmap(Qt::ReturnByValue);
#else
    const QPixmap pm = label.pixmap() ? *label.pixmap() : QPixmap();
#endif
    CHECK(near(inkOf(QIcon(pm), 16), UiColor::muted()), "a bound label follows too, in its own colour (muted)");

    // A third theme, to be sure it is not a light/dark toggle only.
    ThemeUtil::apply(Theme::Nord);
    CHECK(near(inkOf(button.icon(), 18), qApp->palette().color(QPalette::WindowText)), "and any other theme");

    CHECK(UiIcons::names().contains(QStringLiteral("lock")), "a lock icon exists (locked fields)");
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
}

// =============================================================================
TEST_SUITE(session158lockcore)
{
    LocoInfo::Layout layout;
    QString error;
    CHECK(layout.load(QStringLiteral(":/schema/kavach.xml"), &error), "the LOCO_INFO layout loads");
    LocoInfo::Values defaults;
    CHECK(LocoInfo::loadDefaults(QStringLiteral(":/lococonfig/loco_defaults.json"), layout, &defaults, &error),
          "the defaults load");
    LocoInfo::completeValues(layout, LocoInfo::Values(), &defaults);

    // keepLocked: locked fields keep the current value; `kept` names the
    // ones the lock actually held back.
    LocoInfo::Values current = defaults;
    current.insert(QStringLiteral("vcc_crc"), 0x12345678LL);
    current.insert(QStringLiteral("loco_unit_id"), 30421);
    QStringList kept;
    const LocoInfo::Values out = LocoInfo::keepLocked(
        layout, defaults, current, { QStringLiteral("vcc_crc"), QStringLiteral("loco_wheel_dia1") }, &kept);
    CHECK(out.value(QStringLiteral("vcc_crc")).toLongLong() == 0x12345678LL, "a locked field keeps its value");
    CHECK(out.value(QStringLiteral("loco_unit_id")) == defaults.value(QStringLiteral("loco_unit_id")),
          "an unlocked one takes the incoming value");
    CHECK(kept == QStringList{ QStringLiteral("vcc_crc") },
          "kept names only the locked field whose incoming value differed");

    // Saved with the configuration, read back; a lock on a field the schema
    // does not have, or on the CRC, is dropped.
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("loco_configs.json"));
    {
        LocoInfo::ConfigStore store(path, &layout, defaults);
        store.load();
        LocoInfo::LocoConfig c = store.config(store.activeName());
        c.locked = QStringList{ QStringLiteral("loco_unit_id"), QStringLiteral("vcc_crc") };
        store.upsert(c);
        CHECK(store.save(), "saved");
    }
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonArray configs = root.value(QStringLiteral("configs")).toArray();
        QJsonObject first = configs.at(0).toObject();
        CHECK(first.value(QStringLiteral("locked_fields")).toArray().size() == 2, "written as locked_fields");
        QJsonArray locks = first.value(QStringLiteral("locked_fields")).toArray();
        locks.append(QStringLiteral("no_such_field"));
        locks.append(QStringLiteral("loco_info_crc"));
        first.insert(QStringLiteral("locked_fields"), locks);
        configs.replace(0, first);
        root.insert(QStringLiteral("configs"), configs);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(QJsonDocument(root).toJson());
    }
    LocoInfo::ConfigStore again(path, &layout, defaults);
    CHECK(again.load(), "loaded again");
    const LocoInfo::LocoConfig back = again.config(again.activeName());
    CHECK((back.locked == QStringList{ QStringLiteral("loco_unit_id"), QStringLiteral("vcc_crc") }),
          "the locks come back; unknown fields and the CRC are not locks");

    // Export / import to another PC carries them.
    const LocoInfo::ImportedConfigs imported =
        LocoInfo::importConfigs(layout, defaults, LocoInfo::exportConfigs(layout, again.all()));
    CHECK(imported.ok && imported.configs.size() == 1 && imported.configs.first().locked == back.locked,
          "export / import carries the locks");
}

// =============================================================================
TEST_SUITE(session158lockwindow)
{
    QTemporaryDir temp;
    LocoConfigWindow window(nullptr, temp.path());
    CHECK(window.isUsable(), "the window loads");
    LocoFieldModel *model = window.fieldModel();
    const QString vcc = QStringLiteral("vcc_crc");
    const int row = model->rowForKey(vcc);
    const QModelIndex value = model->index(row, LocoFieldModel::ColumnValue);

    CHECK(model->setData(value, QStringLiteral("0x0BADCAFE"), Qt::EditRole), "vcc_crc is editable before the lock");
    CHECK(window.setFieldLocked(vcc, true), "locked");
    CHECK(window.lockedFields() == QStringList{ vcc }, "the configuration holds the lock");
    CHECK(!(model->flags(value) & Qt::ItemIsEditable), "a locked value is not editable in the table");
    QString rejected;
    QObject::connect(model, &LocoFieldModel::editRejected, [&rejected](const QString &why) { rejected = why; });
    CHECK(!model->setData(value, QStringLiteral("0x11111111"), Qt::EditRole), "and setData refuses it");
    CHECK(rejected.contains(QStringLiteral("locked")), "saying it is locked");
    CHECK(model->values().value(vcc).toLongLong() == 0x0BADCAFELL, "the value is unchanged");
    CHECK(model->index(row, LocoFieldModel::ColumnField).data(Qt::DecorationRole).canConvert<QIcon>(),
          "a lock is drawn beside the name");
    CHECK(model->index(row, 0).data(Qt::ToolTipRole).toString().contains(QStringLiteral("Unlock")),
          "and the tooltip says how to unlock");

    // The "Locked fields" group lists exactly the locked ones.
    LocoFieldFilter filter;
    filter.setSourceModel(model);
    filter.setGroup(LocoFieldFilter::lockedFields());
    CHECK(filter.rowCount() == 1 && filter.index(0, 0).data(LocoFieldModel::KeyRole).toString() == vcc,
          "the Locked fields group shows the locked field only");

    // Reset to defaults: everything else goes back, the locked field stays.
    const int unitRow = model->rowForKey(QStringLiteral("loco_unit_id"));
    CHECK(model->setData(model->index(unitRow, LocoFieldModel::ColumnValue), QStringLiteral("30421"), Qt::EditRole),
          "loco_unit_id edited");
    BoxSeen reset;
    answerNextBox(&reset, QMessageBox::Yes);
    QMetaObject::invokeMethod(&window, "resetToDefaults");
    CHECK(reset.seen && reset.text.contains(QStringLiteral("Locked fields keep their values: vcc_crc")),
          "the reset question says the locked field keeps its value");
    CHECK(model->values().value(QStringLiteral("loco_unit_id")).toLongLong() != 30421, "the unlocked field was reset");
    CHECK(model->values().value(vcc).toLongLong() == 0x0BADCAFELL, "the locked field kept its value");
    CHECK(statusOf(window).contains(QStringLiteral("locked field(s) kept: vcc_crc")), "and the status line says so");

    // Unlocking asks first; No leaves it locked.
    BoxSeen no;
    answerNextBox(&no, QMessageBox::No);
    CHECK(!window.setFieldLocked(vcc, false, true) && no.seen && window.lockedFields().contains(vcc),
          "unlocking asks, and No keeps the lock");
    BoxSeen yes;
    answerNextBox(&yes, QMessageBox::Yes);
    CHECK(window.setFieldLocked(vcc, false, true) && window.lockedFields().isEmpty(), "Yes unlocks it");
    CHECK(model->flags(value) & Qt::ItemIsEditable, "editable again");
    CHECK(!window.setFieldLocked(QStringLiteral("loco_info_crc"), true), "the computed CRC cannot be locked");

    // The lock is saved with the configuration (the save timer is flushed on close).
    CHECK(window.setFieldLocked(QStringLiteral("loco_unit_id"), true), "loco_unit_id locked");
    window.close();
    LocoConfigWindow reopened(nullptr, temp.path());
    CHECK(reopened.lockedFields() == QStringList{ QStringLiteral("loco_unit_id") }, "and still locked after reopening");
    CHECK(reopened.configNamed(reopened.configNames().first()).locked == QStringList{ QStringLiteral("loco_unit_id") },
          "in the stored configuration");
}

// =============================================================================
TEST_SUITE(session158vcccrc)
{
    QTemporaryDir temp;
    LocoConfigWindow window(nullptr, temp.path());
    CHECK(window.isUsable(), "the window loads");
    window.setTarget(QStringLiteral("127.0.0.1"), 9);
    LocoFieldModel *model = window.fieldModel();
    const int row = model->rowForKey(QStringLiteral("vcc_crc"));

    // Never sent, at the default value.
    BoxSeen first;
    answerNextBox(&first, QMessageBox::No);
    CHECK(!window.sendNow(true), "No: nothing sent");
    CHECK(first.seen, "the confirmation box appeared");
    CHECK(first.format == Qt::RichText && first.text.contains(QStringLiteral("<b>vcc_crc 0xF6134AC1</b>")),
          "vcc_crc and its value are in the question itself, in bold");
    CHECK(first.text.contains(QStringLiteral("x-large")), "set large");
    CHECK(first.text.contains(QStringLiteral("must match this loco's VCC build")), "with why it matters");
    CHECK(first.text.contains(QStringLiteral("default value")), "and that it is still the default");
    CHECK(first.text.contains(QStringLiteral("Not sent from this configuration before")), "first send");
    CHECK(first.informative.contains(QStringLiteral("loco_info_crc")), "loco_info_crc stays with the details");

    // Sent once (to the discard port), then changed: the box says so.
    CHECK(window.sendNow(false), "sent once");
    CHECK(model->setData(model->index(row, LocoFieldModel::ColumnValue), QStringLiteral("0x0BADCAFE"), Qt::EditRole),
          "vcc_crc changed by hand");
    BoxSeen changed;
    answerNextBox(&changed, QMessageBox::No);
    window.sendNow(true);
    CHECK(changed.text.contains(QStringLiteral("<b>vcc_crc 0x0BADCAFE</b>")), "the new value, in the question");
    CHECK(changed.text.contains(QStringLiteral("<b>Changed</b> since the last send")) &&
              changed.text.contains(QStringLiteral("0xF6134AC1")),
          "marked as changed, with the value it was");
    CHECK(!changed.text.contains(QStringLiteral("default value")), "no longer the default");

    // Many other fields changed: vcc_crc is still shown, not lost in "and N more".
    for (const QString &k : { QStringLiteral("loco_unit_id"), QStringLiteral("loco_wheel_dia1"),
                              QStringLiteral("loco_wheel_dia2") }) {
        const int r = model->rowForKey(k);
        if (r >= 0) model->setData(model->index(r, LocoFieldModel::ColumnValue), QStringLiteral("901"), Qt::EditRole);
    }
    BoxSeen many;
    answerNextBox(&many, QMessageBox::No);
    window.sendNow(true);
    CHECK(many.text.contains(QStringLiteral("<b>vcc_crc 0x0BADCAFE</b>")), "shown with many other changes too");

    // Bulk send: the same paragraph, under the list of targets.
    window.setTargets({ LocoInfo::SendTarget{ QStringLiteral("127.0.0.1"), 9, true },
                        LocoInfo::SendTarget{ QStringLiteral("127.0.0.2"), 9, true },
                        LocoInfo::SendTarget(), LocoInfo::SendTarget() });
    BoxSeen bulk;
    answerNextBox(&bulk, QMessageBox::No);
    window.sendNow(true);
    CHECK(bulk.text.contains(QStringLiteral("2 VCCs")) && bulk.text.contains(QStringLiteral("<b>vcc_crc 0x0BADCAFE</b>")),
          "a bulk send shows it too");

    // Same as the last send.
    window.setTarget(QStringLiteral("127.0.0.1"), 9);
    CHECK(window.sendNow(false), "sent again");
    BoxSeen same;
    answerNextBox(&same, QMessageBox::No);
    window.sendNow(true);
    CHECK(same.text.contains(QStringLiteral("The same as the last send")), "unchanged since the last send");
}
