#include "../src/UiLayout.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok){if(!ok)throw std::runtime_error("Full-screen layout bounds/spacing failed");}
int main(){
    for(auto size:{std::pair{320.f,240.f},std::pair{426.f,320.f},std::pair{480.f,270.f},std::pair{570.f,320.f},std::pair{640.f,360.f},std::pair{854.f,480.f},std::pair{1280.f,960.f}}){
        auto l=fwrui::Layout::make(size.first,size.second);
        check(l.cardWidth>=70 && l.cardHeight>=30);
        check(l.contentX>l.sidebar && l.cardsTop<l.headerBottom-40);
        for(int row=0;row<2;++row)for(int col=0;col<2;++col){
            check(l.cardX(col)>=l.contentX && l.cardX(col)+l.cardWidth<=l.width-19.9f);
            check(l.cardY(row)>=l.footer-.01f && l.cardY(row)+l.cardHeight<=l.cardsTop+.01f);
        }
        check(l.cardX(0)+l.cardWidth<l.cardX(1));
        check(l.cardY(1)+l.cardHeight<l.cardY(0));
    }
    std::cout<<"Responsive layout bounds passed for seven window sizes\n";
}
