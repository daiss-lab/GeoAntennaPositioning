#include "coverage.hpp"

#include <algorithm>
#include <cmath>

namespace gis {

void SlotLayout::build(const Domain& dom, double target_len, uint32_t min_slots,
                       uint32_t max_slots) {
    const uint32_t nb = dom.numBuildings();
    slot_offset.resize(nb + 1);
    n_slots.resize(nb);
    slot_width.resize(nb);

    uint32_t acc = 0;
    for (uint32_t b = 0; b < nb; ++b) {
        slot_offset[b] = acc;
        const double perim = dom.perimeter(b);
        uint32_t n = uint32_t(std::llround(perim / target_len));
        n = std::max(min_slots, std::min(max_slots, std::max<uint32_t>(n, 1)));
        slot_width[b] = float(perim / double(n));
        n_slots[b] = n;
        acc += n + 1;
    }
    slot_offset[nb] = acc;
    total_slots = acc;
}

void arcsToRuns(const Domain& dom, const SlotLayout& layout, const std::vector<VisArc>& arcs,
                SlotRule rule, std::vector<uint32_t>& scratch, std::vector<Run>& out) {
    scratch.clear();
    out.clear();

    for (const VisArc& a : arcs) {
        const uint32_t b = dom.edgeBuilding(a.edge);
        const uint32_t n = layout.count(b);
        const double w = double(layout.slot_width[b]);
        if (w <= 0.0) continue;

        const double len = dom.edgeLen(a.edge);
        const double base = dom.edgeCum(a.edge);
        const double a0 = base + a.t0 * len;
        const double a1 = base + a.t1 * len;

        double lo_f, hi_f;
        if (rule == SlotRule::kMidpoint) {
            lo_f = std::ceil(a0 / w - 0.5);
            hi_f = std::floor(a1 / w - 0.5);
        } else {
            lo_f = std::ceil(a0 / w);
            hi_f = std::floor(a1 / w) - 1.0;
        }
        if (lo_f < 0.0) lo_f = 0.0;
        if (hi_f > double(n) - 1.0) hi_f = double(n) - 1.0;
        if (hi_f < lo_f) continue;

        const uint32_t base_slot = layout.slot_offset[b];
        const uint32_t lo = base_slot + uint32_t(lo_f);
        const uint32_t hi = base_slot + uint32_t(hi_f);
        for (uint32_t s = lo; s <= hi; ++s) scratch.push_back(s);
    }

    if (scratch.empty()) return;
    std::sort(scratch.begin(), scratch.end());
    scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());

    uint32_t start = scratch[0];
    uint32_t prev = scratch[0];
    for (size_t i = 1; i < scratch.size(); ++i) {
        if (scratch[i] == prev + 1) {
            prev = scratch[i];
            continue;
        }
        out.push_back({start, prev - start + 1});
        start = prev = scratch[i];
    }
    out.push_back({start, prev - start + 1});
}

}
