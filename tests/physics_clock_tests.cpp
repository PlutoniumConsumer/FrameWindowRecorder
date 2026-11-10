#include "../src/PhysicsClock.hpp"
#include <stdexcept>
#include <limits>
void check(bool ok){if(!ok)throw std::runtime_error("Physics clock regression");}
int main(){meter::PhysicsClock c;
    check(c.tick()==0&&c.supportedStep(1.f/240));
    for(unsigned render=0;render<100;++render)for(unsigned step=0;step<4;++step){check(c.tick()==render*4+step);c.advance();}
    check(c.tick()==400);c.reset();check(c.tick()==0);
    check(!c.supportedStep(1.f/60)&&!c.supportedStep(1.f/480)&&!c.supportedStep(0)&&!c.supportedStep(std::numeric_limits<float>::quiet_NaN()));
}
