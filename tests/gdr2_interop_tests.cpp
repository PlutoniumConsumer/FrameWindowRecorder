#include <algorithm>
#include <iostream>
#include <gdr.hpp>
#include "../src/GdrCodec.hpp"
int main(int argc,char** argv){
    meter::Macro m;m.levelId=123;m.levelName="Interop";m.finishTick=1337;
    m.inputs={{0,true,true},{5,true,false},{17,false,true},{20,false,false},{600,true,true},{700,false,true}};
    auto out=meter::encodeGdr2(m);auto upstream=gdr::Replay<>::importData(out);
    if(upstream.isErr()){std::cerr<<upstream.unwrapErr();return 1;}
    auto const& u=upstream.unwrap();
    if(u.inputs.size()!=m.inputs.size()||u.framerate!=240||u.levelInfo.id!=123||u.gameVersion!=22081)return 2;
    for(std::size_t i=0;i<m.inputs.size();++i)if(u.inputs[i].frame!=m.inputs[i].tick||u.inputs[i].down!=m.inputs[i].down||u.inputs[i].player2==m.inputs[i].player1)return 3;
    gdr::Replay<> r("Reference",1);r.levelInfo=gdr::Level("Interop",123);r.duration=1337/240.f;r.gameVersion=22081;
    for(auto const& e:m.inputs)r.inputs.emplace_back(e.tick,1,!e.player1,e.down);
    auto data=r.exportData();if(data.isErr())return 4;
    auto imported=meter::decodeMacro(data.unwrap());
    if(imported.finishTick!=1337||imported.inputs.size()!=m.inputs.size())return 5;
    for(std::size_t i=0;i<m.inputs.size();++i)if(imported.inputs[i].tick!=m.inputs[i].tick||imported.inputs[i].down!=m.inputs[i].down||imported.inputs[i].player1!=m.inputs[i].player1)return 6;
    std::cout<<"Both directions passed against the official GDR2 writer/reader\n";
    if(argc==2){
        auto local=meter::readMacro(argv[1]);
        auto ref=gdr::Replay<>::importData(std::filesystem::path(argv[1]));if(ref.isErr())return 7;
        auto const& original=ref.unwrap();
        if(original.inputs.size()!=local.inputs.size())return 8;
        for(std::size_t i=0;i<local.inputs.size();++i){auto const& e=local.inputs[i];auto const& o=original.inputs[i];if(e.tick!=o.frame||e.down!=o.down||e.player1==o.player2)return 9;}
        auto exported=meter::encodeGdr2(local);auto reread=gdr::Replay<>::importData(exported);if(reread.isErr()||reread.unwrap().inputs.size()!=original.inputs.size())return 10;
        for(std::size_t i=0;i<original.inputs.size();++i){auto const& a=original.inputs[i];auto const& b=reread.unwrap().inputs[i];if(a.frame!=b.frame||a.down!=b.down||a.player2!=b.player2)return 11;}
        std::cout<<"Actual macro: "<<local.levelName<<", "<<local.inputs.size()<<" commands preserved exactly on import and export\n";
    }
}
