#include "../src/Counter.hpp"
#include <cstdlib>
#include <iostream>
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    meter::Counter c;
    for (int i = 0; i < 60; ++i) c.observe(1.f / 60.f, true);
    check(c.updates == 60 && c.equivalent(240) == 240, "60 updates = 240 reference frames in one second");
    c.observe(10, false);
    c.observe(0, true);
    c.observe(-1, true);
    c.observe(std::numeric_limits<double>::infinity(), true);
    c.observe(std::numeric_limits<double>::quiet_NaN(), true);
    check(c.updates == 60 && c.equivalent(240) == 240, "Inactive and invalid samples ignored");
    check(c.equivalent(120) == 120, "Rate conversion must not change updates");
    c.reset();
    check(c.updates == 0 && c.seconds == 0, "Attempt reset clears state");
    c.observe(0.01, true); c.observe(0.02, true);
    check(c.updates == 2 && c.equivalent(100) == 3, "Variable dt is accumulated without per-update rounding");
    c.reset(); c.observe(1e30, true);
    check(c.equivalent(10000) == std::numeric_limits<std::uint64_t>::max(), "Conversion saturates");
    std::cout << "Counter tests passed\n";
}
