#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.hpp"

namespace gis {

struct ParsedSolution {
    double tau = 0.0;
    uint32_t k = 0;
    std::vector<Vec2> antennas;
    std::vector<long long> claimed;
};

ParsedSolution parseSolutionFile(const std::string& path);

struct ScoreOptions {
    std::string data;
    std::string solution;
    std::string rewrite;
    double tau = -1.0;
    double eval_max_radius = 0.0;
    double knife_eps = 1e-9;
    double claim_band = 0.0;
    bool claim_all = false;
    double min_arc_len = 1e-6;
    int bins = 1024;
    double cell = 25.0;
    bool strict = false;
    bool include_interior_rings = false;
    std::string dump_cov;
    long long explain_building_id = -1;
};

int runScore(const ScoreOptions& o);

}
