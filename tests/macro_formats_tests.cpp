#include "../src/GdrCodec.hpp"
#include <iostream>
#include <cstdlib>
void check(bool ok,char const* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
template<class F>void rejects(F f){try{f();}catch(std::exception const&){return;}check(false,"Unsupported/malformed macro accepted");}
bool same(std::vector<meter::Input> const& a,std::vector<meter::Input> const& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto const& x,auto const& y){return x.tick==y.tick&&x.down==y.down&&x.player1==y.player1;});}
int main(){
    // Hand-authored wire fixture: GDR2, no extensions, 1 second, GD 22081,
    // IEEE754 big-endian duration/fps; two independently delta-encoded players.
    std::vector<std::uint8_t> fixture={'G','D','R',2,0,0,0,
      0x3f,0x80,0,0,0xc1,0xac,1,0x40,0x6e,0,0,0,0,0,0,
      0,0,0,0,1,'B',1,123,1,'L',0,0,4,2,5,12,7,12};
    auto m=meter::decodeMacro(fixture);
    check(m.levelId==123&&m.levelName=="L"&&m.finishTick==240,"GDR2 fixed metadata decoding");
    check(m.inputs.size()==4&&m.inputs[0].tick==2&&m.inputs[1].tick==3&&m.inputs[2].tick==8&&m.inputs[3].tick==9,"Sort per-player delta streams");
    check(m.inputs[0].player1&&!m.inputs[1].player1&&!m.inputs[2].down,"Player/press bits");
    auto encoded=meter::encodeGdr2(m);auto again=meter::decodeMacro(encoded);
    check(same(again.inputs,m.inputs)&&again.finishTick==m.finishTick,"GDR2 round trip");
    auto commands=m;
    commands.inputs={{1,false,true},{2,true,true},{3,true,true},{4,false,true},{4,false,true},{5,false,false}};
    auto preserved=meter::decodeMacro(meter::encodeGdr2(commands));
    check(same(preserved.inputs,commands.inputs),"Retain initial releases, repeated presses and same-tick identical commands");
    check(same(meter::decodeMacro(meter::encodeMacro(commands)).inputs,commands.inputs),"GDR1 preserves repeated commands too");
    check(same(meter::decodeMacro(meter::encodePlainText(commands)).inputs,commands.inputs),"Text preserves repeated commands too");
    for(auto tick:{9u,17u,1337u,287999u}){auto timed=m;timed.finishTick=tick;check(meter::decodeMacro(meter::encodeGdr2(timed)).finishTick==tick,"GDR2 float duration preserves nearest physics tick");}
    // Every byte boundary must reject an incomplete file, not silently shorten it.
    for(std::size_t n=0;n<fixture.size();++n){auto truncated=std::vector<std::uint8_t>(fixture.begin(),fixture.begin()+n);rejects([&]{meter::decodeMacro(truncated);});}
    auto bad=fixture;bad.push_back(0);rejects([&]{meter::decodeMacro(bad);});
    bad=fixture;bad[3]=3;rejects([&]{meter::decodeMacro(bad);});
    bad=fixture;bad[22]=1;rejects([&]{meter::decodeMacro(bad);}); // seed
    bad=fixture;bad[24]=2;rejects([&]{meter::decodeMacro(bad);}); // invalid bool
    bad=fixture;bad[32]=1;rejects([&]{meter::decodeMacro(bad);}); // replay extension
    bad=fixture;bad[33]=1;rejects([&]{meter::decodeMacro(bad);}); // death timeline
    bad=fixture;bad[35]=5;rejects([&]{meter::decodeMacro(bad);}); // p1 > total
    bad={'G','D','R'};bad.insert(bad.end(),11,0xff);rejects([&]{meter::decodeMacro(bad);});
    bad=fixture;bad[7]=0x7f;bad[8]=0xc0;rejects([&]{meter::decodeMacro(bad);}); // NaN duration
    bad=fixture;bad[15]=0x5e;rejects([&]{meter::decodeMacro(bad);}); // non-240 FPS
    // Empty extension payloads with a named input tag are valid; nonempty aren't.
    bad=fixture;bad[4]=1;bad.insert(bad.begin()+5,'X');
    for(int i=0;i<4;++i)bad.insert(bad.begin()+38+i*2,0);
    check(same(meter::decodeMacro(bad).inputs,m.inputs),"Named zero-length input extensions");
    bad.back()=1;rejects([&]{meter::decodeMacro(bad);});
    std::string text="240\r\n2 1 0\r\n3 1 1\r\n8 0 0\r\n9 0 1\r\n";
    auto plain=meter::decodeMacro({text.begin(),text.end()});
    check(same(plain.inputs,m.inputs)&&plain.requiresLevelBinding,"Plaintext flags and required level binding");
    rejects([&]{meter::encodeGdr2(plain);});rejects([&]{meter::encodeMacro(plain);});
    check(same(meter::decodeMacro(meter::encodePlainText(m)).inputs,m.inputs),"Plaintext export round trip");
    for(auto s:{"120\n1 1 0\n","240\n-1 1 0\n","240\n1 2 0\n","240\n1 1 0 junk\n","240\n1 1 0\n1 0 0\n"})rejects([&]{std::string v=s;meter::decodeMacro({v.begin(),v.end()});});
    auto doc=nlohmann::json::parse(R"({"_":"fixture","meta":{"fps":240},"events":[{"frame":2,"down":true},{"frame":8}]})");
    auto legacy=meter::decodeMhrJson(doc);check(legacy.requiresLevelBinding&&legacy.inputs.size()==2&&!legacy.inputs[1].down,"Basic MHR JSON optional false flags");
    auto str=doc.dump();check(meter::decodeMacro({str.begin(),str.end()}).requiresLevelBinding,"JSON format detection");
    doc["events"][0]["x"]=12;rejects([&]{meter::decodeMhrJson(doc);});
    doc["events"][0].erase("x");doc["events"][0]["down"]=1;rejects([&]{meter::decodeMhrJson(doc);});
    std::cout<<"GDR2, legacy adapters, truncation and unsupported feature tests passed\n";
}
