#include "testutil.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"
#include <cstdio>

// Does a field's span actually point at the bytes that produced its value?
// Mutate one byte, re-decode, and check that ONLY fields whose span covers
// that byte changed. This is the property that makes byte-linking trustworthy.

TEST_SUITE(fieldmutation)
{

    const Schema::Decoder &d = kavachSchema();
    CHECK(d.isLoaded(),"schema loaded");
    if(!d.isLoaded()) return;   // nothing further is meaningful without a schema

    const char *types[] = {"slrp","aap","aep","dip1","dip2","dop1","dop2","arp"};
    int tested=0, mismatched=0, confirmed=0;

    for (const char *ct : types) {
        QByteArray base(96,'\0');
        for(int i=0;i<base.size();++i) base[i]=char(i*13+5);
        const QString cts = QString::fromLatin1(ct);
        if(!d.handles(base,cts)) continue;
        QVector<FieldRow> a = d.decode(base,{},cts);
        if(a.isEmpty()) continue;

        for(int flip=0; flip<base.size(); flip+=7){         // sample bytes
            QByteArray mut = base;
            mut[flip] = char(quint8(mut[flip]) ^ 0xFF);
            QVector<FieldRow> b = d.decode(mut,{},cts);
            if(b.size()!=a.size()) continue;   // structure changed; skip
            ++tested;
            for(int i=0;i<a.size();++i){
                const bool changed = (a[i].value != b[i].value);
                if(!a[i].hasSpan()) continue;
                const bool covers = (flip>=a[i].byteStart() && flip<=a[i].byteEnd());
                if(changed && !covers){
                    // value moved but its span says this byte is unrelated
                    ++mismatched;
                    if(mismatched<=3)
                        printf("  MISMATCH %s '%s': byte %d changed value but span is %d-%d\n",
                               ct,qPrintable(a[i].field),flip,a[i].byteStart(),a[i].byteEnd());
                }
                if(changed && covers) ++confirmed;
            }
        }
    }
    printf("  %d mutations, %d confirmed span hits, %d mismatches\n",
           tested,confirmed,mismatched);
    CHECK(tested>0,"mutations were run");
    CHECK(confirmed>0,"changing a byte changed fields whose span covers it");
    CHECK(mismatched==0,"no field changed without its span covering the byte");
}
