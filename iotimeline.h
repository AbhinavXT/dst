#ifndef IOTIMELINE_H
#define IOTIMELINE_H

// =============================================================================
//  Cab inputs and outputs (session 176) — Tools ▸ Monitor ▸ Cab inputs and outputs…
//  -----------------------------------------------------------------------------
//  What the driver did, and what the unit drove, around an event: the DIO
//  logs (@dip1/@dip2 inputs, @dop1/@dop2 outputs, from the IOA over serial)
//  as rows under the loco's mode, on the fault timeline's chart (174).
//
//  One row per signal that CHANGES in the tab; a bar wherever it differs
//  from its usual value (the value it holds most of the time). That reads
//  the same for normally-open and normally-closed contacts and for active-
//  low feedback: a DMI button press, the cab switched to reverse, the horn
//  solenoid, traction cut off. The bar says which value it read.
//    inputs   @dip1 / @dip2 fields ("5 dmi1_common_nc" -> "DIP1 dmi1_common_nc")
//    outputs  @dop1 / @dop2 pin[i] output= bit ("DOP1 pin 3")
// =============================================================================

#include "faulttimeline.h"

class LogModel;

namespace IoTimeline {

FaultTimeline::Timeline build(const LogModel *model);

}  // namespace IoTimeline

#endif // IOTIMELINE_H
