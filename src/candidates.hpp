// candidates.hpp -- the discrete set of antenna positions the optimiser may choose from.
//
// Antennas must lie on a footprint boundary, so a candidate is an (edge, parameter) pair.
// Two families are generated:
//
//   * every polygon vertex.  A convex corner has the widest angular aperture of any point
//     on the boundary and, under Definition 1, sees both of its incident walls in full.
//     Emitting the vertex coordinate verbatim also sidesteps any argument about whether
//     the submitted point really lies on the boundary.
//
//   * interior points along each edge at a fixed spacing.  A point halfway down a long
//     block can look straight down a cross-street that neither of its corners can see.
#pragma once

#include <cstdint>
#include <vector>

#include "domain.hpp"

namespace gis {

struct Candidate {
    uint32_t edge;  // host edge index
    double t;       // parameter along the edge; t == 0 means exactly at its start vertex
};

struct CandidateParams {
    bool include_vertices = true;
    // Approximate spacing in metres between interior samples on an edge; <= 0 disables.
    double edge_spacing = 4.0;
    // Edges shorter than this contribute their vertex only.
    double min_edge_for_interior = 1.0;
};

std::vector<Candidate> generateCandidates(const Domain& dom, const CandidateParams& p);

// World-space position of a candidate, in the original (untranslated) coordinates.
Vec2 candidateWorldPos(const Domain& dom, const Candidate& c);

// Inverse of candidateWorldPos: map a world coordinate back onto the boundary.
//
// The visibility engine has to know which building an antenna stands on so it can block
// that building's interior wedge, and a bare point does not carry that; snapping to the
// nearest edge recovers it.  A point landing on a vertex snaps to that vertex exactly
// (t == 0), because the vertex case is what produces the corner self-coverage the whole
// approach depends on and a t of 1e-16 instead of 0 would quietly switch it off.
// `dist_out`, when non-null, receives how far the input was from the boundary.
Candidate snapToBoundary(const Domain& dom, const Vec2& world, double* dist_out);

// Local (translated) position, used for all geometry.
inline Vec2 candidateLocalPos(const Domain& dom, const Candidate& c) {
    const Vec2 a = dom.edgeA(c.edge);
    if (c.t == 0.0) return a;
    return a + (dom.edgeB(c.edge) - a) * c.t;
}

}  // namespace gis
