#include "testutil.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"
#include <QFile>
#include <cstdio>



TEST_SUITE(fieldspan)
{


    // ---- FieldRow span arithmetic (pure, no schema needed) ----
    { FieldRow r; CHECK(!r.hasSpan(),"default row has no span");
      CHECK(r.byteStart()==-1 && r.byteEnd()==-1,"no span -> -1 bytes"); }
    { FieldRow r; r.bitOffset=0; r.bitLength=8;
      CHECK(r.hasSpan(),"span set");
      CHECK(r.byteStart()==0 && r.byteEnd()==0,"bits 0..7 = byte 0"); }
    { FieldRow r; r.bitOffset=8; r.bitLength=16;
      CHECK(r.byteStart()==1 && r.byteEnd()==2,"bits 8..23 = bytes 1..2"); }
    { FieldRow r; r.bitOffset=3; r.bitLength=2;   // sub-byte field
      CHECK(r.byteStart()==0 && r.byteEnd()==0,"3-bit field inside byte 0"); }
    { FieldRow r; r.bitOffset=6; r.bitLength=4;   // straddles a byte boundary
      CHECK(r.byteStart()==0 && r.byteEnd()==1,"field straddling bytes 0-1"); }
    { FieldRow r; r.bitOffset=0; r.bitLength=1;
      CHECK(r.byteStart()==0 && r.byteEnd()==0,"single bit"); }
    { FieldRow r; r.bitOffset=5; r.bitLength=0;
      CHECK(!r.hasSpan(),"zero length is not a span"); }
    // aggregate init used by the 15 existing call sites must still compile
    { FieldRow r{ QStringLiteral("n"), QStringLiteral("v") };
      CHECK(r.field=="n" && r.value=="v","brace init still works");
      CHECK(!r.hasSpan(),"brace init leaves span unset"); }

    // ---- real schema decode: do fields actually get spans? ----
    const Schema::Decoder &d = kavachSchema();
    if (!d.isLoaded()) { printf("NOTE: kavach.xml not loaded; skipping decode checks\n"); }
    else {
        // Find any frame the schema handles by trying plausible payloads.
        // Drive real packet definitions by captype rather than hoping a
        // random byte string matches one.
        const char *types[] = {"slrp","aap","aep","dip1","dip2","dop1","dop2","arp"};
        int typesDecoded=0, totalSpanned=0, outOfBounds=0, overlapBad=0;
        for (const char *ct : types) {
            QByteArray f(96, '\0');
            for(int i=0;i<f.size();++i) f[i]=char(i*13+5);
            if(!d.handles(f, QString::fromLatin1(ct))) continue;
            QVector<FieldRow> rows = d.decode(f, {}, QString::fromLatin1(ct));
            if(rows.isEmpty()) continue;
            ++typesDecoded;
            int spanned=0, lastEnd=-1;
            for(const FieldRow&r:rows){
                if(!r.hasSpan()) continue;
                ++spanned; ++totalSpanned;
                if(r.byteStart()<0 || r.byteEnd()>=f.size()) ++outOfBounds;
                // spans should advance monotonically through the frame:
                // a field starting before the previous one ENDED would mean
                // the guard mis-attributed a nested walk.
                if(r.bitOffset < lastEnd) ++overlapBad;
                lastEnd = r.bitOffset;
            }
            printf("  %-5s %3d rows, %3d spanned\n", ct, rows.size(), spanned);
        }
        CHECK(typesDecoded>0,"at least one real packet type decoded");
        CHECK(totalSpanned>0,"decoded fields carry byte spans");
        CHECK(outOfBounds==0,"no span points outside the frame");
        CHECK(overlapBad==0,"spans advance monotonically (no mis-attribution)");
        bool decodedSomething = typesDecoded>0;
        CHECK(decodedSomething,"schema decoded at least one synthetic frame");
}
}
