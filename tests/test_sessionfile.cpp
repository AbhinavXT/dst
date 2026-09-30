#include "testutil.h"
#include "sessionfile.h"
#include <cstdio>
#include <cassert>

using namespace SessionFile;

TEST_SUITE(sessionfile)
{

    // --- header round trip ---
    FileHeader h; h.createdMs=1754451234567LL; h.sourceId=33; h.kvchId=7;
    QByteArray hb = encodeHeader(h);
    CHECK(hb.size()==32, "header is 32 bytes");
    FileHeader out;
    CHECK(decodeHeader(hb,&out), "header decodes");
    CHECK(out.createdMs==h.createdMs, "createdMs round trip");
    CHECK(out.sourceId==33 && out.kvchId==7, "ids round trip");
    CHECK(out.version==kVersion && out.headerLen==32, "version/len");

    // bad magic rejected
    QByteArray bad=hb; bad.data()[0]='X';
    CHECK(!decodeHeader(bad,&out), "bad magic rejected");
    // future version rejected
    QByteArray fut=hb; putU16(fut.data()+8, 99);
    CHECK(!decodeHeader(fut,&out), "future version rejected");
    // short buffer rejected
    CHECK(!decodeHeader(QByteArray(10,'\0'),&out), "short header rejected");

    // --- record round trip + manual decode mirroring SessionReader ---
    const char wire[]={ (char)33,(char)101,(char)5,(char)0x0A,(char)0x00,(char)0x07,(char)0x00,
                        'R','A','D',' ','I','N' };
    QByteArray w(wire,sizeof(wire));
    qint64 t=1754451299123LL;
    QByteArray rec = encodeRecord(t,w);
    CHECK(rec.size()==4+8+2+w.size()+4, "record size formula");

    const char*p=rec.constData();
    quint32 bodyLen=getU32(p);
    CHECK((int)bodyLen==rec.size()-4, "bodyLen consistent");
    CHECK(getI64(p+4)==t, "arrivalMs round trip");
    quint16 wl=getU16(p+12);
    CHECK(wl==w.size(), "wireLen round trip");
    CHECK(memcmp(p+14,w.constData(),wl)==0, "wire bytes round trip");
    quint32 stored=getU32(p+14+wl);
    quint32 calc=crc32Final(crc32(p+4,10+wl));
    CHECK(stored==calc, "crc verifies");
    CHECK((quint32)wl+8+2+4==bodyLen, "wireLen/bodyLen agree (reader check)");

    // corruption is caught
    QByteArray corrupt=rec; corrupt.data()[16]^=0x01;
    quint32 c2=crc32Final(crc32(corrupt.constData()+4,10+wl));
    CHECK(c2!=stored, "single bit flip detected");

    // empty payload (header-only datagram)
    QByteArray hdrOnly(wire,7);
    QByteArray r2=encodeRecord(1,hdrOnly);
    CHECK(getU16(r2.constData()+12)==7, "7-byte wire ok");
    CHECK(crc32Final(crc32(r2.constData()+4,17))==getU32(r2.constData()+21), "crc on min record");

    // reader's plausibility bounds
    quint32 minBody=8+2+4, maxBody=8+2+kMaxWireBytes+4;
    CHECK(bodyLen>=minBody && bodyLen<=maxBody, "typical record within bounds");
    QByteArray big=encodeRecord(1,QByteArray(kMaxWireBytes,'z'));
    CHECK((quint32)getU32(big.constData())==maxBody, "max record hits upper bound exactly");

    // endianness is explicit, not host-dependent
    char b4[4]; putU32(b4,0x01020304u);
    CHECK((quint8)b4[0]==0x04&&(quint8)b4[3]==0x01, "putU32 is little-endian");
    char b8[8]; putI64(b8,-2LL);
    CHECK((quint8)b8[0]==0xFE&&(quint8)b8[7]==0xFF, "putI64 LE two's complement");
    CHECK(getI64(b8)==-2LL, "negative i64 round trip");

    // crc32 known-answer: "123456789" -> 0xCBF43926
    const char kav[]="123456789";
    CHECK(crc32Final(crc32(kav,9))==0xCBF43926u, "CRC-32 known answer vector");
}
