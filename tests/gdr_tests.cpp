#include "../src/GdrCodec.hpp"
#include <iostream>
#include <cstdlib>
void check(bool value,char const* reason){if(!value){std::cerr<<reason<<'\n';std::exit(1);}}
template<class F>void rejects(F action,char const* reason){try{action();}catch(std::exception const&){return;}check(false,reason);}
int main(){
    // Public GDR1 field names, created independently of the encoder under test.
    auto fixture=nlohmann::json::parse(R"({"version":1.0,"framerate":240,"duration":1.0,"gameVersion":2.2081,"author":"fixture","description":"schema fixture","seed":0,"coins":0,"ldm":false,"level":{"id":123,"name":"Example"},"bot":{"name":"FixtureBot","version":"1"},"inputs":[{"frame":2,"btn":1,"2p":false,"down":true},{"frame":3,"btn":1,"2p":true,"down":true},{"frame":8,"btn":1,"2p":false,"down":false},{"frame":9,"btn":1,"2p":true,"down":false}]})");
    auto m=meter::decodeMacro(nlohmann::json::to_msgpack(fixture));
    check(m.inputs.size()==4 && m.inputs[0].player1 && !m.inputs[1].player1,"Read canonical btn/2p keys correctly");
    auto bytes=meter::encodeMacro(m);auto encoded=nlohmann::json::from_msgpack(bytes);
    check(encoded["inputs"]==fixture["inputs"] && encoded["bot"]["name"]=="FixtureBot","Preserve supported imported events and metadata");
    auto m2=meter::decodeMacro(bytes);check(m2.levelId==123 && m2.finishTick==240,"Binary round trip");
    auto text=meter::encodeMacro(m,true);check(meter::decodeMacro(text).inputs.size()==4,"JSON GDR accepted");
    auto bad=fixture;bad["framerate"]=120;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject unsupported TPS");
    bad=fixture;bad["inputs"][0]["btn"]=2;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject unsupported buttons");
    bad=fixture;bad["inputs"][0]["frame"]=-1;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject negative frames");
    bad=fixture;bad["inputs"][0]["frame"]=4294967296ull;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject overflowing frames");
    bad=fixture;bad["inputs"][0]["down"]=1;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Require boolean flags");
    bad=fixture;bad["inputs"][0]["frameFix"]=true;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject unsupported input extensions");
    bad=fixture;bad["seed"]=44;
    rejects([&]{meter::decodeMacro(nlohmann::json::to_msgpack(bad));},"Reject seed restoration requirement");
    rejects([&]{meter::decodeMacro({'G','D','R',2});},"GDR2 rejected with explanation");
    bytes.pop_back();rejects([&]{meter::decodeMacro(bytes);},"Truncated MessagePack rejected");
    std::vector<std::uint8_t> deeplyNested(40,0x91);deeplyNested.push_back(0xc0);
    rejects([&]{meter::decodeMacro(deeplyNested);},"Excessive MessagePack nesting rejected");
    rejects([&]{meter::decodeMacro({0xdd,0x7f,0xff,0xff,0xff});},"Oversized container rejected without allocating it");
    m.inputs[2].tick=2;rejects([&]{meter::encodeMacro(m);},"Sub-tick/unordered transitions rejected");
    std::cout<<"GDR compatibility and rejection tests passed\n";
}
