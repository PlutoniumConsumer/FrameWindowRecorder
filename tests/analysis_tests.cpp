#include "../src/Analysis.hpp"
#include <cstdlib>
#include <iostream>
void require(bool value, char const* message) { if(!value) { std::cerr<<message<<'\n'; std::exit(1); } }
int main() {
    for(int early=0;early<=11;++early) for(int late=0;late<=11;++late) {
        meter::WindowSearch w(-12,12);
        int trials=0;
        while(auto d=w.candidate()) { require(++trials<=24,"Bounded search"); w.outcome(*d>=-early && *d<=late); }
        auto r=w.result();
        if(early+late+1>12) require(r.wide && r.row()==7,"Wider than twelve must never be labelled exact");
        else require(r.exact && r.early==early && r.late==late,"Find both gameplay failure boundaries");
    }
    meter::WindowSearch all(-12,12);
    while(auto d=all.candidate()) all.outcome(true);
    require(all.result().wide && !all.result().exact,"All-success search is lower-bounded");
    meter::WindowSearch constrained(0,0);
    require(!constrained.candidate() && !constrained.result().exact && constrained.result().row()==8,"Input chronology is not a failed gameplay boundary");
    meter::WindowSearch hole(-12,12);
    while(auto d=hole.candidate()) hole.outcome(*d!=-1 && *d!=1);
    require(hole.result().exact && hole.result().width()==1,"Use contiguous window, not disconnected successes");
    std::vector<meter::Input> e={{5,true,true},{8,true,false},{10,false,true},{11,false,false},{20,true,true}};
    auto [low,high]=meter::shiftLimits(e,0);
    require(low==-5 && high==4,"Preserve release order");
    auto shifted=meter::shifted(e,0,4);
    require(shifted[0].tick==8 && !shifted[0].player1 && shifted[1].tick==9,"Moving one player's input preserves other player ordering");
    constexpr int widths[]={9,7,5,4,3,2,1};
    for(int row=0;row<7;++row) { meter::Window w; w.exact=true; w.late=widths[row]-1; require(w.row()==row,"Disjoint HUD buckets"); }
    std::cout<<"Timing-window tests passed\n";
}
