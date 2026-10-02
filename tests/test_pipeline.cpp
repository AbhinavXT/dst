#include "testutil.h"
#include "udpcommunication.h"
#include "messagedispatcher.h"
#include "logwriter.h"
#include "logmodel.h"
#include "colorrules.h"
#include "namemap.h"
#include "sessionreader.h"
#include <QUdpSocket>
#include <QTimer>
#include <QDir>
#include <QTemporaryDir>
#include <QDirIterator>
#include <cstdio>
#include <QEventLoop>



// Drives real sockets and drain timers, so it spins its own event loop
// and blocks until the assertions have run.
TEST_SUITE(pipeline)
{
    QEventLoop loop;

    // Same reasoning as the bookmark suite: no hardcoded absolute paths.
    QTemporaryDir tmpRoot;
    CHECK(tmpRoot.isValid(), "temp log root created");
    const QString root = tmpRoot.path() + "/OUT";
    QDir(root).removeRecursively();

    ColorRules rules; rules.loadFromFile("/nonexistent");  // -> defaults
    NameMap names;
    MessageDispatcher disp(nullptr);
    disp.setColorRules(&rules); disp.setNameMap(&names);
    UDPCommunication recv(50111, 50000, nullptr);
    disp.attachReceiver(&recv);
    LogWriter writer(root, 100*1024*1024, nullptr);
    writer.setNameMap(names); writer.setRawCapture(true);
    writer.setMaxFolderBytes(0);
    QObject::connect(&disp,&MessageDispatcher::entriesAppended,
        [&writer](QString,QVector<LogEntryPtr> v){ writer.enqueueBatch(v); });

    bool bound=false;
    QObject::connect(&recv,&UDPCommunication::bindSucceeded,[&](quint16){bound=true;});
    QObject::connect(&recv,&UDPCommunication::bindFailed,[&](QString e){printf("BIND FAIL: %s\n",qPrintable(e));});
    recv.start(); writer.start();

    QTimer::singleShot(600,[&](){
        CHECK(bound,"UDP socket bound");
        QUdpSocket s;
        #pragma pack(push, 1)   // portable packing; __attribute__((packed)) is GCC-only (session 116)
        struct { quint8 src,dst,mid; quint16 len,kv; } h;
        #pragma pack(pop)
        const char* texts[]={"RAD IN Link 1 Error","CAN OUT No Error","RAD OUT ack"};
        quint8 srcs[]={33,21,33}; quint16 kvs[]={1,2,1};
        for(int i=0;i<30;++i){
            int k=i%3; QByteArray body(texts[k]);
            h.src=srcs[k]; h.dst=101; h.mid=7; h.len=body.size(); h.kv=kvs[k];
            QByteArray d(reinterpret_cast<char*>(&h),7); d+=body;
            s.writeDatagram(d,QHostAddress::LocalHost,50111);
        }
        // misaddressed: must be ignored, not logged
        h.src=33;h.dst=55;h.mid=7;h.len=4;h.kv=1;
        QByteArray d(reinterpret_cast<char*>(&h),7); d+="nope";
        s.writeDatagram(d,QHostAddress::LocalHost,50111);
    });

    QTimer::singleShot(2500,[&](){
        recv.quit(); recv.wait(2000);
        CHECK(writer.stop(3000),"writer stopped cleanly");

        // --- models ---
        CHECK(disp.knownKeys().size()==2,"two tabs created (misaddressed ignored)");
        LogModel* m33=disp.modelForKey("33_1");
        CHECK(m33 && m33->count()==20,"33_1 got 20 entries");
        if(m33 && m33->count()){
            LogEntryPtr e=m33->entryAt(0);
            CHECK(e && e->severity==Severity::Error,"severity classified");
            CHECK(e && e->direction==Direction::In,"DIRECTION classified (the tier-1 fix)");
            CHECK(e && e->epochMs>0,"arrival timestamp set");
            CHECK(e && e->rawBytes.size()==7+19,"rawBytes = header+payload");
        }
        LogModel* m21=disp.modelForKey("21_2");
        if(m21&&m21->count()){
            LogEntryPtr e=m21->entryAt(0);
            CHECK(e->severity==Severity::Info,"'No Error' -> Info not Error");
            CHECK(e->direction==Direction::Out,"CAN OUT -> Out");
        }

        // --- files on disk ---
        QStringList logs,dlrs;
        QDirIterator it(root,QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()){ it.next();
            if(it.filePath().endsWith(".log")) logs<<it.filePath();
            if(it.filePath().endsWith(".dlr")) dlrs<<it.filePath(); }
        CHECK(logs.size()==2,"two .log files written");
        CHECK(dlrs.size()==2,"two .dlr sidecars written");

        // --- read a .dlr back and rebuild entries ---
        int total=0; bool matched=false;
        for(const QString&p:dlrs){
            SessionReader r;
            CHECK(r.open(p),"session opens");
            QString key=r.tabKey();
            while(r.next()){
                ++total;
                LogEntryPtr e=MessageDispatcher::buildEntry(r.wire(),r.arrivalMs(),&rules);
                CHECK(!e.isNull(),"record rebuilds into an entry");
                CHECK(!e || e->tabKey()==key,
                      "record rebuilds into the tab it was written from");
                if(e && key=="33_1" && e->text=="RAD IN Link 1 Error"){
                    matched=true;
                    CHECK(e->direction==Direction::In,"replayed entry re-classified with direction");
                }
            }
            CHECK(r.status()==SessionReader::Ok,"clean EOF, no corruption");
        }
        CHECK(total==30,"all 30 datagrams round-tripped through .dlr");
        CHECK(matched,"replayed text matches what was sent");

        // truncation tolerance: lop the last few bytes off a .dlr
        if(!dlrs.isEmpty()){
            QFile f(dlrs.first());
            f.open(QIODevice::ReadWrite); qint64 sz=f.size(); f.resize(sz-5); f.close();
            SessionReader r; r.open(dlrs.first());
            int got=0; while(r.next()) ++got;
            CHECK(r.status()==SessionReader::TruncatedTail,"truncated tail detected");
            CHECK(got>0,"records before the tear still recovered");
        }
        loop.quit();
    });
    loop.exec();
}
