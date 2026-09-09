#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "domain.hpp"
#include "visibility.hpp"

namespace gis {

struct Run {
    uint32_t start;
    uint32_t len;
};

struct SlotLayout {
    std::vector<uint32_t> slot_offset;
    std::vector<uint32_t> n_slots;
    std::vector<float> slot_width;
    uint32_t total_slots = 0;

    void build(const Domain& dom, double target_len, uint32_t min_slots, uint32_t max_slots);

    uint32_t count(uint32_t b) const { return n_slots[b]; }

    uint32_t buildingOfSlot(uint32_t slot) const {
        return uint32_t(std::upper_bound(slot_offset.begin(), slot_offset.end(), slot) -
                        slot_offset.begin() - 1);
    }

    uint32_t required(uint32_t b, double tau) const {
        const uint32_t n = count(b);
        uint32_t r = uint32_t(std::ceil(tau * double(n) - 1e-9));
        if (r == 0) r = 1;
        return r > n ? n : r;
    }
};

enum class SlotRule { kMidpoint, kConservative };

void arcsToRuns(const Domain& dom, const SlotLayout& layout, const std::vector<VisArc>& arcs,
                SlotRule rule, std::vector<uint32_t>& scratch, std::vector<Run>& out);

inline uint32_t runLength(const std::vector<Run>& runs) {
    uint32_t n = 0;
    for (const Run& r : runs) n += r.len;
    return n;
}

}
