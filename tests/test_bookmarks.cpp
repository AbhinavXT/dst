#include "testutil.h"
#include "bookmarks.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "udpcommunication.h"
#include "colorrules.h"
#include "namemap.h"
#include <QUdpSocket>
#include <QTimer>
#include <QThread>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <QEventLoop>

static LogEntryPtr mk(quint8 s,quint16 k,qint64 ms,const QString&t){
    auto e=QSharedPointer<LogEntry>::create();
    e->header.source_id=s; e->header.kvchId=k; e->epochMs=ms; e->text=t;
    e->cacheDerived(); return e;
}

// Drives real sockets and drain timers, so it spins its own event loop
// and blocks until the assertions have run.
TEST_SUITE(bookmarks)
{
    QEventLoop loop;

    // Temp dir rather than a hardcoded /tmp path: the suite must run on
    // Windows and must not collide with a developer's real bookmarks.
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    const QString path = tmp.path() + "/bookmarks.json";
    QFile::remove(path);

    // ---------- BookmarkStore ----------
    { BookmarkStore bs; bs.load(path);
      CHECK(bs.count()==0,"empty store on first load");
      CHECK(bs.toggle("33_1",1000,"first")==true,"toggle adds");
      CHECK(bs.count()==1,"count after add");
      CHECK(bs.has("33_1",1000),"has() finds it");
      CHECK(!bs.has("33_1",1001),"has() rejects other timestamp");
      CHECK(!bs.has("21_2",1000),"has() rejects other tab");
      CHECK(bs.toggle("33_1",1000,"first")==false,"toggle removes");
      CHECK(bs.count()==0,"count after remove");
      // time ordering
      bs.toggle("33_1",3000,"c"); bs.toggle("21_2",1000,"a"); bs.toggle("33_1",2000,"b");
      CHECK(bs.count()==3,"three bookmarks");
      CHECK(bs.all().at(0).epochMs==1000 && bs.all().at(1).epochMs==2000
            && bs.all().at(2).epochMs==3000,"stored in time order across tabs");
      bs.setNote(1,"the interesting one");
      CHECK(bs.all().at(1).note=="the interesting one","note saved");
      CHECK(bs.all().at(1).messageSnippet=="b","snippet preserved");
    }
    // ---------- persistence across restart ----------
    { BookmarkStore bs2; CHECK(bs2.load(path),"reloads from disk");
      CHECK(bs2.count()==3,"all three survived restart");
      CHECK(bs2.all().at(1).note=="the interesting one","note survived restart");
      CHECK(bs2.all().at(0).tabKey=="21_2","tab key survived");
      CHECK(bs2.has("33_1",3000),"lookup works after reload");

      // applyToModel re-flags entries (the reloaded-session path)
      LogModel m(nullptr,1000);
      QVector<LogEntryPtr> v{ mk(33,1,1500,"x"), mk(33,1,2000,"y"), mk(33,1,2500,"z") };
      m.appendEntries(v);
      bs2.applyToModel(&m,"33_1");
      CHECK(!m.entryAt(0)->bookmarked,"non-bookmarked row untouched");
      CHECK(m.entryAt(1)->bookmarked,"matching timestamp re-flagged");
      CHECK(!m.entryAt(2)->bookmarked,"other rows untouched");
      // a bookmark for a different tab must not flag this model
      LogModel m2(nullptr,1000);
      QVector<LogEntryPtr> v2{ mk(99,9,2000,"other") };
      m2.appendEntries(v2);
      bs2.applyToModel(&m2,"99_9");
      CHECK(!m2.entryAt(0)->bookmarked,"tab key is part of identity");

      bs2.remove(0); CHECK(bs2.count()==2,"remove by index");
      bs2.clear();   CHECK(bs2.count()==0,"clear");
    }
    { BookmarkStore bs3; bs3.load(path); CHECK(bs3.count()==0,"clear persisted"); }
    // corrupt file must not crash or throw away the app
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write("{not json"); f.close();
      BookmarkStore bs4; CHECK(!bs4.load(path),"corrupt file reports failure");
      CHECK(bs4.count()==0,"corrupt file yields empty store, no crash"); }

    // ---------- merged ordering via allEntriesAppended ----------
    ColorRules rules; rules.loadFromFile("/nonexistent");
    NameMap names;
    MessageDispatcher disp(nullptr);
    disp.setColorRules(&rules); disp.setNameMap(&names);
    UDPCommunication recv(50222,50000,nullptr);
    disp.attachReceiver(&recv);
    QVector<LogEntryPtr> merged;
    QObject::connect(&disp,&MessageDispatcher::allEntriesAppended,
        [&merged](QVector<LogEntryPtr> v){ merged += v; });
    recv.start();

    QTimer::singleShot(600,[&](){
        QUdpSocket s;
        struct { quint8 a,b,c; quint16 l,k; } __attribute__((packed)) h;
        // deliberately alternate sources so a per-key grouping would be visible
        quint8 srcs[]={33,21,99,33,21,99};
        quint16 kvs[]={1,2,7,1,2,7};
        for(int i=0;i<24;++i){
            int j=i%6; QByteArray body=QByteArray("m")+QByteArray::number(i);
            h.a=srcs[j]; h.b=101; h.c=7; h.l=body.size(); h.k=kvs[j];
            QByteArray d(reinterpret_cast<char*>(&h),7); d+=body;
            s.writeDatagram(d,QHostAddress::LocalHost,50222);
            QThread::msleep(4);   // spread across several drain ticks
        }
    });
    QTimer::singleShot(2200,[&](){
        recv.quit(); recv.wait(2000);
        CHECK(merged.size()==24,"merged stream got every message");
        bool sorted=true, interleaved=false;
        for(int i=1;i<merged.size();++i){
            if(merged[i]->epochMs < merged[i-1]->epochMs) sorted=false;
            if(merged[i]->tabKey()!=merged[i-1]->tabKey()) interleaved=true;
        }
        CHECK(sorted,"merged stream is monotonic in time");
        CHECK(interleaved,"merged stream actually interleaves sources");
        // and it should NOT be grouped by key
        int runs=1; for(int i=1;i<merged.size();++i)
            if(merged[i]->tabKey()!=merged[i-1]->tabKey()) ++runs;
        CHECK(runs>merged.size()/2,"sources alternate rather than being batched");
        loop.quit();
    });
    loop.exec();
}
