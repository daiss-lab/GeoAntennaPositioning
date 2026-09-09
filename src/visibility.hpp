// visibility.hpp -- exact visible wall fragments seen from a point on a footprint.
//
// For a candidate antenna position c the engine returns, for every building edge, the
// sub-intervals of that edge which are visible from c under Definition 1: a sight-line
// is blocked only where it meets the *interior* of a footprint.
//
// Two consequences of that definition shape the implementation:
//
//   1. A sight-line running *along* a wall is tangent, not blocking.  An antenna at a
//      convex corner therefore sees both of its own adjacent walls in full.  This is
//      worth ~50% self-coverage and is the single largest scoring effect in the problem.
//
//   2. A segment leaving c straight into its own building's interior crosses no edge
//      transversally (it starts on the boundary), so edge-crossing tests alone would
//      wrongly report it visible.  The engine therefore blocks the *interior wedge* of
//      directions at c explicitly, and uses edge crossings for everything else.
#pragma once

#include <cstdint>
#include <vector>

#include "domain.hpp"
#include "geom.hpp"

namespace gis {

// A visible sub-interval [t0, t1] of edge `edge`, parameterised along that edge.
struct VisArc {
    uint32_t edge;
    double t0;
    double t1;
};

struct VisibilityParams {
    // Angular bins used both to prune blocker/target pairs and to detect when the full
    // circle is occluded.  More bins = tighter pruning, slightly more per-candidate setup.
    int num_bins = 1024;
    // First shell radius, in metres; doubled until the horizon is fully occluded.
    double initial_radius = 128.0;
    // Hard cap on the shell radius.  In a dense city the horizon closes long before this
    // and the result is exact; where it does not (open ground at the edge of the extent)
    // the cap truncates very long sight-lines.  That only ever *understates* coverage, so
    // the optimiser stays conservative -- the final evaluator runs uncapped and reports
    // the true figure.
    double max_radius = 600.0;
    // Visible fragments shorter than this are dropped; they cannot meaningfully move a
    // coverage ratio but do cost memory.  Set to 0 to keep everything.
    double min_arc_len = 1e-6;
    // When true, any boundary contact blocks (the strict reading of visibility).  The
    // default follows Definition 1 and Figure 1: only interior intersections block.
    bool boundary_blocks = false;
};

// Per-thread scratch.  Allocated once and reused across candidates.
struct VisWorkspace {
    GridScratch grid;
    std::vector<uint32_t> edges;
    std::vector<double> arc_lo, arc_hi;   // angular extent of each collected edge
    std::vector<uint8_t> blocks;          // does this edge occlude at all
    std::vector<float> near_dist;         // closest approach from the viewer, per edge
    std::vector<float> far_dist;          // furthest endpoint from the viewer, per edge
    // Per angular bin, the distance beyond which that direction is certainly occluded.
    // A target further away than this in every bin it spans cannot be seen at all.
    std::vector<float> bin_depth;
    std::vector<uint32_t> bin_start, bin_items, bin_cursor;
    std::vector<uint32_t> seen_stamp;
    uint32_t seen_epoch = 0;
    std::vector<std::pair<double, double>> shadows;
    std::vector<std::pair<double, double>> horizon;  // merged blocked angular intervals
    // Distance out to which the collected edge set is a *complete* description of the
    // scene.  Cell queries return a square superset, so edges beyond this may be present
    // without their own occluders having been gathered; they must not be trusted.
    double valid_radius = 0.0;

    void init(size_t n_edges, int num_bins);
};

class VisibilityEngine {
public:
    VisibilityEngine(const Domain& dom, const VisibilityParams& params)
        : dom_(dom), p_(params) {}

    // Visible fragments from the point at parameter `t` along `host_edge`
    // (t == 0 means exactly at the edge's start vertex).  Results are appended to `out`.
    void computeArcs(uint32_t host_edge, double t, VisWorkspace& ws,
                     std::vector<VisArc>& out) const;

    // Direct point form, for tests and for the evaluator.  `host_edge` is still needed
    // to know which interior wedge to block.
    void computeArcsAt(const Vec2& c, uint32_t host_edge, bool at_vertex,
                       VisWorkspace& ws, std::vector<VisArc>& out) const;

    const VisibilityParams& params() const { return p_; }

private:
    const Domain& dom_;
    VisibilityParams p_;

    void collectEdges(const Vec2& c, uint32_t host_edge, const Vec2& wedge_a,
                      const Vec2& wedge_b, VisWorkspace& ws) const;
};

// True when direction d lies strictly inside the counter-clockwise wedge from a to b.
bool insideWedge(const Vec2& a, const Vec2& b, const Vec2& d);

// The interior wedge at a point on the boundary: directions strictly between `a` and
// `b` (counter-clockwise) lead straight into the host building and are opaque.
void wedgeAt(const Domain& dom, uint32_t host_edge, bool at_vertex, const Vec2& c,
             Vec2& a, Vec2& b);

// Reference visibility test, O(edges) per query.  The self-tests compare the accelerated
// engine against this so that the shell/bin machinery is validated independently of the
// geometric predicates it shares.
bool bruteVisible(const Domain& dom, const Vec2& c, const Vec2& wedge_a, const Vec2& wedge_b,
                  const Vec2& x, bool strict);

// Index of the first edge the reference test considers blocking, or UINT32_MAX.
// Diagnostic aid for the self-tests.
uint32_t bruteBlockerOf(const Domain& dom, const Vec2& c, const Vec2& x, bool strict);

}  // namespace gis
