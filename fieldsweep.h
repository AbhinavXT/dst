#ifndef FIELDSWEEP_H
#define FIELDSWEEP_H
// =====================================================================
//  fieldsweep.{h,cpp} -- walk one field across its values and record
//  what the target did about it.
//
//  PacketVary already varies a field from send to send, but it is a rule
//  evaluated per tick with nowhere to put an answer: it holds a session
//  open, it does not ask a question. A sweep is the question --
//  "which values of this field does the target accept?" -- so it plans a
//  finite list of values up front, sends them one at a time, and scores
//  each one by what came back.
//
//  Two halves, both pure and both here:
//
//    plan()      the values to send, in order, each with the reason it is
//                interesting ("min", "max", "first undefined enum code").
//                Boundary mode is the default because that is where
//                bit-packed field bugs live -- off-by-one at the top of
//                the range, sign handling at -1, the code one past the
//                last defined enum.
//
//    classify()  what a step's observations mean. Kept out of the dialog
//                so "silence" and "fault only" are decided by something
//                that can be tested against a table of cases rather than
//                by reading a UI.
//
//  Sending, and the wiring that feeds observations in, is the dialog's
//  job. Nothing in this file touches a socket or a widget.
// =====================================================================
#include <QSet>
#include <QString>
#include <QVector>

#include "schema/schemaencoder.h"     // FieldInfo, enum choices

namespace FieldSweep {

enum class Mode {
    Boundary,   // the edges of the field's range (and of its signedness)
    Range,      // from .. to, stepping
    List,       // an explicit list the operator typed
    EnumCodes,  // every declared enum value, plus the first undefined one
};

struct Spec {
    QString field;
    Mode    mode     = Mode::Boundary;
    int     bits     = 0;         // field width; 0 means unknown (no clamp)
    bool    isSigned = false;
    qint64  from = 0, to = 0, step = 1;
    QVector<qint64> list;                       // Mode::List
    QVector<QPair<qint64, QString>> enumChoices; // Mode::EnumCodes
    int     maxValues = 512;      // refuse rather than queue a 17-bit range
};

struct Step {
    qint64  value = 0;
    QString note;                 // why this value is in the plan
};

// The values to send, in order. Empty with *err set when the spec cannot be
// turned into a finite, sane list.
QVector<Step> plan(const Spec &s, QString *err);

// What one observation after a send was.
struct Observation {
    qint64  msAfterSend = 0;
    QString captype;
    bool    isFault = false;      // a fault packet, or a frame reporting faults
};

enum class Verdict {
    Pending,     // the response window has not closed yet
    Reply,       // something we count as an answer arrived
    FaultOnly,   // only fault traffic arrived
    Other,       // traffic arrived, but not of a type we were watching for
    Silent,      // nothing at all
};

// `replyTypes` are the captypes that count as an answer (e.g. {"aap"} when
// sweeping an ARP field). `windowMs` is how long after the send an
// observation still belongs to it.
Verdict classify(const QVector<Observation> &obs,
                 const QSet<QString> &replyTypes,
                 int windowMs);

QString verdictName(Verdict v);

// Largest / smallest value a field of `bits` can hold, honouring signedness.
qint64 maxValue(int bits, bool isSigned);
qint64 minValue(int bits, bool isSigned);

}  // namespace FieldSweep

#endif  // FIELDSWEEP_H
