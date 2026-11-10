#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace meter {
// Independent of Geode: one accepted callback is one observed update.
class Counter {
public:
    std::uint64_t updates = 0;
    long double seconds = 0;
    void reset() { updates = 0; seconds = 0; }
    void observe(double dt, bool active) {
        if (!active || !std::isfinite(dt) || dt <= 0) return;
        if (updates < std::numeric_limits<std::uint64_t>::max()) ++updates;
        seconds += static_cast<long double>(dt);
    }
    std::uint64_t equivalent(unsigned rate) const {
        auto value = seconds * rate;
        auto limit = std::numeric_limits<std::uint64_t>::max();
        if (value >= static_cast<long double>(limit)) return limit;
        // Compensate for float-dt representation near integral boundaries.
        return static_cast<std::uint64_t>(std::floor(value + 0.00001L));
    }
};
}
