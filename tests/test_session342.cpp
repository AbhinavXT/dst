#include "testutil.h"

#include "engunits.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "schemaencoder.h"

#include <QComboBox>
#include <QLineEdit>

// =============================================================================
//  Session 342 — values typed with their unit in the Packet Maker: a coded
//  speed (enum speed6, 5 km/h steps) takes "45 km/h" and writes code 9; a
//  speed no code means is refused by name; a field carried in metres takes
//  "1200 m" and refuses "1.2 km". The schema is the shipped kavach.xml.
// =============================================================================

namespace {
Schema::FieldInfo findField(const Schema::Encoder &enc, const QString &structName, const QString &name)
{
    for (const Schema::FieldInfo &f : enc.structLayout(structName).scalarFields)
        if (f.name == name) return f;
    return Schema::FieldInfo();
}

// Type into an editor's box and leave it, as the operator does.
void typeInto(QWidget *editor, const QString &text)
{
    QLineEdit *le = qobject_cast<QLineEdit *>(editor);
    if (auto *cb = qobject_cast<QComboBox *>(editor)) le = cb->lineEdit();
    le->setText(text);
    emit le->editingFinished();
}
}  // namespace

TEST_SUITE(session342)
{
    using namespace EngUnits;
    // ---- the conversions ------------------------------------------------------------
    double v = 0;
    CHECK(apply(QStringLiteral("v*5"), 9, &v) && v == 45, "v*5 at 9: 45");
    CHECK(apply(QStringLiteral("v-31"), 40, &v) && v == 9, "v-31 at 40: 9");
    CHECK(!apply(QStringLiteral("v*v"), 3, &v), "a formula of another form is not used");
    const QVector<Range> speed6{ { 1, 50, QStringLiteral("v*5"), QStringLiteral("km/h") } };
    qint64 raw = -1;
    QString why;
    CHECK(toRaw(speed6, QStringLiteral("45 km/h"), &raw, &why) && raw == 9, "45 km/h: code 9");
    CHECK(toRaw(speed6, QStringLiteral("250km/h"), &raw, &why) && raw == 50, "250km/h (no space): code 50");
    CHECK(!toRaw(speed6, QStringLiteral("47 km/h"), &raw, &why) && why.contains(QStringLiteral("5–250 km/h")),
          QByteArray("47 km/h: refused, saying what it carries: ") + why.toUtf8());
    CHECK(!toRaw(speed6, QStringLiteral("300 km/h"), &raw, &why), "300 km/h: past the last code, refused");
    CHECK(!toRaw(speed6, QStringLiteral("45 m"), &raw, &why) && why.contains(QStringLiteral("not given in m")),
          "another unit: refused");
    CHECK(text(speed6, 9) == QStringLiteral("45 km/h") && text(speed6, 0).isEmpty(), "code 9 reads 45 km/h; 0 is not in the range");
    CHECK(plainToRaw(QStringLiteral("m"), QStringLiteral("1200 m"), &raw, &why) && raw == 1200, "1200 m in metres: 1200");
    CHECK(!plainToRaw(QStringLiteral("m"), QStringLiteral("1.2 km"), &raw, &why) && why.contains(QStringLiteral("in m, not km")),
          "1.2 km into metres: refused, not converted");
    CHECK(!plainToRaw(QStringLiteral("m"), QStringLiteral("12.5 m"), &raw, &why), "a fraction of a metre: refused");

    // ---- in the Packet Maker, on the shipped schema -------------------------------------
    PacketBuilder builder;
    CHECK(builder.ready(), "fixture: the schema loaded");
    const Schema::Encoder &enc = builder.encoder();
    CHECK(enc.enumRanges(QStringLiteral("speed6")).size() == 1 && enc.enumRanges(QStringLiteral("pktDir")).isEmpty(),
          "speed6 has one formula range; pktDir none");
    bool labelled = false;
    for (const auto &c : enc.enumChoices(QStringLiteral("speed6"), true))
        if (c.first == 9 && c.second == QStringLiteral("45 km/h")) labelled = true;
    CHECK(labelled, "the choice list reads code 9 as \"45 km/h\", not \"v*5 km/h\"");

    const Schema::FieldInfo speed = findField(enc, QStringLiteral("MovementAuthority"), QStringLiteral("AUTHORIZED_SPEED"));
    CHECK(speed.enumName == QStringLiteral("speed6"), "fixture: MovementAuthority's AUTHORIZED_SPEED");
    QWidget *sp = PacketMakerDialog::makeFieldEditor(speed, enc, nullptr);
    typeInto(sp, QStringLiteral("45 km/h"));
    CHECK(PacketMakerDialog::readFieldEditor(sp) == 9, "typed 45 km/h: the frame gets code 9");
    typeInto(sp, QStringLiteral("47 km/h"));
    CHECK(PacketMakerDialog::readFieldEditor(sp) == 9 && sp->toolTip().startsWith(QStringLiteral("Not taken:")),
          "typed 47 km/h: not taken, the last value stays, the tooltip says why");
    typeInto(sp, QStringLiteral("62"));
    CHECK(PacketMakerDialog::readFieldEditor(sp) == 62, "a bare number is still the code itself");
    delete sp;

    Schema::FieldInfo dist;
    for (const Schema::FieldInfo &f : enc.packet(QStringLiteral("slrp")).header)
        if (f.name == QLatin1String("DIST_PKT_START")) dist = f;
    CHECK(dist.unit == QStringLiteral("m"), "fixture: SLRP's DIST_PKT_START, in m");
    QWidget *d = PacketMakerDialog::makeFieldEditor(dist, enc, nullptr);
    CHECK(d->toolTip().startsWith(QStringLiteral("In m.")), "its box says its unit");
    typeInto(d, QStringLiteral("1200 m"));
    CHECK(PacketMakerDialog::readFieldEditor(d) == 1200, "1200 m: 1200");
    typeInto(d, QStringLiteral("1.2 km"));
    CHECK(PacketMakerDialog::readFieldEditor(d) == 1200, "1.2 km: not taken, 1200 stays");
    delete d;
}
