#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>

namespace meter {
struct Input {
    std::uint32_t tick;
    bool down;
    bool player1;
};
struct Window {
    int early = 0;
    int late = 0;
    bool exact = false;
    bool wide = false;
    int width() const { return early + late + 1; }
    // Disjoint rows: 9-12, 7-8, 5-6, 4, 3, 2, 1, >12, unknown.
    int row() const {
        if (wide) return 7;
        if (!exact) return 8;
        int n = width();
        return n >= 9 ? 0 : n >= 7 ? 1 : n >= 5 ? 2 : 7 - n;
    }
};

// Searches only the contiguous successful region containing the recorded press.
// A failed gameplay trial closes a boundary. A chronology/search limit does not.
class WindowSearch {
    int m_min, m_max, m_shift = -1;
    bool m_right = false, m_leftClosed = false, m_rightClosed = false;
    bool m_leftFailed = false, m_rightFailed = false;
    Window m_result;
    void close(bool failed) {
        if (!m_right) {
            m_leftClosed = true; m_leftFailed = failed;
            m_right = true; m_shift = 1;
        } else { m_rightClosed = true; m_rightFailed = failed; }
        m_result.exact = m_leftClosed && m_rightClosed && m_leftFailed && m_rightFailed;
    }
public:
    WindowSearch(int earliestShift, int latestShift)
      : m_min(std::max(-12, earliestShift)), m_max(std::min(12, latestShift)) {}
    std::optional<int> candidate() {
        if (done()) return std::nullopt;
        if (m_shift < m_min || m_shift > m_max) close(false);
        if (done()) return std::nullopt;
        if (m_shift < m_min || m_shift > m_max) { close(false); return std::nullopt; }
        return m_shift;
    }
    void outcome(bool completed) {
        if (!completed) { close(true); return; }
        if (m_right) m_result.late = m_shift;
        else m_result.early = -m_shift;
        m_result.wide = m_result.width() > 12;
        m_shift += m_right ? 1 : -1;
    }
    bool done() const { return m_result.wide || (m_leftClosed && m_rightClosed); }
    Window result() const { return m_result; }
};

inline std::pair<int, int> shiftLimits(std::vector<Input> const& inputs, std::size_t index) {
    auto const& target = inputs[index];
    int low = -static_cast<int>(target.tick), high = 12;
    for (std::size_t i = 0; i < index; ++i)
        if (inputs[i].player1 == target.player1)
            low = std::max(low, static_cast<int>(inputs[i].tick) - static_cast<int>(target.tick) + 1);
    for (std::size_t i = index + 1; i < inputs.size(); ++i) {
        if (inputs[i].player1 == target.player1) {
            high = std::min(high, static_cast<int>(inputs[i].tick) - static_cast<int>(target.tick) - 1);
            break;
        }
    }
    return {low, high};
}

inline std::vector<Input> shifted(std::vector<Input> inputs, std::size_t index, int shift) {
    inputs[index].tick = static_cast<std::uint32_t>(static_cast<int>(inputs[index].tick) + shift);
    std::stable_sort(inputs.begin(), inputs.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
    return inputs;
}
}

