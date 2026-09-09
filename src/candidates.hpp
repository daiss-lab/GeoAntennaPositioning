#pragma once

#include <cstdint>
#include <vector>

#include "domain.hpp"

namespace gis {

struct Candidate {
    uint32_t edge;
    double t;
};

struct CandidateParams {
    bool include_vertices = true;
    double edge_spacing = 4.0;
    double min_edge_for_interior = 1.0;
};

std::vector<Candidate> generateCandidates(const Domain& dom, const CandidateParams& p);

Vec2 candidateWorldPos(const Domain& dom, const Candidate& c);

Candidate snapToBoundary(const Domain& dom, const Vec2& world, double* dist_out);

inline Vec2 candidateLocalPos(const Domain& dom, const Candidate& c) {
    const Vec2 a = dom.edgeA(c.edge);
    if (c.t == 0.0) return a;
    return a + (dom.edgeB(c.edge) - a) * c.t;
}

}
