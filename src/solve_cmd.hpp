// solve_cmd.hpp -- options for the `solve` subcommand.
#pragma once

#include <cstdint>
#include <string>

namespace gis {

struct SolveOptions {
    std::string data;
    std::string cache;
    std::string out;
    double tau = 0.5;
    uint32_t k = 100;
    double time_budget = 60.0;
    // Absolute wall-clock stop, as a unix timestamp; 0 disables.  Whichever of this and
    // `time_budget` comes first wins.  This is what makes a 24-hour submission window
    // safe: the job is told when the window closes, not how long it may run.
    double deadline_epoch = 0.0;
    uint64_t seed = 1;
    double cell = 25.0;
    double slot_len = 0.1;
    uint32_t min_slots = 128;
    uint32_t max_slots = 65536;
    int bins = 1024;
    bool strict = false;
    bool include_interior_rings = false;
    double eval_max_radius = 0.0;
    // Emitted claim list widens below tau; ranking still uses the honest count.
    double claim_band = 0.0;
    bool claim_all = false;
    uint32_t repair_sample = 2000;
    uint32_t pool_cap = 6000;
    int replicas = 1;
    double sync_interval = 90.0;
    // Negative means "not set on the command line"; the default then follows tau.  See
    // the resolution in runSolve and explanation.md §8.4.
    double phi_exponent = -1.0;
    // Difficulty weighting of the surrogate; see OptimizeParams.  0 disables.
    double difficulty_beta = 0.0;
    uint32_t difficulty_cap = 0;
    uint32_t difficulty_width = 64;
    uint32_t bundles = 3;
    uint32_t bundle_own_cap = 40;
    double grasp_prob = 0.35;
    uint32_t grasp_width = 6;
    // Replicas whose best solution is re-scored exactly at each synchronisation point, so
    // the leader that gets copied into the trailing replicas is the genuinely best one.
    // 0 keeps Solution04's behaviour of trusting the slot score, and 0 is the default
    // until the A/B in slurm/leadsweep.sbatch reports: the argument for this is strong but
    // it is still only an argument, it changes the search dynamics rather than just the
    // final pick, and it spends about 4% of the budget.  Raising --shortlist is the safe
    // half of the same idea and is on by default, because it runs after the search has
    // finished and takes the max over strictly more candidates -- it cannot choose worse.
    int exact_lead = 0;
    // How many of the replicas' solutions to score exactly before choosing one.
    int exact_shortlist = 8;
    std::string dump_cov;
    long long explain_building_id = -1;
    int destroy_min = 2;
    int destroy_max = 8;
    // Fourth destroy shape: aim the repair at a building sitting just below tau.
    bool target_destroy = true;
    uint32_t target_sample = 64;
    uint32_t target_pool = 768;
    // Island model: a directory every process solving this same sub-problem can read and
    // write, so many independent jobs on many nodes become one search.  Empty disables it.
    std::string share_dir;
    int island = 0;
    int immigrants = 2;
    // Asks the search for tau*(1+slot_margin) rather than tau, to offset the slot model's
    // optimism.  Negative means measure it from a greedy calibration run instead of taking
    // a number on faith; see runSolve.  Zero disables the mechanism.
    double slot_margin = 0.0;
    // Ceiling on a measured margin.  A margin large enough to be unreachable turns
    // buildings into money pits: the search keeps spending antennas on something it can
    // never mark finished.
    double slot_margin_cap = 0.08;
    // Quantile of the measured shortfall distribution the margin is taken at.
    double slot_margin_quantile = 0.80;
    // Floor, in slots on a median building.  A margin that moves `required` by less than a
    // whole slot changes no decision the search makes.
    double slot_margin_floor_slots = 2.0;
    // Minimum over-claimed fraction of the serviced set before a margin is applied at all.
    double slot_margin_trigger = 0.005;
};

int runSolve(const SolveOptions& o);

}  // namespace gis
