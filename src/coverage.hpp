// coverage.hpp -- discretisation of building perimeters into slots.
//
// The optimiser needs to union coverage from many antennas millions of times, which is
// far too slow with exact interval arithmetic.  Each perimeter is therefore cut into
// short equal-length slots and a candidate's contribution becomes a set of slot runs.
// Union is then a bitmap OR and the marginal gain is a popcount.
//
// A slot counts as covered when its *midpoint* falls inside a visible arc.  That rule is
// unbiased -- the half-slot errors at the two ends of an arc cancel on average -- unlike
// a "fully covered" rule, which would systematically understate coverage and mis-rank
// candidates.  The final answer never relies on this approximation: evaluate.cpp
// recomputes the reported coverage with exact interval unions.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "domain.hpp"
#include "visibility.hpp"

namespace gis {

// A contiguous span of covered slots in the global slot numbering.
struct Run {
    uint32_t start;
    uint32_t len;
};

struct SlotLayout {
    // Global slot numbering leaves one unused sentinel slot between buildings, so a run
    // of consecutive slot indices can never straddle two buildings.  That keeps every
    // run attributable to exactly one building without storing its id.
    std::vector<uint32_t> slot_offset;  // n_buildings + 1, global slot base per building
    std::vector<uint32_t> n_slots;      // slots actually used by each building
    std::vector<float> slot_width;      // metres per slot, per building
    uint32_t total_slots = 0;

    // `target_len` is the desired slot length in metres; the count per building is
    // clamped so tiny footprints still get a usable resolution and huge ones stay bounded.
    void build(const Domain& dom, double target_len, uint32_t min_slots, uint32_t max_slots);

    uint32_t count(uint32_t b) const { return n_slots[b]; }

    // Building owning a global slot index; slots are contiguous per building.
    uint32_t buildingOfSlot(uint32_t slot) const {
        return uint32_t(std::upper_bound(slot_offset.begin(), slot_offset.end(), slot) -
                        slot_offset.begin() - 1);
    }

    // Slots required for building `b` to reach coverage ratio `tau`.
    uint32_t required(uint32_t b, double tau) const {
        const uint32_t n = count(b);
        uint32_t r = uint32_t(std::ceil(tau * double(n) - 1e-9));
        if (r == 0) r = 1;
        return r > n ? n : r;
    }
};

// How an arc maps onto slots.
//   kMidpoint     -- slot covered when its midpoint is inside the arc.  Unbiased per
//                    arc, but it silently swallows shadow gaps narrower than a slot,
//                    which inflates coverage on facades peppered with slivers.
//   kConservative -- slot covered only when it lies wholly inside the arc.  Never
//                    over-states coverage, but it shaves a slot off each end of every
//                    wall, and a rectangle needs *exactly* half its perimeter to clear
//                    tau = 0.5.  That rounding hides the corner self-coverage effect and
//                    measured less than half the score of the midpoint rule, so the
//                    midpoint rule is the default despite its small upward bias.
enum class SlotRule { kMidpoint, kConservative };

// Converts visible arcs into sorted, coalesced slot runs.  `scratch` is reused.
void arcsToRuns(const Domain& dom, const SlotLayout& layout, const std::vector<VisArc>& arcs,
                SlotRule rule, std::vector<uint32_t>& scratch, std::vector<Run>& out);

// Total slots spanned by a run list.
inline uint32_t runLength(const std::vector<Run>& runs) {
    uint32_t n = 0;
    for (const Run& r : runs) n += r.len;
    return n;
}

}  // namespace gis
