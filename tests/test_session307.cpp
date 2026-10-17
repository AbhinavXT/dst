#include "testutil.h"

#include "linfoedit.h"
#include "linfofielddialog.h"
#include "lococonfigcore.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>

// =============================================================================
//  Session 307 — Loco Configuration ▸ Manage ▸ Fields: add or remove a
//  LOCO_INFO member, written into kavach.xml.
//
//  Everything runs on the repository's real schema/kavach.xml (and its real
//  loco_defaults.json): the edit core line by line, the window against a
//  temporary copy set as the external schema, the two dialogs.
// =============================================================================

namespace {

QByteArray schema307()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

LocoInfo::Layout layout307(const QByteArray &xml)
{
    LocoInfo::Layout l;
    l.loadXml(xml, QStringLiteral("kavach.xml"), nullptr);
    return l;
}

// The lines `after` has that `before` lacks, and the reverse.
QStringList added307(const QByteArray &before, const QByteArray &after)
{
    QList<QByteArray> a = before.split('\n'), b = after.split('\n');
    int i = 0;
    while (i < a.size() && i < b.size() && a.at(i) == b.at(i)) ++i;
    int ja = a.size() - 1, jb = b.size() - 1;
    while (ja >= i && jb >= i && a.at(ja) == b.at(jb)) { --ja; --jb; }
    QStringList out;
    for (int k = i; k <= jb; ++k) out << QStringLiteral("+") + QString::fromUtf8(b.at(k));
    for (int k = i; k <= ja; ++k) out << QStringLiteral("-") + QString::fromUtf8(a.at(k));
    return out;
}

int column307(const QString &line, const QString &attr)
{
    return line.indexOf(attr + QLatin1Char('='));
}

LinfoEdit::NewField u16After(const QString &name, const QString &after)
{
    LinfoEdit::NewField f;
    f.name = name;
    f.kind = LocoInfo::Kind::U16;
    f.afterKey = after;
    return f;
}

}  // namespace

