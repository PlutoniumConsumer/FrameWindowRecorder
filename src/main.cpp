#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/async.hpp>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include "Analysis.hpp"
#include "ReplayLifecycle.hpp"
#include "PhysicsClock.hpp"
#include "GdrCodec.hpp"
#include "VideoCapture.hpp"
#include "FrameUi.hpp"
#include "Showcase.hpp"
#include "EncoderDiscovery.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <chrono>
using namespace geode::prelude;

namespace {
constexpr std::uint32_t MaxTicks = 240 * 60 * 20;
constexpr std::size_t MaxInputs = 10000;
constexpr std::array<char const*, 9> Rows = {"9-12", "7-8", "5-6", "4", "3", "2", "1", ">12", "?"};
constexpr std::array<ccColor3B, 9> Colors = {{
    {133,115,255}, {129,200,255}, {137,247,145}, {255,255,255},
    {255,246,115}, {255,180,86}, {255,85,96}, {205,150,245}, {170,170,180}
}};
struct Sample {
    std::uint32_t tick;
    double time;
    float x, y, x2, y2;
    double vy, vy2;
    bool dual;
};
void service();
void showReplayResults(PlayLayer* owner);
enum class Stage { Idle, Record, Baseline, Search, Confirm, Playback, Finished, Aborted };
meter::VideoCapture video;
struct Session {
    PlayLayer* owner = nullptr;
    Stage stage = Stage::Idle;
    bool recording=false, hudEnabled=true, introEnabled=true, learning=false, analyzed=false;
    bool videoStarting=false, videoPlaying=false, videoArming=false, audioPaused=false, ending=false;
    std::chrono::steady_clock::time_point endingAt{};
    std::chrono::steady_clock::time_point videoReady{};
    std::array<unsigned,9> playbackCounts{};
    meter::Macro macro;
    std::vector<Sample> learningTrace;
    bool intro = false, introShown = false, bypassIntro = false;
    bool inject = false, internalReset = false, queued = false, freeze = false;
    bool valid = true, completed = false, pendingAuto = false, seenTick = false;
    bool held[2] = {false, false};
    std::uint32_t lastTick = 0, finishTick = 0;
    std::size_t cursor = 0, click = 0;
    int offset = 0;
    std::optional<bool> outcome;
    std::optional<std::uint32_t> endGateTick;
    meter::ReplayLifecycle lifecycle;
    meter::PhysicsClock clock;
    std::optional<meter::WindowSearch> search;
    std::vector<meter::Input> attempt, saved, plan;
    std::vector<Sample> trace, savedTrace;
    std::vector<std::size_t> presses;
    std::vector<meter::Window> windows;
    std::string status = "Off - open Frame Meter in pause";
    std::string reason;
    bool replaying() const { return stage == Stage::Baseline || stage == Stage::Search || stage == Stage::Confirm || stage==Stage::Playback; }
} s;
bool owns(GJBaseGameLayer* layer) { return static_cast<GJBaseGameLayer*>(s.owner)==layer; }

void invalid(std::string const& why) {
    log::warn("Frame Window Recorder: {}",why);
    s.valid = false;
    s.reason = why;
    s.status = why;
    if (s.replaying()) {
        if(s.videoPlaying) video.stop();
        s.stage = Stage::Aborted; s.freeze = true; s.pendingAuto = false;
        s.windows.clear(); s.analyzed=false; s.outcome.reset();
    }
}
Sample sample(PlayLayer* p) {
    auto a = p->m_player1; auto b = p->m_player2;
    return {s.clock.tick(), s.clock.tick()/240.,
        a->getPositionX(), a->getPositionY(), b ? b->getPositionX() : 0.f,
        b ? b->getPositionY() : 0.f, a->m_yVelocity, b ? b->m_yVelocity : 0.,
        p->m_gameState.m_isDualMode};
}
bool same(Sample const& a, Sample const& b) {
    return std::abs(a.x-b.x) < .05f && std::abs(a.y-b.y) < .05f
        && std::abs(a.vy-b.vy) < .05 && a.dual == b.dual
        && (!a.dual || (std::abs(a.x2-b.x2) < .05f && std::abs(a.y2-b.y2) < .05f
            && std::abs(a.vy2-b.vy2) < .05));
}
std::filesystem::path reportPath(char const* suffix) {
    return Mod::get()->getSaveDir() / ("level-" + std::to_string(s.owner->m_level->m_levelID.value()) + suffix);
}
void saveRecording() {
    std::ofstream f(reportPath("-recording.csv"), std::ios::trunc);
    f << "tick,down,player\n";
    for (auto const& e : s.saved) f << e.tick << ',' << e.down << ',' << (e.player1 ? 1 : 2) << '\n';
    std::ofstream t(reportPath("-trace.csv"), std::ios::trunc);
    t << "tick,game_seconds,x,y,vy,dual,x2,y2,vy2\n" << std::setprecision(12);
    for (auto const& a : s.savedTrace)
        t << a.tick << ',' << a.time << ',' << a.x << ',' << a.y << ',' << a.vy << ','
          << a.dual << ',' << a.x2 << ',' << a.y2 << ',' << a.vy2 << '\n';
    if (!f || !t) log::warn("Frame Meter: could not export recording/trace");
}
void saveResults() {
    std::ofstream f(reportPath("-windows.csv"), std::ios::trunc);
    f << "click,tick,player,early_frames,late_frames,minimum_width,exact,category\n";
    for (std::size_t i=0; i<s.windows.size(); ++i) {
        auto const& w=s.windows[i]; auto const& e=s.saved[s.presses[i]];
        f << i+1 << ',' << e.tick << ',' << (e.player1 ? 1 : 2) << ',' << w.early << ','
          << w.late << ',' << w.width() << ',' << w.exact << ',' << Rows[w.row()] << '\n';
    }
    if (!f) log::warn("Frame Meter: could not export timing-window report");
}
void beforeTick(PlayLayer* p, float, bool half) {
    if(!s.recording && !s.replaying()) return;
    if (s.owner != p || s.lifecycle.resetting() || s.intro || s.freeze || s.outcome || !p->m_started
        || p->m_isPaused || p->m_hasCompletedLevel || !p->m_player1 || p->m_player1->m_isDead) return;
    if (s.completed && !s.replaying()) return;
    if (p->m_isPracticeMode || p->m_isPlatformer || p->m_startPosObject || p->m_isTestMode) {
        invalid("Full normal-mode attempts only"); return;
    }
    if (p->m_clickBetweenSteps || p->m_clickOnSteps || half) {
        invalid("Disable sub-tick input modes"); return;
    }
    auto a=sample(p);
    if (a.tick > MaxTicks) { invalid("20-minute recording limit"); return; }
    if (s.seenTick && a.tick == s.lastTick) return;
    if (s.seenTick && a.tick != s.lastTick+1) { invalid("Non-sequential physics ticks"); return; }
    s.seenTick=true; s.lastTick=a.tick;
    if (!s.replaying()) {
        if (!s.valid) return;
        if (!s.trace.empty() && std::abs((a.time-s.trace.back().time)-1./240.) > .00001) {
            invalid("Requires vanilla 240 TPS clock"); return;
        }
        s.trace.push_back(a);
        return;
    }
    if(s.learning && s.stage==Stage::Baseline) {
        if(!s.learningTrace.empty() && std::abs(a.time-s.learningTrace.back().time-1./240.)>.00001) {
            invalid("Imported replay is not running at 240 TPS"); return;
        }
        s.learningTrace.push_back(a);
    }
    auto index=s.savedTrace.empty() || a.tick<s.savedTrace.front().tick ? s.savedTrace.size() : static_cast<std::size_t>(a.tick-s.savedTrace.front().tick);
    bool compare=!s.savedTrace.empty() && !s.learning && s.stage!=Stage::Search;
    if (s.stage==Stage::Search) {
        auto tick=s.saved[s.presses[s.click]].tick;
        auto moved=static_cast<std::uint32_t>(static_cast<int>(tick)+s.offset);
        compare=a.tick<std::min(tick,moved);
    }
    if (compare && (index>=s.savedTrace.size() || !same(a,s.savedTrace[index]))) {
        invalid("Replay diverged - no result"); return;
    }
    if (!s.savedTrace.empty() && index>0 && index<s.savedTrace.size()) {
        auto elapsed=a.time-s.savedTrace.front().time;
        auto expected=static_cast<double>(a.tick-s.savedTrace.front().tick)/240.;
        if (std::abs(elapsed-expected)>.0001) { invalid("Replay clock is not 240 TPS"); return; }
    }
    if (a.tick>s.finishTick+480) { s.reason="Replay timed out at tick "+std::to_string(a.tick);s.outcome=false; return; }
    if (s.cursor<s.plan.size() && s.plan[s.cursor].tick<a.tick) {
        invalid("Replay missed an input tick"); return;
    }
    while (s.cursor<s.plan.size() && s.plan[s.cursor].tick==a.tick) {
        auto inputIndex=s.cursor;
        auto const& e=s.plan[s.cursor++];
        if(s.stage==Stage::Playback && e.down) {
            auto it=std::find(s.presses.begin(),s.presses.end(),inputIndex);
            auto n=static_cast<std::size_t>(it-s.presses.begin());
            ++s.playbackCounts[n<s.windows.size()?s.windows[n].row():8];
            showcase::marker(p,e.player1,n<s.windows.size()?&s.windows[n]:nullptr);
            if(n<s.windows.size())showcase::ding();
        }
        s.inject=true; p->handleButton(e.down,1,e.player1); s.inject=false;
    }
}
}

