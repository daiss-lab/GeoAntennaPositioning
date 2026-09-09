// optimize.hpp -- choosing which k candidates to switch on.
//
// The score counts buildings whose covered fraction reaches tau, which is a step
// function: below the threshold a building is worth nothing, at it, one point.  Greedy
// search directly on that score stalls badly -- at tau = 0.75 no single antenna ever
// completes a building, so every first move looks worthless and the search never starts.
//
// The optimiser therefore works on a clipped surrogate
//
//     Phi(S) = sum_b  g( min(covered_b(S), required_b) / required_b )
//
// which credits partial progress.  With g the identity this is a clipped coverage
// function: monotone submodular, so Minoux's lazy greedy applies and the usual (1 - 1/e)
// argument holds for Phi.  `phi_exponent` can bend g upwards (g(x) = x^gamma) to
// concentrate effort on buildings that are close to finishing; that trades the guarantee
// for a heuristic, which is why it defaults to the identity.
//
// Three things sit on top of the surrogate, each aimed at a specific way the plain
// version leaves score on the table:
//
//   * Bundle moves.  A single antenna cannot finish a building at tau = 0.75, but two on
//     opposite corners finish thousands of them.  Greedy that can only add one antenna at
//     a time never sees that; it is offered pre-computed antenna *pairs* as single moves,
//     ranked per antenna spent, so a completion two moves deep is visible one move out.
//
//   * Randomised greedy.  The plain greedy is deterministic, so every replica used to
//     build the same starting solution and they differed only in where LNS wandered from
//     it.  Replicas now sample among the near-best moves, so they start in different
//     basins, which is what makes running many of them worth the cores.
//
//   * Large-neighbourhood search with a structured destroy step.  Tearing out antennas
//     that are spatially close, or that cover little no one else covers, leaves a hole
//     the repair can actually do something with; scattered random removals mostly get
//     put straight back.
//
// Immutable data (the visibility cache, the building index, the per-building
// requirement) lives in Optimizer; everything the search mutates lives in SearchState,
// so many independent replicas can run concurrently on separate cores.  Replicas are no
// longer fully independent: at fixed intervals the best solution found anywhere is copied
// into the replicas that are trailing, which turns 16 separate searches into one search
// with 16 fronts.
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
    // Antennas torn out per LNS iteration, sampled uniformly from this range.
    int destroy_min = 2;
    int destroy_max = 8;
    // Adds a fourth destroy shape that picks a building sitting just below tau and steers
    // the repair at it.  The score is a step function, so all of the reachable gain lives
    // in buildings a little short of threshold -- and none of the other three shapes aims
    // anywhere in particular.  0 disables it, restoring the three-mode behaviour.
    bool target_destroy = true;
    // Buildings sampled when looking for the one closest to tipping over.  Sampling rather
    // than scanning keeps the cost independent of how many buildings there are.
    uint32_t target_sample = 64;
    // Candidates from the target building's own list added to the repair pool.  This is
    // deliberately far below pool_cap: a popular building is visible from tens of
    // thousands of candidates, every one of them gets scored by the repair, and the ones
    // worth having are a small set.  Adding pool_cap of them roughly doubles the cost of
    // every targeted iteration to widen a net that was already wide enough.
    uint32_t target_pool = 768;
    // Random candidates mixed into each repair, on top of those touching the buildings
    // the destroy step disturbed.
    uint32_t repair_sample = 2000;
    // Upper bound on how many candidates a single repair may examine.
    uint32_t pool_cap = 6000;
    // Weight given to completing a building relative to partial progress.
    double completion_weight = 1e6;
    // Independent searches run concurrently.
    int replicas = 1;
    // Seconds between synchronisation points.  At each one the best solution found by any
    // replica is copied into the trailing ones and the caller's callback is invoked.
    double sync_interval_sec = 90.0;
    // Bends the partial-progress reward: g(x) = x^phi_exponent.  1.0 keeps Phi submodular.
    double phi_exponent = 1.0;

    // Difficulty weighting.  Phi credits partial progress on every building equally, but a
    // building that needs six antennas to itself is worth a sixth as much per antenna as
    // one that needs a single corner.  At tau = 0.75 roughly half the buildings are never
    // going to be serviced at any k this problem uses, and under a flat Phi their partial
    // coverage is still a large share of the surrogate -- so the ranking that decides every
    // greedy step and every repair is dominated by buildings that cannot score.
    //
    // `difficulty_beta` reweights the surrogate by w_b = (1 / m_b)^beta, where m_b is the
    // number of antennas a greedy set-cover needs to service building b *on its own*.
    // 0 disables it and restores the flat Phi exactly.  Weights are normalised to mean 1
    // over the active buildings so `completion_weight` keeps its meaning.
    double difficulty_beta = 0.0;
    // Buildings needing more than this many antennas to themselves are dropped from the
    // objective outright.  0 disables.  This is deliberately separate from the weighting:
    // down-weighting still lets a building be finished for free when an antenna placed for
    // a neighbour happens to cover it, whereas exclusion forgoes that.
    uint32_t difficulty_cap = 0;
    // Candidates examined per building when measuring m_b, best-first by how much of that
    // building they cover.  The measurement is a one-off, parallel over buildings.
    uint32_t difficulty_width = 64;
    // Candidate pairs kept per building as bundle moves; 0 disables bundles entirely.
    uint32_t bundles_per_building = 3;
    // Cap on how many of a building's own candidates are considered when forming pairs.
    uint32_t bundle_own_cap = 40;
    // Fraction of greedy steps that pick uniformly among the top `grasp_width` moves
    // instead of the single best.  Replica 0 always runs at 0, so the deterministic
    // greedy result is never lost.
    double grasp_prob = 0.35;
    uint32_t grasp_width = 6;
    bool verbose = true;

    // Island model.  Replica sharing turns the cores of one node into one search; this
    // turns the nodes of the cluster into one search.  When `share_dir` is set, each
    // process publishes its leader there at every synchronisation point and reads what the
    // other islands have published, adopting anything better than its own into a trailing
    // replica.  The medium is the shared filesystem -- no MPI, so an island that dies, is
    // pre-empted, or never starts costs the others nothing but a file that stops changing.
    std::string share_dir;
    int island = 0;
    // Foreign elites pulled in per synchronisation point.  More than a couple and the
    // islands converge onto one trajectory, which is the failure mode replica sharing
    // already had to be tuned away from.
    int immigrants = 2;

    // Exact score of a selection, when the caller can supply one.  The search ranks by the
    // slot model, which is a proxy: measured over 291 historical shortlists, the placement
    // with the best slot score was also the best under the exact evaluator only 53% of the
    // time, and the true winner sat as deep as rank 7 of 8.  Every synchronisation point
    // copies "the leader" into the trailing replicas, so for roughly half of them the search
    // has been propagating the wrong solution and discarding a better one.
    //
    // When this is set, the top `exact_top` replicas by slot score are evaluated exactly and
    // the leader is chosen on that instead.  Results are cached per replica against its slot
    // score and phi, so a replica that did not move between rounds is not re-evaluated.
    std::function<uint32_t(const std::vector<uint32_t>&)> exact_score;
    int exact_top = 0;

    // Called at each synchronisation point with the best solution so far, from a single
    // thread.  Used to checkpoint a valid answer to disk long before the budget runs out.
    std::function<void(const Solution&)> on_sync;
};

