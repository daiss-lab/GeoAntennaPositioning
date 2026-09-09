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
    double claim_band = 0.0;
    bool claim_all = false;
    uint32_t repair_sample = 2000;
    uint32_t pool_cap = 6000;
    int replicas = 1;
    double sync_interval = 90.0;
    double phi_exponent = -1.0;
    double difficulty_beta = 0.0;
    uint32_t difficulty_cap = 0;
    uint32_t difficulty_width = 64;
    uint32_t bundles = 3;
    uint32_t bundle_own_cap = 40;
    double grasp_prob = 0.35;
    uint32_t grasp_width = 6;
    int exact_lead = 0;
    int exact_shortlist = 8;
    std::string dump_cov;
    long long explain_building_id = -1;
    int destroy_min = 2;
    int destroy_max = 8;
    bool target_destroy = true;
    uint32_t target_sample = 64;
    uint32_t target_pool = 768;
    std::string share_dir;
    int island = 0;
    int immigrants = 2;
    double slot_margin = 0.0;
    double slot_margin_cap = 0.08;
    double slot_margin_quantile = 0.80;
    double slot_margin_floor_slots = 2.0;
    double slot_margin_trigger = 0.005;
};

int runSolve(const SolveOptions& o);

}
