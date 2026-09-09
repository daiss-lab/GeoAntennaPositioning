#pragma once

#include <cstdint>
#include <string>

namespace gis {

struct RefineOptions {
    std::string data;
    std::string solution;
    std::string out;
    double tau = -1.0;

    int rounds = 4;
    int trials = 48;
    int event_verts = 28;
    double event_radius = 220.0;

    int relocate_edges = 48;
    double relocate_radius = 120.0;

    double time_budget_sec = 0.0;
    double deadline_epoch = 0.0;

    double eval_max_radius = 0.0;
    double knife_eps = 1e-9;
    double claim_band = 0.0;
    bool claim_all = false;
    int bins = 1024;
    double cell = 25.0;
    bool strict = false;
    bool include_interior_rings = false;
};

int runRefine(const RefineOptions& o);

}
