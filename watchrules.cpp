#include "watchrules.h"

namespace WatchRules {

const QVector<WatchRule> &all()
{
    static const QVector<WatchRule> rules = {
        { QStringLiteral("eb"), QStringLiteral("EB applied"),
          QStringLiteral("@dmi field:brake_type=4"),
          QStringLiteral("@dmi brake_type 4 (EMERGENCY_BRAKE), as the incident report counts EB") },
        { QStringLiteral("fsb"), QStringLiteral("FSB applied"),
          QStringLiteral("@dmi field:brake_type=3"),
          QStringLiteral("@dmi brake_type 3 (FULL_SERVICE_BRAKE), as the incident report counts FSB") },
        { QStringLiteral("sos"), QStringLiteral("SoS"),
          QStringLiteral("@lsos"),
          QStringLiteral("Any LOCO_SOS frame (@lsos), as the two-loco view marks SoS") },
        { QStringLiteral("tsr"), QStringLiteral("TSR acted on"),
          QStringLiteral("field:TSR_STATUS=2"),
          QStringLiteral("TSR_STATUS 2 (Latest TSR): the only status under which TSR entries act") },
        { QStringLiteral("trip"), QStringLiteral("Trip mode"),
          QStringLiteral("field:LOCO_MODE=7"),
          QStringLiteral("LOCO_MODE 7 (Trip)") },
        { QStringLiteral("sysfail"), QStringLiteral("System failure mode"),
          QStringLiteral("field:LOCO_MODE=12"),
          QStringLiteral("LOCO_MODE 12 (System_Failure)") },
        { QStringLiteral("isolation"), QStringLiteral("Isolation mode"),
          QStringLiteral("field:LOCO_MODE=13"),
          QStringLiteral("LOCO_MODE 13 (Isolation)") },
        // @dmi and @rfid only: their CRC recipes are confirmed against real
        // frames. Every recorded @ccsys / @dlsys frame fails its check (see
        // CHANGELOG, Packet Maker round trip), so there it would fire on
        // every frame and say nothing.
        { QStringLiteral("crc"), QStringLiteral("CRC failed (DMI, RFID)"),
          QStringLiteral("(@dmi OR @rfid) field:CRC~FAIL"),
          QStringLiteral("An @dmi or @rfid frame whose CRC did not match. Not @ccsys / @dlsys: every recorded "
                         "frame of those fails, an open question") },
    };
    return rules;
}

const WatchRule *byId(const QString &id)
{
    for (const WatchRule &r : all()) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

}  // namespace WatchRules
