#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "coverage.hpp"
#include "domain.hpp"
#include "viscache.hpp"

namespace gis {

struct Solution {
    std::vector<uint32_t> cand_idx;
    uint32_t score = 0;
    double phi = 0.0;
};

struct OptimizeParams {
    double tau = 0.5;
    uint32_t k = 100;
    uint64_t seed = 1;
    double time_budget_sec = 60.0;
    int destroy_min = 2;
    int destroy_max = 8;
    bool target_destroy = true;
    uint32_t target_sample = 64;
    uint32_t target_pool = 768;
    uint32_t repair_sample = 2000;
    uint32_t pool_cap = 6000;
    double completion_weight = 1e6;
    int replicas = 1;
    double sync_interval_sec = 90.0;
    double phi_exponent = 1.0;

    double difficulty_beta = 0.0;
    uint32_t difficulty_cap = 0;
    uint32_t difficulty_width = 64;
    uint32_t bundles_per_building = 3;
    uint32_t bundle_own_cap = 40;
    double grasp_prob = 0.35;
    uint32_t grasp_width = 6;
    bool verbose = true;

    std::string share_dir;
    int island = 0;
    int immigrants = 2;

    std::function<uint32_t(const std::vector<uint32_t>&)> exact_score;
    int exact_top = 0;

    std::function<void(const Solution&)> on_sync;
};

struct SearchState {
    std::vector<uint32_t> cov;
    std::vector<uint16_t> cnt;
    std::vector<uint64_t> bits;
    std::vector<uint8_t> chosen;
    std::vector<uint32_t> sel;
    uint32_t score = 0;
    double phi = 0.0;

    std::vector<uint32_t> touch_b, touch_d, touch_stamp;
    uint32_t touch_epoch = 0;

    void init(uint32_t n_buildings, uint32_t n_slots, uint32_t n_cands);
    void clear();
};

class Optimizer {
public:
    Optimizer(const Domain& dom, const SlotLayout& layout, const VisCache& cache);

    uint32_t setThreshold(double tau, double margin = 0.0);

    std::vector<Solution> solve(const OptimizeParams& p);

    std::vector<double> slotRatios(const std::vector<uint32_t>& sel) const;

    uint32_t numCandidates() const { return n_cands_; }

private:
    struct Bundle {
        uint32_t c0, c1;
        uint32_t target;
    };

    const Domain& dom_;
    const SlotLayout& layout_;
    const VisCache& cache_;

    uint32_t n_buildings_ = 0;
    uint32_t n_cands_ = 0;
    std::vector<uint32_t> req_;
    std::vector<uint32_t> active_;

    std::vector<double> w_;
    bool w_flat_ = true;
    std::vector<uint16_t> difficulty_;

    std::vector<uint32_t> bc_start_, bc_items_;
    std::vector<uint32_t> bc_cnt_;
    std::vector<uint32_t> cb_start_, cb_items_;
    std::vector<uint32_t> own_start_, own_items_;
    std::vector<uint32_t> bundle_start_;
    std::vector<Bundle> bundles_;
    std::vector<Vec2> cand_pos_;

    static const uint32_t kPhiTableSize = 4096;
    std::vector<double> phi_table_;
    bool phi_linear_ = true;

    void buildIndices();
    void buildOwnIndex();
    void buildBundles(const OptimizeParams& p);
    void setPhiShape(double exponent);
    double applyDifficulty(const OptimizeParams& p);

    inline double phiOf(uint32_t b, uint32_t cov, uint32_t req) const {
        const uint32_t c = cov < req ? cov : req;
        const double x = double(c) / double(req);
        const double g = phi_linear_ ? x : phi_table_[uint32_t(x * double(kPhiTableSize - 1) + 0.5)];
        return w_flat_ ? g : g * w_[b];
    }

    void addCandidate(SearchState& st, uint32_t ci) const;
    void removeCandidate(SearchState& st, uint32_t ci) const;
    uint32_t countNewSlots(const SearchState& st, uint32_t start, uint32_t len) const;
    void gain(SearchState& st, uint32_t ci, double* dphi, int* dscore) const;
    void gainPair(SearchState& st, uint32_t c0, uint32_t c1, double* dphi, int* dscore) const;
    static double value(double dphi, int dscore, double w) { return dscore * w + dphi; }

    void greedy(const OptimizeParams& p, SearchState& st, uint64_t& rng_state,
                bool randomised) const;
    long long lnsRound(const OptimizeParams& p, SearchState& st, Solution& best,
                       uint64_t& rng_state, double until) const;
    void loadSelection(SearchState& st, const std::vector<uint32_t>& sel) const;
};

}