class $modify(OpenWindowBase, GJBaseGameLayer) {
    void update(float dt) {
        if (owns(this) && (s.intro || s.freeze || s.outcome)) { service(); return; }
        GJBaseGameLayer::update(dt);
    }
    void processCommands(float dt, bool half, bool last) {
        bool tracked=owns(this) && !s.lifecycle.resetting() && (s.recording||s.replaying())
            && m_started && !s.owner->m_isPaused && !s.freeze && !s.intro && !s.outcome
            && m_player1 && !m_player1->m_isDead && !s.owner->m_hasCompletedLevel;
        if(tracked && !half && !meter::PhysicsClock::supportedStep(dt)) {
            invalid("Physics step is not 1/240 second");return;
        }
        if (owns(this) && !s.lifecycle.resetting()) {
            beforeTick(s.owner,dt,half);
            if (s.freeze || s.outcome || s.intro) return;
        }
        GJBaseGameLayer::processCommands(dt,half,last);
        if(tracked && !half && owns(this) && !s.lifecycle.resetting() && !s.freeze)s.clock.advance();
    }
    void handleButton(bool down, int button, bool player1) {
        if (owns(this) && !s.inject) {
            if (s.replaying() || s.freeze || s.intro) return;
            if (s.recording && !s.completed && s.valid && button==1 && s.owner->m_started
                && !s.owner->m_isPaused && !s.owner->m_player1->m_isDead) {
                int player=player1 ? 0 : 1;
                if (s.held[player]!=down) {
                    auto tick=s.clock.tick();
                    for (auto it=s.attempt.rbegin(); it!=s.attempt.rend(); ++it) {
                        if (it->player1==player1) {
                            if (it->tick==tick) invalid("Sub-tick transition: recording invalid");
                            break;
                        }
                    }
                    if (s.attempt.size()>=MaxInputs) invalid("10,000-input recording limit");
                    else if (s.valid) s.attempt.push_back({tick,down,player1});
                    s.held[player]=down;
                }
            }
        }
        GJBaseGameLayer::handleButton(down,button,player1);
    }
};

