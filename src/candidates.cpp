#include "candidates.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace gis {

Candidate snapToBoundary(const Domain& dom, const Vec2& world, double* dist_out) {
    const Vec2 p = world - dom.origin();

    uint32_t best_e = 0;
    double best_d2 = std::numeric_limits<double>::infinity();
    double best_t = 0.0;

    GridScratch scratch;
    scratch.reset(dom.numEdges());
    std::vector<uint32_t> edges;
    double radius = std::max(4.0 * dom.cellSize(), 32.0);
    for (int attempt = 0; attempt < 24; ++attempt) {
        edges.clear();
        scratch.nextEpoch();
        dom.queryRadius(p, radius, scratch, edges);
        for (uint32_t e : edges) {
            double t;
            const double d2 = segPointDist2(dom.edgeA(e), dom.edgeB(e), p, &t);
            if (d2 < best_d2) {
                best_d2 = d2;
                best_e = e;
                best_t = t;
            }
        }
        if (best_d2 < radius * radius || radius > dom.diagonal()) break;
        radius *= 2.0;
    }
    if (!std::isfinite(best_d2)) throw std::runtime_error("no edge found near an antenna");

    const double len = dom.edgeLen(best_e);
    const double along = best_t * len;
    if (along <= 1e-9) {
        best_t = 0.0;
    } else if (len - along <= 1e-9) {
        best_e = dom.nextVert(best_e);
        best_t = 0.0;
    }
    if (dist_out) *dist_out = std::sqrt(best_d2);
    return Candidate{best_e, best_t};
}

std::vector<Candidate> generateCandidates(const Domain& dom, const CandidateParams& p) {
    std::vector<Candidate> out;
    const uint32_t ne = dom.numEdges();
    out.reserve(size_t(ne) * 3);

    for (uint32_t e = 0; e < ne; ++e) {
        if (p.include_vertices) out.push_back({e, 0.0});

        const double len = dom.edgeLen(e);
        if (p.edge_spacing <= 0.0 || len < p.min_edge_for_interior) continue;

        const int n = int(std::llround(len / p.edge_spacing));
        for (int i = 1; i < n; ++i) out.push_back({e, double(i) / double(n)});
    }
    return out;
}

Vec2 candidateWorldPos(const Domain& dom, const Candidate& c) {
    const Vec2 a = dom.toWorld(dom.edgeA(c.edge));
    if (c.t == 0.0) return a;
    const Vec2 b = dom.toWorld(dom.edgeB(c.edge));
    return a + (b - a) * c.t;
}

}