TEST_SUITE(session307)
{
    const QByteArray xml = schema307();
    const LocoInfo::Layout real = layout307(xml);
    CHECK(real.isLoaded() && real.bodySize() > 300, "the repository's LINFO loads");
    const int size0 = real.bodySize();

    // ---- add: one line, in place, aligned -------------------------------------
    {
        LinfoEdit::NewField f = u16After(QStringLiteral("max_brake_cylinder_pressure"), QStringLiteral("loco_doubtover"));
        f.group = QStringLiteral("Loco & wheels");
        f.note = QStringLiteral("kPa");
        f.defaultText = QStringLiteral("380");
        const LinfoEdit::Result r = LinfoEdit::addField(xml, real, f);
        CHECK(r.ok && r.oldSize == size0 && r.newSize == size0 + 2, "u16 after loco_doubtover: LOCO_INFO grows 2 bytes");
        const QStringList diff = added307(xml, r.text);
        CHECK(diff.size() == 1 && diff.first().startsWith(QLatin1Char('+')), "exactly one line added, nothing else changed");
        const QString line = diff.value(0).mid(1);
        const QString neighbour = QString::fromUtf8(xml.split('\n').at(r.line - 2));
        CHECK(neighbour.contains(QLatin1String("name=\"loco_doubtover\"")), "the new line follows loco_doubtover's");
        CHECK(line.left(line.indexOf(QLatin1Char('<'))) == neighbour.left(neighbour.indexOf(QLatin1Char('<')))
                  && column307(line, QStringLiteral("bits")) == column307(neighbour, QStringLiteral("bits")),
              "same indent, attributes in the neighbour's column");
        CHECK(line.contains(QLatin1String("bits=\"16\" ui-group=\"Loco &amp; wheels\" ui-note=\"kPa\" ui-default=\"380\"/>")),
              "the attributes, the group's & escaped");

        const LocoInfo::Layout after = layout307(r.text);
        const int at = after.indexOf(QStringLiteral("max_brake_cylinder_pressure"));
        CHECK(at == real.indexOf(QStringLiteral("loco_doubtover")) + 1, "read back: right after loco_doubtover");
        const LocoInfo::Field *next = after.field(QStringLiteral("loco_max_speed"));
        CHECK(after.fields().at(at + 1).offset == real.fields().at(at).offset + 2
                  && next && next->offset == real.field(QStringLiteral("loco_max_speed"))->offset,
              "members after it move 2 bytes; those before stay");
        CHECK(after.field(QStringLiteral("max_brake_cylinder_pressure"))->group == QLatin1String("Loco & wheels"),
              "its group is Loco & wheels");

        LocoInfo::Values defaults;
        QString err;
        CHECK(LocoInfo::loadDefaults(QStringLiteral(DL_SRC_DIR "/lococonfig/loco_defaults.json"), after, &defaults, &err)
                  && defaults.value(QStringLiteral("max_brake_cylinder_pressure")).toLongLong() == 380,
              "the real loco_defaults.json lacks it: its default is ui-default, 380");

        const LinfoEdit::Result back = LinfoEdit::removeField(r.text, after, QStringLiteral("max_brake_cylinder_pressure"));
        CHECK(back.ok && back.text == xml, "removing it again gives the original file byte for byte");
    }
    {
        // A section: after a national_values member, the key carries the prefix.
        QString firstNational;
        for (const LocoInfo::Field &f : real.fields())
            if (f.section == QLatin1String("national_values")) { firstNational = f.key; break; }
        const LinfoEdit::Result r = LinfoEdit::addField(xml, real, u16After(QStringLiteral("t_extra"), firstNational));
        CHECK(!firstNational.isEmpty() && r.ok && layout307(r.text).indexOf(QStringLiteral("national_values.t_extra")) >= 0,
              "after a national_values member: keyed national_values.t_extra");
        // vcc_crc is the last of LOCO_INFO's own before static_brake_params' note.
        const LinfoEdit::Result top = LinfoEdit::addField(xml, real, u16After(QStringLiteral("t_top"), QStringLiteral("vcc_crc")));
        CHECK(top.ok && layout307(top.text).indexOf(QStringLiteral("t_top")) == real.indexOf(QStringLiteral("vcc_crc")) + 1,
              "after vcc_crc: a LOCO_INFO member, before the nested structs");
        // brake_pressure_used comes after the "loco_info" note: top level too.
        const LinfoEdit::Result late = LinfoEdit::addField(xml, real, u16After(QStringLiteral("t_late"), QStringLiteral("brake_pressure_used")));
        CHECK(late.ok && layout307(late.text).indexOf(QStringLiteral("t_late")) >= 0, "after brake_pressure_used: top level");
    }
    {
        LinfoEdit::NewField c = u16After(QStringLiteral("depot_name"), QStringLiteral("loco_doubtover"));
        c.kind = LocoInfo::Kind::Char;
        c.charCount = 12;
        const LinfoEdit::Result rc = LinfoEdit::addField(xml, real, c);
        CHECK(rc.ok && rc.newSize == size0 + 12 && rc.lineText.contains(QLatin1String("type=\"char\" count=\"12\"")),
              "char[12]: 12 bytes, type=\"char\" count=\"12\"");
        LinfoEdit::NewField fl = u16After(QStringLiteral("brake_factor"), QStringLiteral("loco_doubtover"));
        fl.kind = LocoInfo::Kind::Float;
        fl.defaultText = QStringLiteral("0.75");
        const LinfoEdit::Result rf = LinfoEdit::addField(xml, real, fl);
        CHECK(rf.ok && rf.newSize == size0 + 4 && rf.lineText.contains(QLatin1String("type=\"float\" prec=\"4\"")),
              "float: 4 bytes, type=\"float\" prec=\"4\" (as the schema writes its floats)");
        LinfoEdit::NewField hx = u16After(QStringLiteral("magic"), QStringLiteral("loco_doubtover"));
        hx.kind = LocoInfo::Kind::U32;
        hx.hex = true;
        CHECK(LinfoEdit::element(hx).contains(QLatin1String("bits=\"32\" hexup=\"8\"")), "hex u32: hexup=\"8\"");
    }

    // ---- add: refused, file untouched ---------------------------------------
    {
        auto refused = [&](const LinfoEdit::NewField &f, const char *want) {
            const LinfoEdit::Result r = LinfoEdit::addField(xml, real, f);
            return !r.ok && r.text.isEmpty() && r.error.contains(QLatin1String(want));
        };
        CHECK(refused(u16After(QStringLiteral("2fast"), QStringLiteral("loco_doubtover")), "not a C member name"),
              "a name starting with a digit");
        CHECK(refused(u16After(QStringLiteral("loco_max_speed"), QStringLiteral("loco_doubtover")), "already has loco_max_speed"),
              "a name LOCO_INFO already has");
        CHECK(refused(u16After(QStringLiteral("x"), QStringLiteral("loco_info_crc")), "stays the last member"),
              "after loco_info_crc");
        CHECK(refused(u16After(QStringLiteral("x"), QStringLiteral("no_such")), "Choose the field"), "after a field that does not exist");
        LinfoEdit::NewField big = u16After(QStringLiteral("x"), QStringLiteral("loco_doubtover"));
        big.defaultText = QStringLiteral("70000");
        CHECK(refused(big, "Default"), "a u16 default of 70000");
        LinfoEdit::NewField one = u16After(QStringLiteral("x"), QStringLiteral("loco_doubtover"));
        one.kind = LocoInfo::Kind::Char;
        one.charCount = 1;
        CHECK(refused(one, "2 to 255"), "char[1]");
        // A national_values member named like a top-level one is a different key: allowed.
        QString national;
        for (const LocoInfo::Field &f : real.fields())
            if (f.section == QLatin1String("national_values")) { national = f.key; break; }
        CHECK(LinfoEdit::addField(xml, real, u16After(QStringLiteral("loco_max_speed"), national)).ok,
              "the same name inside national_values: allowed (its own key)");
    }

    // ---- remove ----------------------------------------------------------------
    {
        const LinfoEdit::Result r = LinfoEdit::removeField(xml, real, QStringLiteral("loco_doubtover"));
        const QStringList diff = added307(xml, r.text);
        CHECK(r.ok && r.newSize == size0 - 1 && diff.size() == 1 && diff.first().startsWith(QLatin1Char('-'))
                  && diff.first().contains(QLatin1String("name=\"loco_doubtover\"")),
              "remove loco_doubtover: its line alone goes, LOCO_INFO 1 byte smaller");
        CHECK(layout307(r.text).indexOf(QStringLiteral("loco_doubtover")) < 0, "read back: gone");
        CHECK(!LinfoEdit::removeField(xml, real, QStringLiteral("loco_info_crc")).ok
                  && !LinfoEdit::removeField(xml, real, QStringLiteral("vcc_crc")).ok
                  && !LinfoEdit::removeField(xml, real, QStringLiteral("no_such")).ok,
              "loco_info_crc, vcc_crc and an unknown key are refused");
    }

    // ---- the file's own form ----------------------------------------------------
    {
        QByteArray crlf = xml;
        crlf.replace("\r\n", "\n");
        crlf.replace("\n", "\r\n");
        const LinfoEdit::Result r = LinfoEdit::addField(crlf, real, u16After(QStringLiteral("x16"), QStringLiteral("loco_doubtover")));
        CHECK(r.ok && r.text.count("\r\n") == crlf.count("\r\n") + 1 && r.text.count('\n') == r.text.count("\r\n"),
              "a CRLF file stays CRLF, the new line too");

        // A LINFO field written over two lines: refused rather than cut.
        QByteArray split = xml;
        split.replace("<field name=\"loco_max_speed\"", "<field\n         name=\"loco_max_speed\"");
        const LinfoEdit::Result rs = LinfoEdit::addField(split, layout307(split), u16After(QStringLiteral("x"), QStringLiteral("loco_doubtover")));
        CHECK(!rs.ok && rs.error.contains(QLatin1String("not one complete element")), "a field over two lines: refused, by hand");
    }

    // ---- the window, on a temporary copy set as the external schema -------------
    {
        const QString oldSchema = Settings::schemaPath();
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kavach.xml"));
        { QFile f(path); f.open(QIODevice::WriteOnly); f.write(xml); }

        Settings::setSchemaPath(QString());
        CHECK(LocoConfigWindow::schemaFile() == QLatin1String(":/schema/kavach.xml"), "no external schema: the built-in copy");
        {
            QTemporaryDir data;
            LocoConfigWindow w(nullptr, data.path());
            QString summary, error;
            CHECK(!w.addField(u16After(QStringLiteral("x"), QStringLiteral("loco_doubtover")), &summary, &error)
                      && error.contains(QLatin1String("built-in")),
                  "the built-in schema is not changed: 'make an editable copy first'");
        }

        Settings::setSchemaPath(path);
        CHECK(LocoConfigWindow::schemaFile() == path, "an external schema: the editor reads it (as the decoder does)");
        QTemporaryDir data;
        {
            LocoConfigWindow w(nullptr, data.path());
            CHECK(w.isUsable() && w.fieldModel()->rowForKey(QStringLiteral("max_brake_cylinder_pressure")) < 0,
                  "before: no such row");
            QSignalSpy edited(&w, &LocoConfigWindow::schemaEdited);
            LinfoEdit::NewField f = u16After(QStringLiteral("max_brake_cylinder_pressure"), QStringLiteral("loco_doubtover"));
            f.defaultText = QStringLiteral("380");
            QString summary, error;
            CHECK(w.addField(f, &summary, &error), "addField writes the external schema");
            CHECK(edited.count() == 1 && edited.at(0).at(0).toString() == path
                      && summary.contains(QStringLiteral("%1 → %2 bytes").arg(size0).arg(size0 + 2))
                      && summary.contains(QLatin1String("sync_linfo.py")),
                  "schemaEdited(path, summary): sizes and the sync_linfo.py reminder");
            const QStringList backups = QDir(dir.path()).entryList({ QStringLiteral("kavach.xml.bak-*") });
            QFile b(dir.filePath(backups.value(0)));
            CHECK(backups.size() == 1 && b.open(QIODevice::ReadOnly) && b.readAll() == xml,
                  "the old file is kept beside it, byte for byte");
            w.close();
        }
        {
            LocoConfigWindow w(nullptr, data.path());
            const int row = w.fieldModel()->rowForKey(QStringLiteral("max_brake_cylinder_pressure"));
            const LocoInfo::LocoConfig c = w.configNamed(w.configNames().value(0));
            CHECK(row >= 0 && c.values.value(QStringLiteral("max_brake_cylinder_pressure")).toLongLong() == 380
                      && w.currentBody().size() == size0 + 2,
                  "reopened: the row is there, the saved configuration has it at 380, the body is 2 bytes longer");
            QString summary, error;
            CHECK(!w.removeField(QStringLiteral("vcc_crc"), &summary, &error) && !error.isEmpty(), "vcc_crc: refused");
            CHECK(w.removeField(QStringLiteral("max_brake_cylinder_pressure"), &summary, &error), "removeField");
            w.close();
        }
        {
            QFile f(path);
            CHECK(f.open(QIODevice::ReadOnly) && f.readAll() == xml, "add then remove: the schema is as it was");
            LocoConfigWindow w(nullptr, data.path());
            CHECK(w.fieldModel()->rowForKey(QStringLiteral("max_brake_cylinder_pressure")) < 0
                      && !w.configNamed(w.configNames().value(0)).values.contains(QStringLiteral("max_brake_cylinder_pressure"))
                      && w.currentBody().size() == size0,
                  "reopened: the row and the saved value are gone");
        }
        Settings::setSchemaPath(oldSchema);
    }

    // ---- the dialogs -----------------------------------------------------------
    {
        LinfoAddFieldDialog d(real, { QStringLiteral("Brake pressures") }, QStringLiteral("/x/kavach.xml"),
                              QStringLiteral("brake_pressure_used"), nullptr);
        CHECK(!d.addButton()->isEnabled() && d.problemText() == QLatin1String("Give the field a name"),
              "add dialog, no name: Add disabled, says why");
        CHECK(d.afterBox()->currentData().toString() == QLatin1String("brake_pressure_used")
                  && d.groupBox()->currentText() == QLatin1String("Brake pressures"),
              "preselected: after the right-clicked field, in its group");
        d.nameEdit()->setText(QStringLiteral("max_brake_cylinder_pressure"));
        d.typeBox()->setCurrentIndex(d.typeBox()->findText(QStringLiteral("uint16_t")));
        CHECK(d.addButton()->isEnabled() && d.previewText().contains(QStringLiteral("%1 → %2").arg(size0).arg(size0 + 2))
                  && d.previewText().contains(QLatin1String("firmware")) && d.previewText().contains(QLatin1String("/x/kavach.xml")),
              "add dialog: the line, the file, the sizes and the firmware warning");
        CHECK(!d.countBox()->isEnabled() && d.hexBox()->isEnabled(), "uint16_t: no N, hex possible");
        d.typeBox()->setCurrentIndex(d.typeBox()->findText(QStringLiteral("char[N]")));
        CHECK(d.countBox()->isEnabled() && !d.hexBox()->isEnabled() && d.field().charCount == d.countBox()->value(),
              "char[N]: N used, no hex");
        d.typeBox()->setCurrentIndex(d.typeBox()->findText(QStringLiteral("uint8_t")));
        d.defaultEdit()->setText(QStringLiteral("300"));
        CHECK(!d.addButton()->isEnabled() && d.problemText().startsWith(QLatin1String("Default")),
              "uint8_t default 300: refused");
    }
    {
        QHash<QString, QStringList> locked;
        locked.insert(QStringLiteral("loco_unit_id"), { QStringLiteral("WAP7 30281") });
        LinfoRemoveFieldDialog d(real, QStringLiteral("/x/kavach.xml"), QStringLiteral("loco_unit_id"), locked, nullptr);
        CHECK(d.key() == QLatin1String("loco_unit_id") && d.fieldBox()->findData(QStringLiteral("vcc_crc")) < 0
                  && d.fieldBox()->findData(QStringLiteral("loco_info_crc")) < 0,
              "remove dialog: preselected; vcc_crc and loco_info_crc not offered");
        CHECK(d.infoText().contains(QStringLiteral("%1 → %2").arg(size0).arg(size0 - 4))
                  && d.infoText().contains(QLatin1String("WAP7 30281")) && d.infoText().contains(QLatin1String("firmware")),
              "remove dialog: the sizes, where it is locked, the firmware warning");
    }
}