// Everything one search replica mutates.
struct SearchState {
    std::vector<uint32_t> cov;   // slots currently covered, per building
    std::vector<uint16_t> cnt;   // per-slot multiplicity, so removal is exact
    std::vector<uint64_t> bits;  // per-slot covered flag, for popcount gains
    std::vector<uint8_t> chosen;
    std::vector<uint32_t> sel;
    uint32_t score = 0;
    double phi = 0.0;

    // gain() scratch
    std::vector<uint32_t> touch_b, touch_d, touch_stamp;
    uint32_t touch_epoch = 0;

    void init(uint32_t n_buildings, uint32_t n_slots, uint32_t n_cands);
    void clear();
};

class Optimizer {
public:
    Optimizer(const Domain& dom, const SlotLayout& layout, const VisCache& cache);

    // Buildings that cannot reach tau even with every candidate switched on are dropped
    // from the objective, so the search never spends antennas chasing them.  Returns how
    // many were excluded.
    //
    // `margin` asks the search for more slots than tau strictly needs.  The slot model is
    // optimistic -- the midpoint rule swallows shadow gaps narrower than one slot, so the
    // search believes it has finished buildings the exact evaluator says are short.  A
    // margin buys back that bias without touching the discretisation: aim at
    // tau*(1+margin), so a building the search calls finished is genuinely finished.  It
    // never causes a building to be excluded (exclusion still tests plain tau) and never
    // asks for more than every candidate together can deliver.
    uint32_t setThreshold(double tau, double margin = 0.0);

