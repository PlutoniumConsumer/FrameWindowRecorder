#include "../src/ReplayLifecycle.hpp"
#include <stdexcept>
#include <iostream>
void check(bool ok){if(!ok)throw std::runtime_error("Replay lifecycle regression");}
int main(){
    meter::ReplayLifecycle gate;
    check(gate.gameplayDeath(false,true));
    check(!gate.gameplayDeath(true,true)); // Native synthetic probe, including tick 0.
    check(!gate.gameplayDeath(false,false)); // Hidden/inactive second player.
    for(int trial=0;trial<100;++trial){
        try{
            meter::ReplayLifecycle::ResetScope outer(gate);
            check(gate.resetting()&&!gate.gameplayDeath(false,true));
            {meter::ReplayLifecycle::ResetScope nested(gate);check(gate.resetting()&&!gate.gameplayDeath(true,true));}
            check(gate.resetting());
            throw trial;
        }catch(int){}
        check(!gate.resetting()&&gate.gameplayDeath(false,true)); // Don't mask a real collision after reset.
    }
    std::cout<<"Replay reset/probe/real-death lifecycle checks passed\n";
}
