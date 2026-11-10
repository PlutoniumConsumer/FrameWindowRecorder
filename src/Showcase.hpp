#pragma once
#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include "Analysis.hpp"

namespace showcase {
using namespace geode::prelude;
inline bool enabled(char const* key) { return Mod::get()->getSettingValue<bool>(key); }
// Dedicated channel: closing our menu must never stop the level's audio channel.
class MenuAudio {
    FMOD::Sound* sound=nullptr;
    FMOD::Channel* channel=nullptr;
public:
    ~MenuAudio(){ stop(); }
    void stop(){if(channel)channel->stop();channel=nullptr;if(sound)sound->release();sound=nullptr;}
    void volume(float gain){if(channel)channel->setVolume(gain*FMODAudioEngine::sharedEngine()->m_musicVolume);}
    void start(){
        stop();if(!enabled("menu-music"))return;
        std::filesystem::path path=CCFileUtils::sharedFileUtils()->fullPathForFilename("menuLoop.mp3",false);
        std::error_code error;
        // Read the user's installed track; never package Geometry Dash's music.
        wchar_t programs[32768]{};
        auto length=GetEnvironmentVariableW(L"ProgramFiles(x86)",programs,32768);
        if(length && length<32768){
            auto supplied=std::filesystem::path(programs)/L"Steam/steamapps/content/app_322170/depot_322171/Resources/menuLoop.mp3";
            if(std::filesystem::is_regular_file(supplied,error))path=supplied;
        }
        if(!std::filesystem::is_regular_file(path,error))return;
        auto system=FMODAudioEngine::sharedEngine()->m_system;
        if(system && system->createStream(path.string().c_str(),FMOD_LOOP_NORMAL,nullptr,&sound)==FMOD_OK) {
            if(system->playSound(sound,nullptr,true,&channel)==FMOD_OK){volume(1);channel->setPaused(false);}
        }
    }
};
inline MenuAudio menuAudio;
inline void ding(){
    if(!enabled("window-sounds"))return;
    auto path=(Mod::get()->getResourcesDir()/"window-ding.wav").string();
    FMODAudioEngine::sharedEngine()->playEffect(path,1.f,0.f,
        static_cast<float>(Mod::get()->getSettingValue<double>("ding-volume")));
}
inline void marker(PlayLayer* p,bool player1,meter::Window const* window){
    if(!enabled("input-markers") || !p->m_objectLayer)return;
    auto player=player1?p->m_player1:p->m_player2;if(!player)return;
    auto layer=p->m_objectLayer->getChildByID("input-marker-layer");
    if(!layer){layer=CCNode::create();layer->setID("input-marker-layer");p->m_objectLayer->addChild(layer,10000);}
    auto root=CCNode::create();root->setPosition(player->getPosition());
    // This is level-space, sampled immediately before the recorded press is injected.
    layer->addChild(root);
    ccColor4F color={.14f,.83f,1.f,1.f};
    auto ring=CCDrawNode::create();ring->drawCircle({0,0},19,{0,0,0,0},2.6f,color,48);root->addChild(ring);
    std::string value=window?(window->exact?std::to_string(window->width()):std::to_string(window->width())+"+"):"?";
    auto label=CCLabelBMFont::create(value.c_str(),"bigFont.fnt");label->setColor({36,212,255});
    label->setScale(.46f);label->setPosition({-32,23});root->addChild(label);
    float duration=static_cast<float>(Mod::get()->getSettingValue<double>("marker-seconds"));
    if(enabled("ui-animations"))ring->runAction(CCSequence::create(CCScaleTo::create(.12f,1.12f),CCScaleTo::create(.12f,1.f),nullptr));
    root->runAction(CCSequence::create(CCDelayTime::create(duration),CCRemoveSelf::create(),nullptr));
}
}