    // Every replica's best solution, sorted best-first by the *slot* score.  The caller
    // gets all of them rather than just the winner because the slot model overstates
    // coverage by a few per cent and does not rank two placements the same way the exact
    // evaluator does; the real choice is made by evaluating these exactly, which costs
    // about a second each.  Never empty.
    std::vector<Solution> solve(const OptimizeParams& p);

    // Slot-model coverage ratios for a selection; reporting only.  The authoritative
    // figures come from evaluate.cpp.
    std::vector<double> slotRatios(const std::vector<uint32_t>& sel) const;

    uint32_t numCandidates() const { return n_cands_; }

private:
    // A pair of candidates offered to the search as one move.  Both sit on the boundary
    // of `target`, and together they cover more of it than either does alone -- usually
    // enough to finish it.
    struct Bundle {
        uint32_t c0, c1;
        uint32_t target;
    };

    const Domain& dom_;
    const SlotLayout& layout_;
    const VisCache& cache_;

    uint32_t n_buildings_ = 0;
    uint32_t n_cands_ = 0;
    std::vector<uint32_t> req_;  // slots needed; 0 marks a building as excluded
    std::vector<uint32_t> active_;  // the buildings with req_ != 0, for sampling

    // Per-building surrogate weight; all 1.0 unless difficulty weighting is on.
    std::vector<double> w_;
    bool w_flat_ = true;
    // Antennas a greedy set-cover needs to service each building alone; 0 = not measured.
    std::vector<uint16_t> difficulty_;

    // building -> candidates that cover any of its slots
    std::vector<uint32_t> bc_start_, bc_items_;
    // slots of that building each of those candidates covers, parallel to bc_items_.
    // Filled while the transpose is built, where the runs are being walked anyway.
    std::vector<uint32_t> bc_cnt_;
    // candidate -> distinct buildings it touches, so hot loops skip the slot lookups
    std::vector<uint32_t> cb_start_, cb_items_;
    // candidates standing on each building's own boundary, which is where the pairs that
    // finish it come from
    std::vector<uint32_t> own_start_, own_items_;
    // building -> its bundle moves; empty when bundles are switched off
    std::vector<uint32_t> bundle_start_;
    std::vector<Bundle> bundles_;
    // local position of each candidate, for the spatially-related destroy step
    std::vector<Vec2> cand_pos_;

    // g(x) sampled on [0,1]; see phi_exponent.  A table keeps the hot loop free of pow().
    static const uint32_t kPhiTableSize = 4096;
    std::vector<double> phi_table_;
    bool phi_linear_ = true;

    void buildIndices();
    void buildOwnIndex();
    void buildBundles(const OptimizeParams& p);
    void setPhiShape(double exponent);
    // Measures m_b for every active building and turns it into w_.  Must run after
    // setThreshold, since m_b is defined against that threshold.  Returns the mean m_b.
    double applyDifficulty(const OptimizeParams& p);

    // w_b * g(min(cov, req)/req), via the table when the shape is non-linear.
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
    // Exact joint gain of adding both candidates, by adding the first temporarily.
    void gainPair(SearchState& st, uint32_t c0, uint32_t c1, double* dphi, int* dscore) const;
    static double value(double dphi, int dscore, double w) { return dscore * w + dphi; }

    void greedy(const OptimizeParams& p, SearchState& st, uint64_t& rng_state,
                bool randomised) const;
    // Runs LNS until `until` seconds on the shared clock, updating `best` in place.
    long long lnsRound(const OptimizeParams& p, SearchState& st, Solution& best,
                       uint64_t& rng_state, double until) const;
    void loadSelection(SearchState& st, const std::vector<uint32_t>& sel) const;
};

}  // namespace gis
