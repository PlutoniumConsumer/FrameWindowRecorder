#pragma once
#include <cmath>
#include <cstdint>
namespace meter {
class PhysicsClock {
    std::uint32_t m_tick=0;
public:
    void reset(){m_tick=0;}
    std::uint32_t tick() const{return m_tick;}
    static bool supportedStep(float dt){return std::isfinite(dt)&&std::abs(double(dt)-1./240.)<.000001;}
    void advance(){++m_tick;}
};
}