class $modify(OpenFrameMeterLayer, PlayLayer) {
public:
    struct Fields {
        std::array<CCLabelBMFont*,9> rows{};
        CCLabelBMFont* status=nullptr;
        CCMenu* controls=nullptr;
        CCLabelBMFont* controlLabel=nullptr;
        CCLayerColor* progress=nullptr;
        CCLabelBMFont* progressText=nullptr;
        CCDrawNode* progressBar=nullptr;
        CCDrawNode* hudPanel=nullptr;
    };
    bool init(GJGameLevel* level, bool replay, bool dontCreate) {
        s=Session{}; s.owner=this;
        s.hudEnabled=Mod::get()->getSettingValue<bool>("show-hud");
        s.introEnabled=Mod::get()->getSettingValue<bool>("show-intro");
        if (!PlayLayer::init(level,replay,dontCreate)) { s.owner=nullptr; return false; }
        auto parent=m_uiLayer ? static_cast<CCNode*>(m_uiLayer) : this;
        auto panel=CCDrawNode::create();parent->addChild(panel,999);m_fields->hudPanel=panel;
        for (std::size_t i=0; i<Rows.size(); ++i) {
            auto label=CCLabelBMFont::create("", "bigFont.fnt");
            if (!label) continue;
            label->setID("frame-window-row-"+std::to_string(i));
            label->setAnchorPoint({0,1}); label->setColor(Colors[i]);
            parent->addChild(label,1000); m_fields->rows[i]=label;
        }
        auto status=CCLabelBMFont::create("", "chatFont.fnt");
        if (status) { status->setAnchorPoint({0,1}); parent->addChild(status,1000); m_fields->status=status; }
        auto menu=CCMenu::create(); menu->setPosition({0,0});
        auto text=CCLabelBMFont::create("Stop", "bigFont.fnt"); text->setScale(.35f);
        auto button=CCMenuItemSpriteExtra::create(text,this,menu_selector(OpenFrameMeterLayer::onStop));
        menu->addChild(button); parent->addChild(menu,1001);
        m_fields->controls=menu; m_fields->controlLabel=text;
        button->setPosition({CCDirector::sharedDirector()->getWinSize().width-35.f,20.f});
        refreshHud(); return true;
    }
    void refreshHud() {
        // Native init may call our reset hook before the UI nodes exist.
        if (!owns(this) || !m_fields->hudPanel) return;
        auto mod=Mod::get(); bool show=s.hudEnabled && s.stage==Stage::Playback && !s.intro;
        bool testing=s.stage==Stage::Baseline || s.stage==Stage::Search || s.stage==Stage::Confirm;
        updateProgress(testing);
        auto scale=static_cast<float>(mod->getSettingValue<double>("overlay-scale"));
        auto size=CCDirector::sharedDirector()->getWinSize();
        auto panel=m_fields->hudPanel;panel->clear();panel->setVisible(show);
        fwrui::box(panel,3,size.height-14.f-9.f*38.f*scale,105.f*scale+12.f,9.f*38.f*scale+10.f,{.025f,.035f,.07f,.80f},.6f,{.36f,.30f,.65f,.7f});
        std::array<unsigned,9> counts{};
        if(s.stage==Stage::Playback || s.videoPlaying) counts=s.playbackCounts;
        else if (s.windows.empty() && s.presses.empty()) {
            for (auto const& e:s.attempt) if(e.down) ++counts[8];
        } else {
            for (auto const& w:s.windows) ++counts[w.row()];
            counts[8]+=static_cast<unsigned>(s.presses.size()-s.windows.size());
        }
        for (std::size_t i=0;i<Rows.size();++i) if(auto label=m_fields->rows[i]) {
            label->setVisible(show);
            if(!show || !showcase::enabled("ui-animations") || label->numberOfRunningActions()==0)label->setScale(i<7 ? scale : scale*.65f);
            label->setPosition({6.f,size.height-8.f-static_cast<float>(i)*(38.f*scale)});
            auto value=std::string(Rows[i])+": "+std::to_string(counts[i]);
            bool changed=value!=label->getString();label->setString(value.c_str());
            if(changed && show && showcase::enabled("ui-animations")){
                label->stopAllActions();label->runAction(CCSequence::create(CCScaleTo::create(.08f,scale*1.08f),CCScaleTo::create(.12f,i<7?scale:scale*.65f),nullptr));
            }
        }
        if(auto label=m_fields->status) {
            label->setVisible(false); label->setScale(.3f);
            label->setPosition({6.f,size.height-12.f-9.f*(38.f*scale)});
            std::string text=s.status;
            if(s.stage==Stage::Search) text="Testing "+std::to_string(s.click+1)+"/"+std::to_string(s.presses.size());
            label->setString(text.c_str()); label->limitLabelWidth(180.f,.3f,.15f);
        }
        if(m_fields->controls) {
            m_fields->controls->setVisible(!testing && (s.replaying() || s.freeze) && !s.videoPlaying && !s.videoStarting);
            m_fields->controlLabel->setString(s.freeze ? "Tools" : "Stop");
        }
    }
    void updateProgress(bool testing) {
        auto size=CCDirector::sharedDirector()->getWinSize();
        if(testing && !m_fields->progress) {
            auto cover=CCLayerColor::create({8,12,25,255},size.width,size.height);cover->setID("analysis-progress");addChild(cover,12000);m_fields->progress=cover;
            auto title=CCLabelBMFont::create("ANALYZING CLICKS","bigFont.fnt");title->setScale(.6f);title->setPosition({size.width/2,size.height*.67f});cover->addChild(title);
            auto detail=CCLabelBMFont::create("","chatFont.fnt");detail->setPosition({size.width/2,size.height*.50f});detail->setScale(.55f);cover->addChild(detail);m_fields->progressText=detail;
            auto bar=CCDrawNode::create();cover->addChild(bar);m_fields->progressBar=bar;
            auto menu=CCMenu::create();menu->setPosition({size.width/2,size.height*.26f});cover->addChild(menu);
            auto text=CCLabelBMFont::create("Cancel","bigFont.fnt");text->setScale(.45f);
            menu->addChild(CCMenuItemSpriteExtra::create(text,this,menu_selector(OpenFrameMeterLayer::onStop)));
        }
        if(!testing && m_fields->progress){m_fields->progress->removeFromParentAndCleanup(true);m_fields->progress=nullptr;m_fields->progressText=nullptr;m_fields->progressBar=nullptr;}
        if(!testing && s.audioPaused && !s.intro && !s.videoStarting){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
        if(testing){
            auto text=s.stage==Stage::Baseline?std::string("Checking original replay"):s.stage==Stage::Confirm?std::string("Confirming results"):"Click "+std::to_string(s.click+1)+" / "+std::to_string(s.presses.size())+"   offset "+std::to_string(s.offset)+"f";
            m_fields->progressText->setString(text.c_str());
            float progress=s.stage==Stage::Baseline?0.f:s.stage==Stage::Confirm?1.f:static_cast<float>(s.click)/std::max<std::size_t>(1,s.presses.size());
            auto bar=m_fields->progressBar;bar->clear();float w=size.width*.65f,x=(size.width-w)/2,y=size.height*.40f;
            fwrui::box(bar,x,y,w,7,{.13f,.17f,.27f,1});fwrui::box(bar,x,y,w*progress,7,fwrui::rgba(fwrui::Cyan));
            // Native trials still advance on the game thread, behind this UI.
            FMODAudioEngine::sharedEngine()->pauseAllAudio();s.audioPaused=true;
        }
    }
    void showIntro() {
        if(getChildByID("frame-window-intro"))return;
        s.intro=true; s.introShown=true;
        FMODAudioEngine::sharedEngine()->pauseAllAudio();s.audioPaused=true;
        auto size=CCDirector::sharedDirector()->getWinSize();
        auto card=CCLayerColor::create({0,0,0,255},size.width,size.height);
        card->setID("frame-window-intro"); addChild(card,10000);
        auto add=[&](std::string const& text,float x,float y,float scale,ccColor3B color,float width) {
            auto label=CCLabelBMFont::create(text.c_str(),"bigFont.fnt");
            label->setAnchorPoint({0,1}); label->setPosition({x,y}); label->setColor(color);
            label->limitLabelWidth(width,scale,.12f); card->addChild(label);
        };
        std::string name=m_level->m_levelName;
        add(name,16,size.height*.74f,.8f,{255,255,255},size.width*.48f);
        add("FRAME WINDOW\nCOUNTER",16,size.height*.43f,.58f,{255,255,255},size.width*.45f);
        add("VANILLA 240 FPS",size.width*.55f,size.height-15,.42f,{243,128,247},size.width*.43f);
        constexpr std::array<char const*,7> legend={"9-12 FRAMES   37.5-50 ms","7-8 FRAMES   29.2-33.3 ms","5-6 FRAMES   20.8-25 ms","4 FRAMES   16.7 ms","3 FRAMES   12.5 ms","2 FRAMES   8.3 ms","1 FRAME   4.2 ms"};
        for(std::size_t i=0;i<legend.size();++i)
            add(legend[i],size.width*.55f,size.height-50-static_cast<float>(i)*(size.height-80)/7,.35f,Colors[i],size.width*.43f);
        float duration=static_cast<float>(Mod::get()->getSettingValue<double>("intro-seconds"));
        card->runAction(CCSequence::create(CCDelayTime::create(duration),CallFuncExt::create([this,card]{
            if(!owns(this)) return;
            card->removeFromParentAndCleanup(true); s.intro=false; s.bypassIntro=true;
            startGame(); startMusic();
            FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;
            s.bypassIntro=false; refreshHud();
        }),nullptr));
        refreshHud();
    }
    void startGame() {
        if(owns(this) && s.intro && !s.bypassIntro) return;
        // resetLevel may leave a delayed native start callback. An automated
        // attempt is already started synchronously below; don't start it twice.
        if(owns(this) && s.replaying() && m_started) return;
        // Intro is explicitly armed only for playback/video, never human gameplay.
        PlayLayer::startGame();
    }
    void startMusic() {
        if(owns(this) && s.intro) return;
        PlayLayer::startMusic();
    }
    void resetLevel() {
        meter::ReplayLifecycle::ResetScope resetScope(s.lifecycle);
        // Automated completion suppresses the native results screen. Its latch
        // must be released before native reset/pause will accept another action.
        if(owns(this) && (s.replaying() || s.freeze || s.stage==Stage::Finished || s.stage==Stage::Aborted))
            m_hasCompletedLevel=false;
        bool internal=s.internalReset; s.internalReset=false;
        if(!internal) {
            if(s.replaying() || s.videoStarting) {
                video.stop(); s.videoPlaying=false; s.videoStarting=false; s.learning=false;
                s.recording=false;
            }
            s.stage=s.recording?Stage::Record:Stage::Idle;
            s.freeze=false; s.valid=true; s.completed=false;
            if(s.recording) { s.attempt.clear(); s.trace.clear(); }
            s.status=s.recording?"Recording - 240 TPS":"Off - open pause tools";
        }
        s.seenTick=false; s.clock.reset();s.cursor=0; s.outcome.reset();s.endGateTick.reset(); s.reason.clear();s.queued=false;
        s.held[0]=s.held[1]=false;
        if(m_objectLayer) m_objectLayer->removeChildByID("input-marker-layer");
        PlayLayer::resetLevel();
        if(s.replaying()) {
            m_queuedButtons.clear(); s.inject=true;
            handleButton(false,1,true); handleButton(false,1,false); s.inject=false;
        }
        refreshHud();
    }
    void restartTrial() {
        s.freeze=false;s.completed=false;s.valid=true;
        s.internalReset=true;resetLevel();
        // Arm input dispatch before resuming the pause screen. Don't wait for a
        // delayed native start action or a second user click to start physics.
        if(s.replaying() && !s.intro && !m_started) {
            startGame();startMusic();
        }
        log::debug("Frame Window Recorder: trial armed; started={}, paused={}, tick={}, inputs={}",
            m_started,m_isPaused,m_gameState.m_currentProgress,s.plan.size());
    }
    void nextCandidate() {
        while(s.click<s.presses.size()) {
            if(!s.search) { auto [low,high]=meter::shiftLimits(s.saved,s.presses[s.click]); s.search.emplace(low,high); }
            auto candidate=s.search->candidate();
            if(candidate) {
                s.offset=*candidate; s.plan=meter::shifted(s.saved,s.presses[s.click],s.offset);
                restartTrial(); return;
            }
            s.windows.push_back(s.search->result()); s.search.reset(); ++s.click;
        }
        s.stage=Stage::Confirm; s.status="Confirming original replay"; s.plan=s.saved; restartTrial();
    }
    void consumeOutcome() {
        if(!s.outcome || !s.replaying()) { s.queued=false; return; }
        bool success=*s.outcome; s.outcome.reset(); s.queued=false;
        if(s.stage==Stage::Playback) {
            s.stage=Stage::Finished; s.freeze=true;
            s.status=success?"Replay finished":("Replay failed: "+(s.reason.empty()?std::string("incomplete attempt"):s.reason));
            if(!success)Notification::create(s.status,NotificationIcon::Error,6.f)->show();
            refreshHud();openFinishedControls();
            if(success){s.ending=true;s.endingAt=std::chrono::steady_clock::now();FMODAudioEngine::sharedEngine()->fadeOutMusic(2.5f,0);}
            else if(s.videoPlaying)video.stop();
            return;
        }
        if(s.stage==Stage::Baseline || s.stage==Stage::Confirm) {
            if(!success) { invalid("Original replay failed - no result: "+(s.reason.empty()?std::string("incomplete attempt"):s.reason)); refreshHud(); return; }
            if(s.stage==Stage::Confirm) {
                s.analyzed=true; s.stage=Stage::Finished; s.freeze=true; s.status="Analysis done - open pause tools"; saveResults(); refreshHud();openFinishedControls(); return;
            }
            if(s.learning) {s.savedTrace=std::move(s.learningTrace);s.learning=false;s.macro.finishTick=s.finishTick;}
            s.stage=Stage::Search; s.search.reset(); nextCandidate();
        } else {
            std::ofstream logFile(reportPath("-trials.csv"),std::ios::app);
            logFile<<s.click+1<<','<<s.offset<<','<<success<<'\n';
            s.search->outcome(success); nextCandidate();
        }
        refreshHud();
    }
    void openFinishedControls() {
        m_hasCompletedLevel=false;
        if(s.audioPaused){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
        showReplayResults(this);
    }
    void playEndAnimationToPos(CCPoint position) {
        if(owns(this)) {
            s.endGateTick=s.clock.tick();
            if(s.replaying()) {
                // The native end animation disables input before levelComplete.
                // Resolve the trial at its end gate without entering that state.
                levelComplete();return;
            }
        }
        PlayLayer::playEndAnimationToPos(position);
    }
    void levelComplete() {
        if(owns(this) && s.replaying()) {
            m_hasCompletedLevel=false;
            if(!s.outcome) {
                auto tick=s.clock.tick();
                bool complete=s.cursor==s.plan.size();
                if(s.stage==Stage::Baseline && s.learning) s.finishTick=tick;
                else if(s.stage!=Stage::Search && s.stage!=Stage::Playback) complete=complete && std::abs(static_cast<long long>(tick)-s.finishTick)<=1;
                s.outcome=complete;
            }
            return; // Analysis replays must never award completion.
        }
        if(s.recording && !s.completed) {
            s.completed=true;
            if(s.valid && !s.trace.empty() && !s.attempt.empty() && !m_isPracticeMode && !m_isPlatformer && !m_startPosObject) {
                s.saved=s.attempt; s.savedTrace=s.trace; s.finishTick=s.endGateTick.value_or(s.clock.tick());
                s.presses.clear(); s.windows.clear();
                for(std::size_t i=0;i<s.saved.size();++i) if(s.saved[i].down) s.presses.push_back(i);
                saveRecording();
                s.macro={static_cast<std::uint32_t>(m_level->m_levelID.value()),s.finishTick,std::string(m_level->m_levelName),s.saved,{}};
                s.recording=false; s.stage=Stage::Idle; s.analyzed=false;
                s.status="Macro saved - pause to analyze";
                Notification::create(s.status,NotificationIcon::Info,5.f)->show();
            }
        }
        PlayLayer::levelComplete(); refreshHud();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool syntheticProbe=object && object==m_anticheatSpike;
        bool activePlayer=player && (player==m_player1 || (m_gameState.m_isDualMode && player==m_player2));
        if(owns(this) && s.replaying() && s.lifecycle.gameplayDeath(syntheticProbe,activePlayer)) {
            if(!s.outcome) {
                s.reason="Player "+std::string(player==m_player1?"1":"2")+" died at tick "+std::to_string(s.clock.tick());
                log::info("Frame Window Recorder: {}",s.reason);
                s.outcome=false;
            }
            return;
        }
        // Forward probes and restart callbacks to the game unchanged. They aren't
        // successful trials or failed trials, and must not stop replay at 0%.
        PlayLayer::destroyPlayer(player,object);
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if(!owns(this)) return;
        servicePending();
    }
    void servicePending() {
        if(s.ending){
            auto elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-s.endingAt).count();
            showcase::menuAudio.volume(std::clamp(1.f-(elapsed-.5f)/2.f,0.f,1.f));
            if(elapsed>=3.f){s.ending=false;showcase::menuAudio.stop();FMODAudioEngine::sharedEngine()->stopMusic(0);if(s.videoPlaying)video.stop();}
        }
        if(s.videoStarting && !s.videoArming) {
            auto state=video.state();
            if(state==meter::VideoCapture::State::Error || state==meter::VideoCapture::State::Done) {
                s.videoStarting=false;s.freeze=true;s.status=video.message();
                removeChildByID("video-preparing");
                if(s.audioPaused){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
                Notification::create("Video capture could not start. Open pause tools.",NotificationIcon::Error,5.f)->show();
            } else if(state==meter::VideoCapture::State::Capturing) {
                auto now=std::chrono::steady_clock::now();
                if(s.videoReady.time_since_epoch().count()==0) s.videoReady=now;
                if(now-s.videoReady>std::chrono::seconds(1)) {
                    s.videoStarting=false;s.videoPlaying=true;s.freeze=false;
                    removeChildByID("video-preparing");
                    if(s.introEnabled)s.intro=true;
                    startPlayback();
                    if(s.introEnabled) showIntro();
                    else if(s.audioPaused){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
                }
            }
        }
        if(s.videoPlaying && (video.state()==meter::VideoCapture::State::Done || video.state()==meter::VideoCapture::State::Error)) {
            bool error=video.state()==meter::VideoCapture::State::Error;
            s.videoPlaying=false;
            if(error && s.replaying()) invalid("Video capture failed - open pause tools");
            s.status=video.message();
            Notification::create(error?"Video export failed. Open pause tools.":"MP4 with game audio saved.",error?NotificationIcon::Error:NotificationIcon::Success,6.f)->show();
        }
        refreshHud();
        if(s.outcome && s.replaying() && !s.queued) {
            s.queued=true; retain();
            geode::queueInMainThread([this]{ if(owns(this)) consumeOutcome(); release(); });
        }
    }
    void onStop(CCObject*) {
        if(s.freeze && !s.videoStarting){showReplayResults(this);return;}
        bool interruptedAnalysis=s.stage==Stage::Baseline || s.stage==Stage::Search || s.stage==Stage::Confirm;
        if(auto card=getChildByID("frame-window-intro"))card->removeFromParentAndCleanup(true);
        s.intro=false;
        if(s.audioPaused){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
        m_queuedButtons.clear(); s.inject=true;
        handleButton(false,1,true); handleButton(false,1,false); s.inject=false;
        video.stop(); s.videoStarting=false; s.videoPlaying=false;s.videoArming=false;s.ending=false;
        removeChildByID("video-preparing");
        s.recording=false;s.learning=false;s.stage=Stage::Idle;s.freeze=false;s.outcome.reset();
        s.search.reset();
        if(interruptedAnalysis){s.windows.clear();s.analyzed=false;}
        resetLevel();
    }
    void beginMeterRecording() {
        s.recording=true;s.freeze=false;s.learning=false;s.outcome.reset();s.stage=Stage::Record;
        s.analyzed=false;s.saved.clear();s.savedTrace.clear();
        s.windows.clear();s.presses.clear();s.macro={};resetLevel();
    }
    void endMeterRecording() {
        if(s.recording && s.valid && !s.attempt.empty()) {
            s.saved=s.attempt;s.savedTrace.clear();s.finishTick=std::max(s.clock.tick(),s.saved.back().tick);
            s.macro={static_cast<std::uint32_t>(m_level->m_levelID.value()),s.finishTick,std::string(m_level->m_levelName),s.saved,{}};
            s.presses.clear();for(std::size_t i=0;i<s.saved.size();++i)if(s.saved[i].down)s.presses.push_back(i);
            s.analyzed=false;s.windows.clear();s.recording=false;s.stage=Stage::Idle;s.status="Macro saved in memory";
        } else if(s.replaying() || s.freeze || s.videoStarting) {s.freeze=false;onStop(nullptr);}
        else {s.recording=false;s.stage=Stage::Idle;s.status="Recording stopped";}
        refreshHud();
    }
    void loadMacro(meter::Macro macro) {
        bool heldAtResults=s.freeze;
        s.recording=false;s.completed=false;s.stage=Stage::Idle;s.valid=true;s.freeze=heldAtResults;
        s.outcome.reset();s.attempt.clear();s.trace.clear();s.seenTick=false;s.search.reset();
        s.macro=std::move(macro);s.saved=s.macro.inputs;s.finishTick=s.macro.finishTick;
        s.savedTrace.clear();s.windows.clear();s.presses.clear();s.analyzed=false;
        for(std::size_t i=0;i<s.saved.size();++i)if(s.saved[i].down)s.presses.push_back(i);
        s.status="Imported macro - verify with Analyze";refreshHud();
    }
    void startAnalysis() {
        s.recording=false;s.freeze=false;s.stage=Stage::Baseline;s.status="Checking original replay";
        s.analyzed=false;s.windows.clear();s.click=0;s.search.reset();s.plan=s.saved;
        s.learning=s.savedTrace.empty();s.learningTrace.clear();
        if(s.learning) s.finishTick=std::min(MaxTicks-480,std::max(s.macro.finishTick,s.saved.back().tick+240*120));
        std::ofstream f(reportPath("-trials.csv"),std::ios::trunc);f<<"click,offset_frames,completed\n";
        restartTrial();
    }
    void startPlayback() {
        s.ending=false;showcase::menuAudio.stop();
        s.recording=false;s.freeze=false;s.learning=false;s.stage=Stage::Playback;
        s.status="Macro playback";s.plan=s.saved;s.playbackCounts={};
        if(s.savedTrace.empty())s.finishTick=std::min(MaxTicks-480,std::max(s.macro.finishTick,s.saved.back().tick+240*120));
        bool intro=s.introEnabled&&!s.videoPlaying;
        if(intro)s.intro=true;
        restartTrial();
        if(intro)showIntro();
    }
    void onQuit() {
        if(owns(this)){video.stop();showcase::menuAudio.stop();s.ending=false;s.videoArming=false;}
        if(owns(this) && s.audioPaused){FMODAudioEngine::sharedEngine()->resumeAllAudio();s.audioPaused=false;}
        if(owns(this)) { s.owner=nullptr; s.intro=false; s.freeze=false; }
        PlayLayer::onQuit();
    }
};

namespace { void service() { if(s.owner) static_cast<OpenFrameMeterLayer*>(s.owner)->servicePending(); } }



class FrameMeterTools : public Popup {
    Ref<PauseLayer> m_pause;
    Ref<EndLevelLayer> m_end;
    PlayLayer* m_owner=nullptr;
    CCLabelBMFont* m_info=nullptr;
    CCLabelBMFont* m_context=nullptr;
    CCLabelBMFont* m_section=nullptr;
    CCLabelBMFont* m_hudState=nullptr;
    CCLabelBMFont* m_introState=nullptr;
    fwrui::Layout m_layout{};
    std::array<CCMenuItemSpriteExtra*,10> m_actions{};
    std::array<CCDrawNode*,4> m_tabBackgrounds{};
    std::array<CCLabelBMFont*,4> m_tabLabels{};
    int m_tab=0;
    bool m_musicPreference=true;
    inline static std::optional<meter::Macro> m_staged;
    bool active() const {return m_owner && PlayLayer::get()==m_owner;}
    bool dialogContext(){return getParent() && (!m_owner || usable());}
    void alert(std::string const& text){FLAlertLayer::create("Frame Window Recorder",text,"OK")->show();}
    bool usable() {
        if(!m_owner){alert("Open a level to record, analyze or replay a macro.");return false;}
        if(!m_owner || PlayLayer::get()!=m_owner){alert("This level session has ended.");return false;}
        if(!s.owner)s.owner=m_owner;
        if(s.owner!=m_owner){alert("A different level session is active.");return false;}
        return true;
    }
    bool idle() {
        if(m_owner && !usable())return false;
        auto state=video.state();
        if(s.replaying() || s.videoStarting || state==meter::VideoCapture::State::Starting || state==meter::VideoCapture::State::Capturing || state==meter::VideoCapture::State::Finalizing) {
            alert("Stop the current operation and let video finalization finish first.");return false;
        }
        return true;
    }
    bool hasMacro() {
        if(!usable())return false;
        if(!idle())return false;
        if(s.saved.empty()){alert("Record and stop/save a macro, complete a recorded run, or import a GDR file first.");return false;}
        return true;
    }
    void resumeWith(geode::Function<void(OpenFrameMeterLayer*)> action) {
        if(!usable())return;
        Ref<PlayLayer> owner=m_owner;Ref<PauseLayer> pause=m_pause;Ref<EndLevelLayer> end=m_end;
        Ref<GJGameLevel> level=owner->m_level;
        auto snapshot=end?std::make_shared<Session>(s):nullptr;
        s.ending=false;
        onClose(nullptr);
        if(pause) {
            // Starting after onResume via a queued callback allowed unrecorded
            // native ticks to run before the macro/reset was armed.
            if(s.owner==owner.data() && PlayLayer::get()==owner.data())action(static_cast<OpenFrameMeterLayer*>(owner.data()));
            pause->onResume(nullptr);
            return;
        } else if(end) {
            s.internalReset=true;
            end->onReplay(nullptr);
            if(auto current=PlayLayer::get()) {
                auto opened=current->m_level;
                bool sameLevel=opened==level.data() || (opened && opened->m_levelID.value()==level->m_levelID.value()
                    && opened->m_levelName==level->m_levelName && opened->m_levelString==level->m_levelString);
                if(sameLevel) {
                    s=std::move(*snapshot);s.owner=current;s.internalReset=false;owner=current;
                }
            }
        }
        if(s.owner==owner.data() && PlayLayer::get()==owner.data())action(static_cast<OpenFrameMeterLayer*>(owner.data()));
    }
    static std::filesystem::path encoder() {return meter::discoverEncoder(Mod::get()->getSavedValue<std::string>("ffmpeg-path",""));}
    void info(float) {
        if(!m_info)return;
        auto music=showcase::enabled("menu-music");if(music!=m_musicPreference){m_musicPreference=music;if(music)showcase::menuAudio.start();else showcase::menuAudio.stop();}
        std::string message=active()?s.status:"Open a level to use recording and analysis.";
        if(message=="Off - open Frame Meter in pause")message="Ready. Choose an action.";
        if(video.state()!=meter::VideoCapture::State::Idle)message+=" | "+video.message();
        if(message.size()>150)message=message.substr(0,147)+"...";
        m_info->setString(message.c_str());m_info->limitLabelWidth(m_layout.contentWidth,.3f,.17f);
        std::string context;
        if(m_tab==0)context=active()?"Level: "+std::string(m_owner->m_level->m_levelName):"Open a level, then choose Record macro.";
        else if(m_tab==1) {
            if(active() && !s.saved.empty())context=std::to_string(s.saved.size())+" inputs loaded / 240 FPS";
            else if(m_staged)context="Staged: "+m_staged->levelName+" / "+std::to_string(m_staged->inputs.size())+" inputs";
            else context="Import a macro here, then open its matching level.";
        } else if(m_tab==2)context="Create a showcase after analyzing the loaded macro.";
        else context="Replay/video visuals only. More options in Settings.";
        m_context->setString(context.c_str());m_context->limitLabelWidth(m_layout.contentWidth,.33f,.18f);
        if(m_hudState)m_hudState->setString((active()?s.hudEnabled:Mod::get()->getSettingValue<bool>("show-hud"))?"On":"Off");
        if(m_introState)m_introState->setString((active()?s.introEnabled:Mod::get()->getSettingValue<bool>("show-intro"))?"On":"Off");
    }
    void hud(CCObject*){bool enabled=!(active()?s.hudEnabled:Mod::get()->getSettingValue<bool>("show-hud"));Mod::get()->setSettingValue<bool>("show-hud",enabled);if(active()){s.hudEnabled=enabled;static_cast<OpenFrameMeterLayer*>(s.owner)->refreshHud();}info(0);}
    void intro(CCObject*){bool enabled=!(active()?s.introEnabled:Mod::get()->getSettingValue<bool>("show-intro"));Mod::get()->setSettingValue<bool>("show-intro",enabled);if(active())s.introEnabled=enabled;info(0);}
    void record(CCObject*){
        if(!usable()||!idle())return;
        if(s.saved.empty()){resumeWith([](auto p){p->beginMeterRecording();});return;}
        geode::createQuickPopup("Replace recording?","Starting a new recording replaces the macro in memory. Export it first if you want to keep it.","Cancel","Record",[self=Ref<FrameMeterTools>(this)](FLAlertLayer*,bool yes){if(yes&&self->dialogContext()&&self->idle())self->resumeWith([](auto p){p->beginMeterRecording();});});
    }
    void stop(CCObject*){if(usable()){static_cast<OpenFrameMeterLayer*>(s.owner)->endMeterRecording();info(0);}}
    void analyze(CCObject*){if(hasMacro())resumeWith([](auto p){p->startAnalysis();});}
    void replay(CCObject*){if(hasMacro())resumeWith([](auto p){p->startPlayback();});}
    void importGdr(CCObject*) {
        if(!idle())return;
        async::spawn(file::pick(file::PickMode::OpenFile,{.filters={{"Supported macros",{"*.gdr2","*.gdr","*.json","*.txt"}},{"GDR2",{"*.gdr2"}},{"GDR1 / Mega Hack JSON",{"*.gdr","*.json"}},{"Plaintext",{"*.txt"}}}}),[self=Ref<FrameMeterTools>(this)](file::PickResult result){
            if(!self->dialogContext())return;
            if(!result.isOk()){self->alert(result.unwrapErr());return;}
            auto selected=result.unwrap();if(!selected)return;
            try {
                auto m=meter::readMacro(*selected);
                if(m.requiresLevelBinding){
                    if(!self->m_owner){self->alert("This format has no level identity. Open the intended level and import it there.");return;}
                    auto level=s.owner->m_level;
                    m.levelId=static_cast<std::uint32_t>(level->m_levelID.value());m.levelName=level->m_levelName;
                    geode::createQuickPopup("Attach macro?","This file has no level information. Attach its inputs to <cy>"+m.levelName+"</c>? Timing is kept unchanged. A successful baseline replay is still required.","Cancel","Attach",[self,macro=std::move(m)](FLAlertLayer*,bool yes)mutable{
                        if(!yes||!self->dialogContext()||!self->active())return;
                        macro.requiresLevelBinding=false;
                        static_cast<OpenFrameMeterLayer*>(s.owner)->loadMacro(std::move(macro));self->info(0);
                    });return;
                }
                if(!self->m_owner){m_staged=std::move(m);self->info(0);return;}
                auto id=static_cast<std::uint32_t>(s.owner->m_level->m_levelID.value());
                std::string name=s.owner->m_level->m_levelName;
                if((m.levelId && id && m.levelId!=id) || ((!m.levelId || !id) && m.levelName!=name))throw std::runtime_error("The macro belongs to a different level. Open its level first.");
                static_cast<OpenFrameMeterLayer*>(s.owner)->loadMacro(std::move(m));self->info(0);
            }catch(std::exception const& e){self->alert(e.what());}
        });
    }
    void exportGdr(CCObject*) {
        if(!idle())return;
        std::optional<meter::Macro> macro=m_staged;
        if(active() && !s.saved.empty()){macro=s.macro;macro->inputs=s.saved;}
        if(!macro || macro->inputs.empty()){alert("Import or record a macro before exporting.");return;}
        async::spawn(file::pick(file::PickMode::SaveFile,{.defaultPath=Mod::get()->getSaveDir()/"macro.gdr2",.filters={{"GDR2",{"*.gdr2"}},{"GDR1 MessagePack",{"*.gdr"}},{"GDR1 JSON",{"*.gdr.json"}},{"Plaintext (inputs only)",{"*.txt"}}}}),[self=Ref<FrameMeterTools>(this),macro=std::move(*macro)](file::PickResult result){
            if(!self->dialogContext())return;
            if(!result.isOk()){self->alert(result.unwrapErr());return;}
            auto path=result.unwrap();if(!path)return;
            try {
                if(path->extension().empty()){*path+=".gdr2";if(std::filesystem::exists(*path))throw std::runtime_error("That .gdr2 already exists. Select it explicitly to confirm replacement.");}
                auto ext=path->extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                if(ext!=".gdr" && ext!=".json" && ext!=".gdr2" && ext!=".txt")throw std::runtime_error("Choose a .gdr2, .gdr, .json or .txt filename.");
                meter::writeMacro(*path,macro);
                self->alert("Macro exported successfully.");
            }catch(std::exception const& e){self->alert(e.what());}
        });
    }
    void chooseEncoder(CCObject*) {
        if(!idle())return;
        async::spawn(file::pick(file::PickMode::OpenFile,{.filters={{"FFmpeg executable",{"*.exe"}}}}),[self=Ref<FrameMeterTools>(this)](file::PickResult result){
            if(!self->dialogContext())return;
            if(!result.isOk()){self->alert(result.unwrapErr());return;}
            if(auto path=result.unwrap()){Mod::get()->setSavedValue<std::string>("ffmpeg-path",path->string());self->alert("Encoder selected. Use Create Video after analyzing a macro.");}
        });
    }
    void createVideo(CCObject*) {
        if(!hasMacro())return;
        if(!s.analyzed){alert("Analyze this macro successfully before creating a video with click-window counts.");return;}
        auto ffmpeg=encoder();
        if(ffmpeg.empty()){alert("FFmpeg was not found in PATH or standard portable locations. Install FFmpeg or select an optional encoder override.");return;}
        async::spawn(file::pick(file::PickMode::SaveFile,{.defaultPath=Mod::get()->getSaveDir()/"frame-window-video.mp4",.filters={{"MP4 with game audio",{"*.mp4"}}}}),[self=Ref<FrameMeterTools>(this),ffmpeg](file::PickResult result){
            if(!self->getParent() || !self->usable())return;
            if(!result.isOk()){self->alert(result.unwrapErr());return;}
            auto path=result.unwrap();if(!path)return;
            try {
                if(path->extension().empty()){*path+=".mp4";if(std::filesystem::exists(*path))throw std::runtime_error("That MP4 exists. Select it explicitly to confirm replacement.");}
                if(path->extension()!=".mp4")throw std::runtime_error("Choose an .mp4 filename.");
                HWND window=GetActiveWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);
                if(pid!=GetCurrentProcessId())throw std::runtime_error("Focus the Geometry Dash window and try again.");
                auto work=Mod::get()->getSaveDir()/("video-work-"+std::to_string(GetTickCount64()));
                self->resumeWith([window,ffmpeg,output=*path,work](auto p){
                    s.freeze=true;s.recording=false;
                    try {
                        auto size=CCDirector::sharedDirector()->getWinSize();
                        auto slate=CCLayerColor::create({0,0,0,255},size.width,size.height);slate->setID("video-preparing");p->addChild(slate,15000);
                        s.videoStarting=true;s.videoArming=true;s.videoReady={};s.status="Starting MP4 capture";
                        p->runAction(CCSequence::create(CCDelayTime::create(.3f),CallFuncExt::create([p,window,ffmpeg,output,work]{
                            if(!owns(p)||!s.videoArming)return;
                            s.videoArming=false;
                            try {
                                if(!video.start(window,ffmpeg,output,work))throw std::runtime_error("A video operation is already running");
                            }catch(std::exception const& e){s.videoStarting=false;s.status=e.what();p->removeChildByID("video-preparing");Notification::create(s.status,NotificationIcon::Error,5.f)->show();}
                        }),nullptr));
                        FMODAudioEngine::sharedEngine()->pauseAllAudio();s.audioPaused=true;
                    }catch(std::exception const& e){
                        s.videoStarting=false;s.videoArming=false;s.status=e.what();p->removeChildByID("video-preparing");
                        FLAlertLayer::create("Frame Window Recorder",e.what(),"OK")->show();
                    }
                    p->refreshHud();
                });
            }catch(std::exception const& e){self->alert(e.what());}
        });
    }
    void closeTools(CCObject*){onClose(nullptr);}
    void selectTab(int tab) {
        Mod::get()->setSavedValue<int>("tools-tab",tab);
        m_tab=tab;
        constexpr char const* titles[]={"Record & analyze","Macro files","Video showcase","Display"};
        constexpr ccColor3B accents[]={fwrui::Violet,fwrui::Cyan,fwrui::Amber,fwrui::Coral};
        m_section->setString(titles[tab]);m_section->setColor(accents[tab]);
        for(int i=0;i<4;++i) {
            auto d=m_tabBackgrounds[i];d->clear();
            fwrui::box(d,0,0,m_layout.sidebar-20,28,i==tab?fwrui::rgba(accents[i],.16f):ccColor4F{.04f,.05f,.085f,1});
            if(i==tab)fwrui::box(d,0,3,2,22,fwrui::rgba(accents[i]));
            m_tabLabels[i]->setColor(i==tab?ccColor3B{248,249,255}:ccColor3B{145,153,177});
        }
        for(int i=0;i<10;++i) {
            int group=i<4?0:i<6?1:i<8?2:3;
            m_actions[i]->setVisible(group==tab);
        }
        info(0);
    }
    void tabStudio(CCObject*){selectTab(0);}
    void tabMacros(CCObject*){selectTab(1);}
    void tabShowcase(CCObject*){selectTab(2);}
    void tabDisplay(CCObject*){selectTab(3);}
    void settings(CCObject*){geode::openSettingsPopup(Mod::get());}
    CCLabelBMFont* text(std::string const& value,float x,float y,float scale,ccColor3B color,float width=0,char const* font="chatFont.fnt") {
        auto label=CCLabelBMFont::create(value.c_str(),font);label->setAnchorPoint({0,.5f});
        label->setPosition({x,y});label->setColor(color);
        if(width>0)label->limitLabelWidth(width,scale,.16f);else label->setScale(scale);
        m_mainLayer->addChild(label,2);return label;
    }
    void buildUi() {
        using namespace fwrui;
        auto w=m_layout.width,h=m_layout.height;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);
        m_mainLayer->setLayout(nullptr);
        m_buttonMenu->ignoreAnchorPointForPosition(false);m_buttonMenu->setAnchorPoint({0,0});
        m_buttonMenu->setPosition({0,0});m_buttonMenu->setContentSize({w,h});
        auto gradient=CCLayerGradient::create({13,16,30,255},{5,8,16,255},{1,-1});
        gradient->setContentSize({w,h});m_mainLayer->addChild(gradient,-10);
        auto backdrop=CCDrawNode::create();m_mainLayer->addChild(backdrop,-9);
        box(backdrop,w-180,45,142,142,{0,0,0,0},1.1f,rgba(Violet,.11f));
        box(backdrop,w-156,28,142,142,{0,0,0,0},1.1f,rgba(Cyan,.08f));
        box(backdrop,w-132,11,142,142,{0,0,0,0},1.1f,rgba(Lime,.055f));
        box(backdrop,0,0,m_layout.sidebar,h,{.025f,.033f,.061f,.9f});
        box(backdrop,m_layout.sidebar,0,1,m_layout.headerBottom,{.16f,.19f,.28f,.7f});
        box(backdrop,0,m_layout.headerBottom,w,h-m_layout.headerBottom,{.045f,.055f,.098f,.95f});
        constexpr ccColor3B spectrum[]={Violet,Cyan,Lime,Amber,Coral};
        for(int i=0;i<5;++i)box(backdrop,i*w/5,h-2,w/5+1,2,rgba(spectrum[i]));
        auto mark=stopwatch(30);mark->setPosition({14,h-45});m_mainLayer->addChild(mark,2);
        text("FRAME WINDOW RECORDER",54,h-25,.51f,{245,247,255},w-170,"bigFont.fnt");
        text("Macro recording and frame-window showcases",54,h-48,.32f,{150,164,193},w-100);
        box(backdrop,w-107,h-35,50,21,rgba(Violet,.2f),.5f,rgba(Violet,.45f));
        auto badge=text("v1",w-101,h-24,.29f,{203,176,255},40,"bigFont.fnt");
        auto close=CCMenuItemSpriteExtra::create(closeIcon(28),this,menu_selector(FrameMeterTools::closeTools));
        close->setPosition({w-24,h-24});close->m_scaleMultiplier=1.06f;m_buttonMenu->addChild(close);
        text("WORKSPACE",14,m_layout.headerBottom-14,.28f,{106,121,152},m_layout.sidebar-24);
        constexpr char const* tabs[]={"Studio","Macro files","Showcase","Display"};
        SEL_MenuHandler callbacks[]={menu_selector(FrameMeterTools::tabStudio),menu_selector(FrameMeterTools::tabMacros),menu_selector(FrameMeterTools::tabShowcase),menu_selector(FrameMeterTools::tabDisplay)};
        float step=h<280?32.f:38.f;
        for(int i=0;i<4;++i) {
            auto v=CCNode::create();v->setContentSize({m_layout.sidebar-20,28});
            auto draw=CCDrawNode::create();v->addChild(draw);m_tabBackgrounds[i]=draw;
            auto label=CCLabelBMFont::create(tabs[i],"chatFont.fnt");label->setAnchorPoint({0,.5f});
            label->setPosition({12,14});label->limitLabelWidth(m_layout.sidebar-44,.55f,.3f);v->addChild(label);m_tabLabels[i]=label;
            auto item=CCMenuItemSpriteExtra::create(v,this,callbacks[i]);item->m_scaleMultiplier=1.015f;
            item->setPosition({m_layout.sidebar/2,m_layout.headerBottom-42-i*step});m_buttonMenu->addChild(item);
        }
        text("PlutoGDTF",14,h<280?14.f:20.f,.31f,{127,140,169},m_layout.sidebar-24);
        m_section=text("",m_layout.contentX,m_layout.headerBottom-16,.42f,{245,247,255},m_layout.contentWidth,"bigFont.fnt");
        m_context=text("",m_layout.contentX,m_layout.headerBottom-36,.33f,{142,156,184},m_layout.contentWidth);
        struct Action {char const* caption;char const* hint;SEL_MenuHandler callback;ccColor3B color;};
        std::array<Action,10> actions={{
            {"Record macro","Start a fresh take",menu_selector(FrameMeterTools::record),Violet},
            {"Stop & save","Keep this recording",menu_selector(FrameMeterTools::stop),Coral},
            {"Analyze clicks","Measure frame windows",menu_selector(FrameMeterTools::analyze),Cyan},
            {"Replay macro","Play loaded inputs",menu_selector(FrameMeterTools::replay),Lime},
            {"Import macro","GDR2, GDR1, JSON, text",menu_selector(FrameMeterTools::importGdr),Cyan},
            {"Export macro","GDR2, GDR1 or text",menu_selector(FrameMeterTools::exportGdr),Violet},
            {"Create video","MP4 with game audio",menu_selector(FrameMeterTools::createVideo),Amber},
            {"Choose encoder","FFmpeg override (optional)",menu_selector(FrameMeterTools::chooseEncoder),Cyan},
            {"Show HUD","",menu_selector(FrameMeterTools::hud),Lime},
            {"Show intro","",menu_selector(FrameMeterTools::intro),Coral}
        }};
        for(int i=0;i<10;++i) {
            int local=i<4?i:i<6?i-4:i<8?i-6:i-8;
            bool enabled=active() || i==4 || i==5 || i==7 || i==8 || i==9;
            auto v=CCNode::create();v->setContentSize({m_layout.cardWidth,m_layout.cardHeight});
            auto d=CCDrawNode::create();v->addChild(d);
            box(d,0,0,m_layout.cardWidth,m_layout.cardHeight,{.065f,.078f,.13f,1},.7f,{.14f,.17f,.25f,1});
            auto accent=enabled?actions[i].color:ccColor3B{79,89,110};
            box(d,0,m_layout.cardHeight-2,m_layout.cardWidth,2,rgba(accent,.85f));
            auto icon=glyph(i,accent);float iconSize=m_layout.cardWidth<120?16.f:22.f;
            icon->setScale(iconSize/icon->getContentSize().width);icon->setPosition({10,m_layout.cardHeight-iconSize-10});v->addChild(icon);
            float tx=iconSize+18,tw=m_layout.cardWidth-tx-8;
            auto label=CCLabelBMFont::create(actions[i].caption,"bigFont.fnt");label->setAnchorPoint({0,.5f});
            label->setPosition({tx,m_layout.cardHeight-17});label->limitLabelWidth(tw,.36f,.16f);
            label->setColor(enabled?ccColor3B{238,242,255}:ccColor3B{114,125,148});v->addChild(label);
            auto hint=CCLabelBMFont::create(enabled?actions[i].hint:"Open a level","chatFont.fnt");hint->setAnchorPoint({0,.5f});
            hint->setPosition({10,12});hint->setColor({137,152,183});hint->limitLabelWidth(m_layout.cardWidth-20,.32f,.18f);v->addChild(hint);
            if(i==8)m_hudState=hint;if(i==9)m_introState=hint;
            auto button=CCMenuItemSpriteExtra::create(v,this,actions[i].callback);button->setEnabled(enabled);
            button->m_scaleMultiplier=1.025f;
            button->setPosition({m_layout.cardX(local%2)+m_layout.cardWidth/2,m_layout.cardY(local/2)+m_layout.cardHeight/2});
            m_buttonMenu->addChild(button);m_actions[i]=button;
        }
        auto settingsLabel=CCLabelBMFont::create("Settings","bigFont.fnt");settingsLabel->setScale(.28f);settingsLabel->setColor(Cyan);
        auto settingsButton=CCMenuItemSpriteExtra::create(settingsLabel,this,menu_selector(FrameMeterTools::settings));
        settingsButton->setPosition({m_layout.sidebar/2,42});m_buttonMenu->addChild(settingsButton);
        m_info=text("",m_layout.contentX,20,.3f,{162,176,203},m_layout.contentWidth);
        selectTab(std::clamp(Mod::get()->getSavedValue<int>("tools-tab",0),0,3));
    }
public:
    static FrameMeterTools* create(PauseLayer* pause=nullptr,EndLevelLayer* end=nullptr,bool home=false) {
        auto ret=new FrameMeterTools;
        auto size=CCDirector::sharedDirector()->getWinSize();
        if(ret->init(size)) {
            ret->autorelease();ret->m_pause=pause;ret->m_end=end;
            ret->m_owner=home?nullptr:PlayLayer::get();
            if(ret->m_owner && !s.owner)s.owner=ret->m_owner;
            if(!ret->m_owner && !s.saved.empty()){m_staged=s.macro;m_staged->inputs=s.saved;}
            if(ret->m_owner && m_staged && s.saved.empty() && !s.recording && !s.replaying()) {
                auto id=static_cast<std::uint32_t>(ret->m_owner->m_level->m_levelID.value());
                std::string name=ret->m_owner->m_level->m_levelName;
                bool match=m_staged->levelId&&id ? m_staged->levelId==id : m_staged->levelName==name;
                if(match){static_cast<OpenFrameMeterLayer*>(ret->m_owner)->loadMacro(std::move(*m_staged));m_staged.reset();}
            }
            ret->m_layout=fwrui::Layout::make(size.width,size.height);
            ret->buildUi();ret->schedule(schedule_selector(FrameMeterTools::info),.25f);return ret;
        }
        delete ret;return nullptr;
    }
    void onClose(CCObject* sender) override {showcase::menuAudio.stop();Popup::onClose(sender);}
    void onExit() override {showcase::menuAudio.stop();Popup::onExit();}
    void show() {
        Popup::show();m_musicPreference=showcase::enabled("menu-music");showcase::menuAudio.start();
        m_mainLayer->stopAllActions();m_mainLayer->setScale(1.f);
        if(showcase::enabled("ui-animations")){m_mainLayer->setScale(.98f);m_mainLayer->runAction(CCEaseSineOut::create(CCScaleTo::create(.18f,1.f)));}
    }
};
namespace {
void showReplayResults(PlayLayer* owner) {
    if(s.owner!=owner || PlayLayer::get()!=owner)return;
    auto scene=CCDirector::sharedDirector()->getRunningScene();
    if(!scene || scene->getChildByID("fwr-results-tools"))return;
    if(auto panel=FrameMeterTools::create()) {
        panel->setID("fwr-results-tools");panel->show();
    }
}
}
class $modify(OpenFrameMeterPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if(getChildByID("fwr-controls"))return;
        auto menu=CCMenu::create();menu->setID("fwr-controls");menu->setPosition({0,0});
        auto diameter=fwrui::settingsDiameter(this);
        auto button=CCMenuItemSpriteExtra::create(fwrui::stopwatch(diameter),this,menu_selector(OpenFrameMeterPause::openTools));
        button->setID("frame-window-recorder-button");button->m_scaleMultiplier=1.08f;
        auto size=CCDirector::sharedDirector()->getWinSize();
        CCPoint position{size.width-28.f,size.height-80.f};
        for(auto id:{"settings-button","options-button"})if(auto gear=getChildByIDRecursive(id)) {
            position=gear->convertToWorldSpace(gear->getContentSize()/2);position.y-=diameter+8;break;
        }
        menu->addChild(button);addChild(menu,1000);button->setPosition(menu->convertToNodeSpace(position));
    }
    void openTools(CCObject*) {if(auto popup=FrameMeterTools::create(this))popup->show();}
};
class $modify(FrameWindowRecorderHome, MenuLayer) {
    bool init() {
        if(!MenuLayer::init())return false;
        auto button=CCMenuItemSpriteExtra::create(fwrui::stopwatch(32),this,menu_selector(FrameWindowRecorderHome::openRecorder));
        button->setID("frame-window-recorder-button");button->m_scaleMultiplier=1.08f;
        if(auto side=getChildByID("right-side-menu")) {
            if(side->getLayout()){side->addChild(button);side->updateLayout();return true;}
        }
        auto menu=CCMenu::create();menu->setPosition({0,0});addChild(menu,100);
        auto size=CCDirector::sharedDirector()->getWinSize();button->setPosition({size.width-28,size.height*.3f});menu->addChild(button);return true;
    }
    void openRecorder(CCObject*){if(auto popup=FrameMeterTools::create(nullptr,nullptr,true))popup->show();}
};
class $modify(FrameWindowRecorderComplete, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto button=CCMenuItemSpriteExtra::create(fwrui::stopwatch(fwrui::settingsDiameter(this)),this,menu_selector(FrameWindowRecorderComplete::openRecorder));
        button->setID("frame-window-recorder-button");button->m_scaleMultiplier=1.08f;
        if(m_sideMenu && m_sideMenu->getLayout()){m_sideMenu->addChild(button);m_sideMenu->updateLayout();return;}
        auto menu=CCMenu::create();menu->setPosition({0,0});addChild(menu,1000);
        auto size=CCDirector::sharedDirector()->getWinSize();button->setPosition({size.width-28,size.height-80});menu->addChild(button);
    }
    void openRecorder(CCObject*){if(auto popup=FrameMeterTools::create(nullptr,this))popup->show();}
};
