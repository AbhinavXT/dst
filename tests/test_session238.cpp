#include "testutil.h"

#include "fieldsweep.h"
#include "packetmakerdialog.h"
#include "schemaencoder.h"

#include <QComboBox>
#include <QLineEdit>

// =============================================================================
//  Session 238 — Packet Maker offers named enum choices. Encoder::enumChoices
//  only looked at top-level <enum> elements; kavach.xml nests them in
//  <enums>, so every enum field was a bare number box and Field Sweep's
//  "Enum codes" refused every field. A field still starts at 0.
// =============================================================================

TEST_SUITE(session238)
{
    Schema::Encoder enc;
    QString err;
    CHECK(enc.load(QStringLiteral(":/schema/kavach.xml"), &err), "kavach.xml loads: " + err.toUtf8());

    const auto modes = enc.enumChoices(QStringLiteral("locoMode"));
    CHECK(modes.size() >= 12, "locoMode has its named codes");
    bool standBy = false;
    for (const auto &c : modes) standBy |= c.first == 1 && c.second == QLatin1String("1 (Stand_By)");
    CHECK(standBy, "1 reads '1 (Stand_By)', %v filled in");

    // speed6: 0, 1-50 (formula), 51-61 (reserved), 62, 63
    const auto plain = enc.enumChoices(QStringLiteral("speed6"));
    CHECK((plain.size()) == 3, "a picker lists speed6's exact values only");
    const auto ranged = enc.enumChoices(QStringLiteral("speed6"), true);
    CHECK((ranged.size()) == 64, "with ranges every 6-bit code is declared");
    bool reserved = false;
    for (const auto &c : ranged) reserved |= c.first == 55 && c.second == QLatin1String("55 (reserved)");
    CHECK(reserved, "a range label gets its value");
    CHECK(enc.enumChoices(QStringLiteral("noSuchEnum")).isEmpty(), "an unknown enum gives nothing");

    // the Packet Maker editor
    Schema::FieldInfo mode;
    mode.name = QStringLiteral("LOCO_MODE"); mode.bits = 4; mode.enumName = QStringLiteral("locoMode");
    QWidget *ed = PacketMakerDialog::makeFieldEditor(mode, enc, nullptr);
    auto *cb = qobject_cast<QComboBox *>(ed);
    CHECK(cb, "an enum field gets a picker");
    if (cb) {
        CHECK((PacketMakerDialog::readFieldEditor(cb)) == 0, "it starts at 0, as before");
        CHECK((cb->currentText()) == QStringLiteral("0 (?)"), "0 shown by its name");
        cb->setCurrentIndex(cb->findData(QVariant(qlonglong(4))));
        CHECK((PacketMakerDialog::readFieldEditor(cb)) == 4, "picking Full_Supervision gives 4");
        cb->setEditText(QStringLiteral("13"));
        CHECK((PacketMakerDialog::readFieldEditor(cb)) == 13, "a raw number is still taken");
    }
    delete ed;

    Schema::FieldInfo noZero;   // nmsModule declares 1-5
    noZero.name = QStringLiteral("MOD"); noZero.bits = 8; noZero.enumName = QStringLiteral("nmsModule");
    ed = PacketMakerDialog::makeFieldEditor(noZero, enc, nullptr);
    cb = qobject_cast<QComboBox *>(ed);
    CHECK(cb, "nmsModule gets a picker");
    if (cb) CHECK((PacketMakerDialog::readFieldEditor(cb)) == 0, "0 undeclared: still starts at 0");
    delete ed;

    Schema::FieldInfo rangeOnly;   // trainSpeed9: 0-400 range + 511
    rangeOnly.name = QStringLiteral("SPD"); rangeOnly.bits = 9; rangeOnly.enumName = QStringLiteral("trainSpeed9");
    ed = PacketMakerDialog::makeFieldEditor(rangeOnly, enc, nullptr);
    CHECK(qobject_cast<QComboBox *>(ed), "trainSpeed9 offers its one named code");
    delete ed;

    // Field Sweep's Enum codes now works on a real field
    FieldSweep::Spec s;
    s.field = QStringLiteral("LOCO_MODE"); s.bits = 4;
    s.mode = FieldSweep::Mode::EnumCodes;
    s.enumChoices = enc.enumChoices(QStringLiteral("locoMode"), true);
    QString why;
    const auto steps = FieldSweep::plan(s, &why);
    CHECK(!steps.isEmpty(), "Enum codes sweeps LOCO_MODE: " + why.toUtf8());
}
