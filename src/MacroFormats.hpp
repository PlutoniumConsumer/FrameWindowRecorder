#pragma once
// Included by GdrCodec.hpp after Macro and validateMacro. Independently written
// adapters; wire-format research and intentional restrictions: MACRO_FORMATS.md.
#include <bit>
#include <sstream>
#include <cctype>
namespace meter {
namespace wire {
struct Reader {
    std::vector<std::uint8_t> const& bytes; std::size_t at=3;
    std::uint8_t byte(){if(at>=bytes.size())throw std::runtime_error("Truncated GDR2 file.");return bytes[at++];}
    std::uint64_t integer(){
        std::uint64_t v=0;
        for(unsigned n=0;n<10;++n){auto b=byte();if(n==9 && (b&0xfe))throw std::runtime_error("GDR2 integer overflow.");v|=std::uint64_t(b&127)<<(7*n);if(!(b&128))return v;}
        throw std::runtime_error("Invalid GDR2 integer.");
    }
    std::uint64_t bounded(std::uint64_t max){auto v=integer();if(v>max)throw std::runtime_error("GDR2 value exceeds supported limits.");return v;}
    std::string string(std::size_t max=65535){auto n=bounded(max);if(n>bytes.size()-at)throw std::runtime_error("Truncated GDR2 string.");std::string s(bytes.begin()+at,bytes.begin()+at+n);at+=n;return s;}
    bool flag(){auto b=byte();if(b>1)throw std::runtime_error("Invalid GDR2 boolean.");return b!=0;}
    template<class T>T real(){using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;U v=0;for(unsigned i=0;i<sizeof(T);++i)v=(v<<8)|byte();return std::bit_cast<T>(v);}
};
struct Writer {
    std::vector<std::uint8_t> bytes={'G','D','R'};
    void integer(std::uint64_t v){do{auto b=static_cast<std::uint8_t>(v&127);v>>=7;bytes.push_back(b|(v?128:0));}while(v);}
    void string(std::string const& s){if(s.size()>65535)throw std::runtime_error("GDR2 string too long.");integer(s.size());bytes.insert(bytes.end(),s.begin(),s.end());}
    template<class T>void real(T v){using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;auto bits=std::bit_cast<U>(v);for(int i=sizeof(T)-1;i>=0;--i)bytes.push_back(static_cast<std::uint8_t>(bits>>(8*i)));}
};
inline void rate(double fps){if(!std::isfinite(fps)||std::abs(fps-240.)>.00001)throw std::runtime_error("Only fixed 240 TPS macros are supported; timing is never rescaled.");}
inline void duration(Macro& m,double seconds){if(!std::isfinite(seconds)||seconds<0||seconds>1200)throw std::runtime_error("Invalid macro duration.");m.finishTick=std::max(m.inputs.empty()?0u:m.inputs.back().tick,static_cast<std::uint32_t>(std::round(seconds*240.)));}
}
inline Macro decodeGdr2(std::vector<std::uint8_t> const& bytes){
    wire::Reader r{bytes};
    if(r.integer()!=2)throw std::runtime_error("Unsupported GDR binary version; expected GDR2.");
    auto tag=r.string(512);auto author=r.string();auto description=r.string();
    auto seconds=r.real<float>();auto game=r.bounded(1000000);wire::rate(r.real<double>());
    auto seed=r.integer();auto coins=r.bounded(3);auto ldm=r.flag();auto platformer=r.flag();
    if(seed||ldm||platformer)throw std::runtime_error("GDR2 requires unsupported seed restoration, LDM or platformer gameplay.");
    auto bot=r.string();auto botVersion=r.bounded(2147483647);
    Macro m;m.levelId=static_cast<std::uint32_t>(r.bounded(0xffffffff));m.levelName=r.string(512);
    if(r.integer()!=0)throw std::runtime_error("GDR2 replay extensions are unsupported; export a plain input macro.");
    if(r.integer()!=0)throw std::runtime_error("GDR2 death/respawn timelines are unsupported; use a single completed attempt.");
    auto count=r.bounded(10000);auto p1=r.bounded(count);std::uint64_t tick=0;
    for(std::uint64_t i=0;i<count;++i){
        if(i==p1)tick=0;
        auto packed=r.integer();auto delta=packed>>1;
        if(delta>288000 || tick>288000-delta)throw std::runtime_error("GDR2 input frame exceeds 20 minutes.");
        tick+=delta;m.inputs.push_back({static_cast<std::uint32_t>(tick),bool(packed&1),i<p1});
        if(!tag.empty() && r.integer()!=0)throw std::runtime_error("GDR2 input extensions/frame corrections are unsupported; export plain inputs.");
    }
    if(r.at!=bytes.size())throw std::runtime_error("GDR2 input count mismatch or trailing data.");
    std::stable_sort(m.inputs.begin(),m.inputs.end(),[](auto const& a,auto const& b){return a.tick<b.tick;});
    m.document={{"author",author},{"description",description},{"bot",{{"name",bot},{"version",std::to_string(botVersion)}}},{"gameVersion",game/10000.},{"coins",coins}};
    wire::duration(m,seconds);validateMacro(m);return m;
}
inline std::vector<std::uint8_t> encodeGdr2(Macro const& m){
    validateMacro(m);if(m.requiresLevelBinding)throw std::runtime_error("Attach this macro to a level before exporting GDR2.");
    wire::Writer w;w.integer(2);w.string("");
    auto const& d=m.document;w.string(d.is_object()?d.value("author",std::string{}):"");
    w.string(d.is_object()?d.value("description",std::string("Frame Window Recorder recording")):"Frame Window Recorder recording");
    w.real(float(m.finishTick/240.));w.integer(22081);w.real(240.);w.integer(0);w.integer(0);w.integer(0);w.integer(0);
    // Exporter identity describes this normalized input-only file, not the source bot.
    w.string("Frame Window Recorder");w.integer(1);w.integer(m.levelId);w.string(m.levelName);
    w.integer(0);w.integer(0);w.integer(m.inputs.size());
    w.integer(std::count_if(m.inputs.begin(),m.inputs.end(),[](auto const& e){return e.player1;}));
    for(bool player1:{true,false}){std::uint32_t prev=0;for(auto const& e:m.inputs)if(e.player1==player1){w.integer((std::uint64_t(e.tick-prev)<<1)|static_cast<std::uint64_t>(e.down));prev=e.tick;}}
    return std::move(w.bytes);
}
inline Macro decodePlainText(std::vector<std::uint8_t> const& bytes){
    std::istringstream stream(std::string(bytes.begin(),bytes.end()));std::string line;
    std::getline(stream,line);std::istringstream header(line);double fps;std::string extra;
    if(!(header>>fps)||(header>>extra))throw std::runtime_error("Plaintext must begin with a single TPS value.");wire::rate(fps);
    Macro m;m.requiresLevelBinding=true;
    while(std::getline(stream,line)){
        if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
        std::istringstream row(line);std::int64_t frame;int down,p2;
        if(!(row>>frame>>down>>p2)||(row>>extra)||frame<0||frame>288000||(down!=0&&down!=1)||(p2!=0&&p2!=1))throw std::runtime_error("Plaintext rows must be: frame down player2 (flags 0 or 1).");
        m.inputs.push_back({static_cast<std::uint32_t>(frame),bool(down),!bool(p2)});
        if(m.inputs.size()>10000)throw std::runtime_error("Macro exceeds 10,000 inputs.");
    }
    m.finishTick=m.inputs.empty()?0:m.inputs.back().tick;validateMacro(m);return m;
}
inline std::vector<std::uint8_t> encodePlainText(Macro const& m){validateMacro(m);std::ostringstream out;out<<"240\n";for(auto const& e:m.inputs)out<<e.tick<<' '<<e.down<<' '<<!e.player1<<'\n';auto s=out.str();return {s.begin(),s.end()};}
inline Macro decodeMhrJson(nlohmann::json const& doc){
    if(!doc.at("events").is_array() || doc.at("events").size()>10000)throw std::runtime_error("Invalid Mega Hack event array.");
    for(auto it=doc.begin();it!=doc.end();++it)if(it.key()!="_"&&it.key()!="events"&&it.key()!="meta")throw std::runtime_error("Unsupported Mega Hack metadata extension.");
    auto const& meta=doc.at("meta");if(!meta.is_object()||meta.size()!=1||!meta.contains("fps"))throw std::runtime_error("Only basic Mega Hack meta.fps is supported.");wire::rate(meta.at("fps").get<double>());
    Macro m;m.requiresLevelBinding=true;
    for(auto const& e:doc.at("events")){
        if(!e.is_object())throw std::runtime_error("Invalid Mega Hack input.");
        for(auto it=e.begin();it!=e.end();++it)if(it.key()!="frame"&&it.key()!="down"&&it.key()!="p2")throw std::runtime_error("Mega Hack position/rotation corrections are unsupported; export plain inputs.");
        auto const& frame=e.at("frame");if(!frame.is_number_integer()||frame.get<double>()<0||frame.get<double>()>288000)throw std::runtime_error("Invalid Mega Hack frame.");
        bool down=e.value("down",false),p2=e.value("p2",false);
        m.inputs.push_back({frame.get<std::uint32_t>(),down,!p2});
    }
    m.finishTick=m.inputs.empty()?0:m.inputs.back().tick;validateMacro(m);return m;
}
}
