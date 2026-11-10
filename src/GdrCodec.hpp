#pragma once
#include "Analysis.hpp"
#include "../vendor/json.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace meter {
struct Macro {
    std::uint32_t levelId=0, finishTick=0;
    std::string levelName;
    std::vector<Input> inputs;
    nlohmann::json document; // Preserve metadata/extensions on a lossless supported import/export.
    bool requiresLevelBinding=false; // Legacy formats have no trustworthy level identity.
};
inline void validateMacro(Macro const& m) {
    if(m.inputs.empty() || m.inputs.size()>10000) throw std::runtime_error("Macro must contain 1 to 10,000 inputs.");
    bool held[2]={false,false}; std::int64_t last[2]={-1,-1};
    std::uint32_t previous=0;
    for(auto const& e:m.inputs) {
        int p=e.player1 ? 0 : 1;
        if(e.tick>240*60*20 || e.tick<previous) throw std::runtime_error("Inputs must be ordered and within 20 minutes.");
        // Replay files store button commands, not just changes in held state.
        // Initial releases and repeated commands are valid and must be retained.
        // Opposing commands on one player's same tick remain unsupported.
        if(last[p]==e.tick && held[p]!=e.down) throw std::runtime_error("Opposing same-player commands on the same tick are unsupported.");
        previous=e.tick; last[p]=e.tick; held[p]=e.down;
    }
    if(m.finishTick>240*60*20 || m.finishTick<m.inputs.back().tick) throw std::runtime_error("Invalid macro duration.");
}
}
#include "MacroFormats.hpp"
namespace meter {
inline Macro decodeMacro(std::vector<std::uint8_t> const& bytes) {
    using J=nlohmann::json;
    if(bytes.empty() || bytes.size()>16*1024*1024) throw std::runtime_error("Macro exceeds the 16 MB file limit or is empty.");
    if(bytes.size()>=3 && bytes[0]=='G' && bytes[1]=='D' && bytes[2]=='R') return decodeGdr2(bytes);
    // Reject excessive nesting before constructing the document, including MessagePack.
    int depth=0;
    auto saxDepth=[&](int d,J::parse_event_t event,J&) {
        if(d>32) throw std::runtime_error("Macro metadata nesting exceeds 32 levels.");
        return true;
    };
    J doc;
    auto first=std::find_if(bytes.begin(),bytes.end(),[](auto c){return c!=' '&&c!='\r'&&c!='\n'&&c!='\t';});
    if(first!=bytes.end() && *first=='{') doc=J::parse(bytes,saxDepth);
    else if(first!=bytes.end() && *first>='0' && *first<='9') return decodePlainText(bytes);
    else {
        // SAX validation avoids unbounded recursion/allocation from hostile binary containers.
        struct Guard : nlohmann::json_sax<J> {
            int depth=0; std::size_t nodes=0;
            bool count(){return ++nodes<250000;}
            bool null() override{return count();} bool boolean(bool) override{return count();}
            bool number_integer(number_integer_t) override{return count();} bool number_unsigned(number_unsigned_t) override{return count();}
            bool number_float(number_float_t,string_t const&) override{return count();}
            bool string(string_t& s) override{return s.size()<1024*1024 && count();}
            bool binary(binary_t&) override{return false;}
            bool start_object(std::size_t n) override{return n<=50000 && ++depth<=32 && count();}
            bool key(string_t& s) override{return s.size()<4096 && count();}
            bool end_object() override{--depth;return true;}
            bool start_array(std::size_t n) override{return n<=50000 && ++depth<=32 && count();}
            bool end_array() override{--depth;return true;}
            bool parse_error(std::size_t,std::string const&,nlohmann::detail::exception const&) override{return false;}
        } guard;
        if(!J::sax_parse(bytes,&guard,J::input_format_t::msgpack)) throw std::runtime_error("Invalid, oversized or unsupported MessagePack macro.");
        doc=J::from_msgpack(bytes);
    }
    if(doc.is_object() && doc.contains("events") && doc.contains("meta")) return decodeMhrJson(doc);
    if(!doc.is_object() || !doc.contains("inputs") || !doc["inputs"].is_array()) throw std::runtime_error("Unsupported macro schema. Use GDR1, GDR2, basic Mega Hack JSON or plaintext.");
    if(doc.value("version",0.)!=1.) throw std::runtime_error("Only GDR version 1 is supported.");
    auto fps=doc.value("framerate",240.);
    if(!std::isfinite(fps) || std::abs(fps-240.)>.00001) throw std::runtime_error("Only 240 TPS macros can be replayed/analyzed.");
    if(doc.value("platformer",false)) throw std::runtime_error("Platformer macros are unsupported.");
    if(doc.value("seed",0)!=0) throw std::runtime_error("This macro requires a random seed that this mod cannot restore.");
    if(doc.value("ldm",false)) throw std::runtime_error("LDM macros require their original environment; export a normal-detail macro.");
    if(doc["inputs"].size()>10000) throw std::runtime_error("Macro exceeds 10,000 inputs.");
    Macro m; m.document=doc;
    auto const& level=doc.at("level");
    auto id=level.at("id");
    if(!id.is_number_integer() || id.get<double>()<0 || id.get<double>()>4294967295.) throw std::runtime_error("Invalid level ID.");
    m.levelId=id.get<std::uint32_t>(); m.levelName=level.value("name",std::string{});
    if(m.levelName.size()>512) throw std::runtime_error("Level name is too long.");
    for(auto const& j:doc["inputs"]) {
        if(!j.is_object() || !j.contains("frame") || !j["frame"].is_number_integer()
            || j["frame"].get<double>()<0 || j["frame"].get<double>()>240*60*20) throw std::runtime_error("Invalid input frame.");
        if(!j.contains("btn") || !j["btn"].is_number_integer() || j["btn"].get<int>()!=1)
            throw std::runtime_error("Only jump inputs (btn=1) are supported.");
        if(!j.contains("2p") || !j["2p"].is_boolean() || !j.contains("down") || !j["down"].is_boolean())
            throw std::runtime_error("Invalid player/press flags.");
        // Bot-specific frame corrections cannot be applied by a plain tick replay.
        for(auto it=j.begin();it!=j.end();++it)
            if(it.key()!="frame" && it.key()!="btn" && it.key()!="2p" && it.key()!="down")
                throw std::runtime_error("Input extensions/frame fixes are unsupported; export plain GDR inputs.");
        m.inputs.push_back({j["frame"].get<std::uint32_t>(),j["down"].get<bool>(),!j["2p"].get<bool>()});
    }
    double duration=doc.value("duration",0.);
    if(!std::isfinite(duration) || duration<0 || duration>1200.) throw std::runtime_error("Invalid macro duration.");
    m.finishTick=std::max(m.inputs.empty()?0u:m.inputs.back().tick,static_cast<std::uint32_t>(std::ceil(duration*240.)));
    validateMacro(m); return m;
}
inline std::vector<std::uint8_t> encodeMacro(Macro const& m,bool text=false) {
    validateMacro(m);
    if(m.requiresLevelBinding) throw std::runtime_error("Attach this macro to a level before exporting GDR.");
    auto doc=m.document.is_object()?m.document:nlohmann::json::object();
    doc["version"]=1.; doc["framerate"]=240.; doc["duration"]=m.finishTick/240.;
    doc["gameVersion"]=2.2081; doc["level"]={{"id",m.levelId},{"name",m.levelName}};
    if(!doc.contains("author")) doc["author"]="";
    if(!doc.contains("description")) doc["description"]="Frame Window Recorder recording";
    if(!doc.contains("bot")) doc["bot"]={{"name","Frame Window Recorder"},{"version","1.0.0"}};
    doc["seed"]=0; doc["coins"]=0; doc["ldm"]=false;
    doc["inputs"]=nlohmann::json::array();
    for(auto const& e:m.inputs) doc["inputs"].push_back({{"frame",e.tick},{"btn",1},{"2p",!e.player1},{"down",e.down}});
    if(text) {auto str=doc.dump(2); return {str.begin(),str.end()};}
    return nlohmann::json::to_msgpack(doc);
}
inline Macro readMacro(std::filesystem::path const& path) {
    auto size=std::filesystem::file_size(path);
    if(size>16*1024*1024) throw std::runtime_error("Macro exceeds 16 MB.");
    std::ifstream f(path,std::ios::binary); std::vector<std::uint8_t> b(static_cast<std::size_t>(size));
    if(!f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()))) throw std::runtime_error("Could not read macro.");
    return decodeMacro(b);
}
inline void writeMacro(std::filesystem::path const& path,Macro const& m) {
    auto ext=path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    auto b=ext==".gdr2"?encodeGdr2(m):ext==".txt"?encodePlainText(m):encodeMacro(m,ext==".json");
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if(!f.write(reinterpret_cast<char const*>(b.data()),static_cast<std::streamsize>(b.size()))) throw std::runtime_error("Could not write macro.");
}
}
