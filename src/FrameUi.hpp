#pragma once
#include <Geode/Geode.hpp>
#include "UiLayout.hpp"
#include <cmath>
namespace fwrui {
using namespace geode::prelude;
inline ccColor4F rgba(ccColor3B c,float alpha=1.f){return {c.r/255.f,c.g/255.f,c.b/255.f,alpha};}
inline constexpr ccColor3B Violet={151,108,255}, Cyan={94,218,248}, Lime={174,242,103}, Amber={255,190,83}, Coral={255,108,128};
inline void box(CCDrawNode* n,float x,float y,float w,float h,ccColor4F fill,float line=0,ccColor4F border={0,0,0,0}) {
    CCPoint v[]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};n->drawPolygon(v,4,fill,line,border);
}
inline CCNode* stopwatch(float size=32.f) {
    auto root=CCNode::create();
    root->setContentSize({size,size});
    auto path=(Mod::get()->getResourcesDir()/"recorder-icon.png").string();
    if(auto artwork=CCSprite::create(path.c_str())) {
        auto bounds=artwork->getContentSize();
        artwork->setScale(size/std::max(bounds.width,bounds.height));
        artwork->setPosition({size/2.f,size/2.f});
        root->addChild(artwork);
    }
    return root;
}
inline CCNode* closeIcon(float size=28.f) {
    auto node=CCNode::create();node->setContentSize({size,size});auto d=CCDrawNode::create();node->addChild(d);
    auto p=size/2;d->drawCircle({p,p},p-1,{.09f,.10f,.16f,1},.8f,rgba(Violet,.6f),40);
    d->drawSegment({p-4,p-4},{p+4,p+4},1.1f,{.9f,.93f,1,1});d->drawSegment({p-4,p+4},{p+4,p-4},1.1f,{.9f,.93f,1,1});return node;
}
inline CCNode* glyph(int type,ccColor3B color) {
    if(type==0)return stopwatch(23);
    auto node=CCNode::create();node->setContentSize({24,24});auto d=CCDrawNode::create();node->addChild(d);auto c=rgba(color);
    if(type==1)box(d,5,5,14,14,c);
    else if(type==2){for(int i=0;i<3;++i)box(d,4+i*6,4,3,6+i*5,c);}
    else if(type==3){CCPoint t[]={{6,3},{6,21},{21,12}};d->drawPolygon(t,3,c,0,c);}
    else if(type==4||type==5){bool in=type==4;float y=in?6.f:18.f;d->drawSegment({12,5},{12,20},1.2f,c);d->drawSegment({6,in?12.f:12.f},{12,y},1.2f,c);d->drawSegment({18,12},{12,y},1.2f,c);d->drawSegment({4,in?3.f:22.f},{20,in?3.f:22.f},1,c);}
    else if(type==6){box(d,2,5,20,14,{0,0,0,0},1,c);CCPoint t[]={{9,8},{9,16},{16,12}};d->drawPolygon(t,3,c,0,c);}
    else if(type==7){d->drawCircle({12,12},8,{0,0,0,0},1.5f,c,32);d->drawCircle({12,12},2.5f,c,0,c,16);for(int i=0;i<4;++i){float a=i*1.570796f;d->drawSegment({12+7*std::cos(a),12+7*std::sin(a)},{12+11*std::cos(a),12+11*std::sin(a)},1.5f,c);}}
    else if(type==8){box(d,3,4,18,16,{0,0,0,0},1,c);for(int i=0;i<3;++i)d->drawSegment({6,16.f-i*4},{17,16.f-i*4},.8f,c);}
    else {box(d,2,4,20,16,{0,0,0,0},1,c);CCPoint t[]={{9,8},{9,16},{16,12}};d->drawPolygon(t,3,c,0,c);}
    return node;
}
inline float settingsDiameter(CCNode* parent) {
    for(auto id:{"settings-button","options-button"})if(auto n=parent->getChildByIDRecursive(id)){
        auto s=n->getScaledContentSize();if(s.width>0)return std::clamp(s.width,24.f,44.f);
    }
    return 32.f;
}
}

