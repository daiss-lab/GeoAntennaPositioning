#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "candidates.hpp"
#include "coverage.hpp"
#include "domain.hpp"
#include "visibility.hpp"

namespace gis {

struct VisCache {
    std::vector<Candidate> cands;
    std::vector<uint64_t> run_offset;
    std::vector<Run> runs;

    double slot_target_len = 0.0;
    uint32_t total_slots = 0;
    uint32_t num_buildings = 0;
    uint8_t boundary_blocks = 0;
    uint8_t slot_rule = 0;

    size_t numCandidates() const { return cands.size(); }
    const Run* runsOf(size_t i, uint32_t& n) const {
        n = uint32_t(run_offset[i + 1] - run_offset[i]);
        return runs.data() + run_offset[i];
    }

    void save(const std::string& path) const;
    void load(const std::string& path);
};

void buildVisCache(const Domain& dom, const SlotLayout& layout,
                   const std::vector<Candidate>& cands, const VisibilityParams& vp,
                   SlotRule rule, VisCache& out, size_t progress_every = 0);

}
