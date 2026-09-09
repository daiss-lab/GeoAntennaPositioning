#pragma once

#include <cstdint>
#include <vector>

#include "candidates.hpp"
#include "domain.hpp"
#include "visibility.hpp"

namespace gis {

struct EvalResult {
    std::vector<double> ratio;
    std::vector<uint32_t> serviced;
    std::vector<uint32_t> claims;
    uint32_t n_safe = 0;
    uint32_t n_knife = 0;
    uint32_t n_specul = 0;
};

struct EvalParams {
    double tau = 0.5;
    double knife_eps = 1e-9;
    double claim_band = 0.0;
    bool claim_all = false;
    double max_radius = 0.0;
    long long explain_building = -1;
};

EvalResult evaluateExact(const Domain& dom, const std::vector<Candidate>& antennas,
                         const EvalParams& ep, const VisibilityParams& vp_in);

}
