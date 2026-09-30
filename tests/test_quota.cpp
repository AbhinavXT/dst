#include "testutil.h"
#include <cstdio>
#include <cstdint>

// Mirror of LogWriter::updateQuotaState()'s decision logic, to check the
// hysteresis actually prevents flapping.
static constexpr double kResumeFraction = 0.95;
struct W {
    int64_t limit; int64_t used=0; bool susp=false; int transitions=0;
    void step(int64_t newUsed){
        used=newUsed;
        if(limit<=0){ if(susp){susp=false;++transitions;} return; }
        int64_t resumeAt=(int64_t)((double)limit*kResumeFraction);
        if(!susp && used>=limit){ susp=true; ++transitions; }
        else if(susp && used<=resumeAt){ susp=false; ++transitions; }
    }
};

TEST_SUITE(diskquota)
{

    const int64_t MB=1024*1024, LIM=100*MB;
    { W w{LIM};
      w.step(50*MB); CHECK(!w.susp,"under limit -> running");
      w.step(99*MB); CHECK(!w.susp,"just under -> running");
      w.step(100*MB); CHECK(w.susp,"at limit -> suspended");
      w.step(101*MB); CHECK(w.susp,"over -> stays suspended");
      // operator deletes a tiny file: must NOT resume (this is the flap case)
      w.step(99*MB); CHECK(w.susp,"barely-under does NOT resume (hysteresis)");
      w.step(96*MB); CHECK(w.susp,"still above resume threshold");
      w.step(95*MB); CHECK(!w.susp,"at 95% -> resumes");
      CHECK(w.transitions==2,"exactly two transitions, no flapping");
    }
    // repeated small deletions near the line must not oscillate
    { W w{LIM}; w.step(100*MB);
      int before=w.transitions;
      for(int i=0;i<50;++i) w.step((int64_t)(99.9*MB)-i*1024);
      CHECK(w.transitions==before,"50 near-line samples cause zero transitions");
    }
    // limit removed while suspended -> resumes immediately
    { W w{LIM}; w.step(200*MB); CHECK(w.susp,"suspended");
      w.limit=0; w.step(200*MB); CHECK(!w.susp,"limit 0 clears suspension"); }
    // limit lowered below current usage -> suspends on next evaluation
    { W w{LIM}; w.step(60*MB); CHECK(!w.susp,"running");
      w.limit=50*MB; w.step(60*MB); CHECK(w.susp,"lowering limit suspends"); }
    // exact boundary arithmetic
    { W w{LIM}; int64_t r=(int64_t)((double)LIM*0.95);
      CHECK(r==99614720,"resume threshold = 95 MiB exactly");
}
}

// ---------------------------------------------------------------------------
// Free-space floor. Same state machine as the folder budget, but tripped by
// a condition the budget cannot see: the disk filling for reasons that have
// nothing to do with us.
// ---------------------------------------------------------------------------
namespace {
struct FreeGuard {
    int64_t limit = 0;       // folder budget, 0 = off
    int64_t minFree = 0;     // free-space floor, 0 = off
    int64_t used = 0, freeSpace = -1;
    bool susp = false; int transitions = 0;
    void step(int64_t u, int64_t f) {
        used = u; freeSpace = f;
        if (limit <= 0 && minFree <= 0) {
            if (susp) { susp = false; ++transitions; }
            return;
        }
        const int64_t resumeAt = limit > 0 ? (int64_t)((double)limit * 0.95) : 0;
        const bool over = (limit > 0 && used >= limit);
        const bool low  = (minFree > 0 && freeSpace >= 0 && freeSpace < minFree);
        if (!susp && (over || low)) { susp = true; ++transitions; return; }
        if (susp && (limit <= 0 || used <= resumeAt)
                 && (minFree <= 0 || freeSpace < 0
                     || freeSpace >= (int64_t)((double)minFree / 0.95))) {
            susp = false; ++transitions;
        }
    }
};
}  // namespace

TEST_SUITE(freespace)
{
    const int64_t MB = 1024 * 1024;

    // Floor alone, budget off: a full disk must suspend even though we
    // wrote almost nothing.
    { FreeGuard g; g.minFree = 500 * MB;
      g.step(10 * MB, 900 * MB); CHECK(!g.susp, "plenty free -> running");
      g.step(10 * MB, 499 * MB); CHECK(g.susp,  "below the floor -> suspended");
      g.step(10 * MB, 510 * MB); CHECK(g.susp,  "barely above floor does NOT resume");
      g.step(10 * MB, 527 * MB); CHECK(!g.susp, "clear of hysteresis -> resumes");
      CHECK(g.transitions == 2, "no flapping around the floor"); }

    // Budget and floor together: either trips it, both must clear.
    { FreeGuard g; g.limit = 100 * MB; g.minFree = 500 * MB;
      g.step(50 * MB, 900 * MB); CHECK(!g.susp, "both clear");
      g.step(50 * MB, 100 * MB); CHECK(g.susp,  "floor trips while budget is fine");
      g.step(50 * MB, 900 * MB); CHECK(!g.susp, "floor cleared -> resumes");
      g.step(120 * MB, 900 * MB); CHECK(g.susp, "budget trips while disk is fine");
      g.step(120 * MB, 100 * MB); CHECK(g.susp, "both bad stays suspended");
      g.step(50 * MB, 100 * MB);  CHECK(g.susp, "budget cleared but disk still full");
      g.step(50 * MB, 900 * MB);  CHECK(!g.susp, "both cleared -> resumes"); }

    // Unknown free space (QStorageInfo not ready) must not suspend.
    { FreeGuard g; g.minFree = 500 * MB;
      g.step(0, -1); CHECK(!g.susp, "unreadable free space does not suspend"); }

    // Both disabled: never suspends.
    { FreeGuard g; g.step(1000 * MB, 0); CHECK(!g.susp, "no limits -> never suspends"); }
}
