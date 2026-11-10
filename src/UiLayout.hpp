#pragma once
#include <algorithm>
namespace fwrui {
struct Layout {
    float width,height,sidebar,contentX,contentWidth,headerBottom,cardsTop,cardWidth,cardHeight,footer,gap=10.f;
    static Layout make(float w,float h) {
        Layout l{};l.width=w;l.height=h;l.sidebar=std::clamp(w*.24f,88.f,148.f);
        l.contentX=l.sidebar+24.f;l.contentWidth=w-l.contentX-20.f;
        l.headerBottom=h-(h<280.f?64.f:82.f);l.cardsTop=l.headerBottom-54.f;
        l.footer=h<280.f?38.f:48.f;l.cardWidth=(l.contentWidth-l.gap)/2;
        l.cardHeight=std::min(76.f,(l.cardsTop-l.footer-l.gap)/2);return l;
    }
    float cardX(int column) const {return contentX+column*(cardWidth+gap);}
    float cardY(int row) const {return cardsTop-row*(cardHeight+gap)-cardHeight;}
};
}
