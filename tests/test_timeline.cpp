#include "testutil.h"
#include "timelineribbon.h"
#include "markerscrollbar.h"
#include "logmodel.h"
#include <QSortFilterProxyModel>
#include <cstdio>

static LogEntryPtr mk(qint64 ms,Severity s=Severity::Info,bool bm=false){
    auto e=QSharedPointer<LogEntry>::create();
    e->header.source_id=33; e->header.kvchId=1; e->epochMs=ms;
    e->severity=s; e->bookmarked=bm; e->text="x"; e->cacheDerived(); return e;
}

TEST_SUITE(timeline)
{


    // ---- degenerate inputs must not crash or misbehave ----
    CHECK(binLogModel(nullptr,10).isEmpty(),"null model -> empty");
    { LogModel m(nullptr,100); CHECK(binLogModel(&m,10).isEmpty(),"empty model -> empty"); }
    { LogModel m(nullptr,100); QVector<LogEntryPtr> v{mk(1000)}; m.appendEntries(v);
      CHECK(binLogModel(&m,0).isEmpty(),"zero bins -> empty");
      CHECK(binLogModel(&m,-5).isEmpty(),"negative bins -> empty"); }

    // ---- all entries share one timestamp (span == 0) ----
    { LogModel m(nullptr,100);
      QVector<LogEntryPtr> v{mk(5000),mk(5000),mk(5000)}; m.appendEntries(v);
      TimelineBins b=binLogModel(&m,10);
      CHECK(b.entries==3,"span 0: all counted");
      CHECK(b.bins[0].total==3,"span 0: all land in bin 0");
      CHECK(b.maxTotal==3,"span 0: maxTotal");
      CHECK(b.timeForBin(0)==5000,"span 0: timeForBin returns the timestamp"); }

    // ---- even distribution ----
    { LogModel m(nullptr,10000); QVector<LogEntryPtr> v;
      for(int i=0;i<100;++i) v.append(mk(1000+i*10));   // 1000..1990
      m.appendEntries(v);
      TimelineBins b=binLogModel(&m,10);
      CHECK(b.entries==100,"all 100 binned");
      CHECK(b.minMs==1000 && b.maxMs==1990,"span from first/last row");
      int sum=0; for(auto&x:b.bins) sum+=x.total;
      CHECK(sum==100,"bins sum to the entry count");
      bool even=true; for(auto&x:b.bins) if(x.total<8||x.total>12) even=false;
      CHECK(even,"roughly even spread across 10 bins"); }

    // ---- boundary: last entry must land in the last bin, not out of range ----
    { LogModel m(nullptr,1000); QVector<LogEntryPtr> v;
      for(int i=0;i<50;++i) v.append(mk(1000+i));
      m.appendEntries(v);
      TimelineBins b=binLogModel(&m,50);
      CHECK(b.binForTime(b.maxMs)==49,"max timestamp maps to last bin");
      CHECK(b.binForTime(b.minMs)==0,"min timestamp maps to first bin");
      CHECK(b.binForTime(b.minMs-1)==-1,"before span -> -1");
      CHECK(b.binForTime(b.maxMs+1)==-1,"after span -> -1");
      int sum=0; for(auto&x:b.bins) sum+=x.total;
      CHECK(sum==50,"no entry lost at the boundary"); }

    // ---- severity counted separately from total ----
    { LogModel m(nullptr,1000);
      QVector<LogEntryPtr> v;
      for(int i=0;i<499;++i) v.append(mk(1000+i,Severity::Info));
      v.append(mk(1000+499,Severity::Error));        // 1 error among 500
      m.appendEntries(v);
      TimelineBins b=binLogModel(&m,10);
      int errs=0,tot=0; for(auto&x:b.bins){errs+=x.error;tot+=x.total;}
      CHECK(tot==500,"500 total");
      CHECK(errs==1,"the single error is counted");
      // and it lives in a bin, so a full-height tick will be drawn for it
      bool found=false; for(auto&x:b.bins) if(x.error>0) found=true;
      CHECK(found,"rare error survives binning (would be invisible if proportional)"); }

    // ---- warn + bookmark counters ----
    { LogModel m(nullptr,1000);
      QVector<LogEntryPtr> v{mk(1000,Severity::Warn),mk(2000,Severity::Info,true),
                             mk(3000,Severity::Error,true)};
      m.appendEntries(v);
      TimelineBins b=binLogModel(&m,3);
      int w=0,e=0,bm=0; for(auto&x:b.bins){w+=x.warn;e+=x.error;bm+=x.bookmarks;}
      CHECK(w==1,"warn counted"); CHECK(e==1,"error counted");
      CHECK(bm==2,"bookmarks counted independently of severity"); }

    // ---- timeForBin is inside its bucket ----
    { LogModel m(nullptr,1000); QVector<LogEntryPtr> v;
      for(int i=0;i<100;++i) v.append(mk(10000+i*100));
      m.appendEntries(v);
      TimelineBins b=binLogModel(&m,20);
      for(int i=0;i<20;++i){
        qint64 t=b.timeForBin(i);
        CHECK(b.binForTime(t)==i, "timeForBin round-trips to its own bin"); }
      CHECK(b.timeForBin(-99)==b.timeForBin(0),"out-of-range index clamps low");
      CHECK(b.timeForBin(9999)==b.timeForBin(19),"out-of-range index clamps high"); }

    // ---- scrollbar row binning ----
    { LogModel m(nullptr,1000); QVector<LogEntryPtr> v;
      for(int i=0;i<200;++i) v.append(mk(1000+i, i==150?Severity::Error:Severity::Info));
      m.appendEntries(v);
      RowMarks r=binRowMarks(nullptr,&m,100);
      CHECK(r.rows==200,"row count");
      int marked=0; for(auto&b:r.buckets) if(b.error) ++marked;
      CHECK(marked==1,"one error bucket");
      CHECK(r.buckets[75].error,"error at row 150 of 200 lands in bucket 75");
      CHECK(binRowMarks(nullptr,nullptr,10).isEmpty(),"null source -> empty");
      CHECK(binRowMarks(nullptr,&m,0).isEmpty(),"zero buckets -> empty"); }

    // ---- marks follow the PROXY, not the source ----
    { LogModel m(nullptr,1000); QVector<LogEntryPtr> v;
      for(int i=0;i<100;++i) v.append(mk(1000+i, i<50?Severity::Error:Severity::Info));
      m.appendEntries(v);
      QSortFilterProxyModel px; px.setSourceModel(&m);
      px.setFilterKeyColumn(LogModel::ColSeverity);
      px.setFilterFixedString("ERR");        // keep only the 50 errors
      RowMarks r=binRowMarks(&px,&m,50);
      CHECK(r.rows==50,"proxy row count used, not source");
      int marked=0; for(auto&b:r.buckets) if(b.error) ++marked;
      CHECK(marked==50,"every visible row is an error mark");
}
}
