#pragma once

#include <cstdint>
#include <vector>

#include "domain.hpp"
#include "geom.hpp"

namespace gis {

struct VisArc {
    uint32_t edge;
    double t0;
    double t1;
};

struct VisibilityParams {
    int num_bins = 1024;
    double initial_radius = 128.0;
    double max_radius = 600.0;
    double min_arc_len = 1e-6;
    bool boundary_blocks = false;
};

struct VisWorkspace {
    GridScratch grid;
    std::vector<uint32_t> edges;
    std::vector<double> arc_lo, arc_hi;
    std::vector<uint8_t> blocks;
    std::vector<float> near_dist;
    std::vector<float> far_dist;
    std::vector<float> bin_depth;
    std::vector<uint32_t> bin_start, bin_items, bin_cursor;
    std::vector<uint32_t> seen_stamp;
    uint32_t seen_epoch = 0;
    std::vector<std::pair<double, double>> shadows;
    std::vector<std::pair<double, double>> horizon;
    double valid_radius = 0.0;

    void init(size_t n_edges, int num_bins);
};

class VisibilityEngine {
public:
    VisibilityEngine(const Domain& dom, const VisibilityParams& params)
        : dom_(dom), p_(params) {}

    void computeArcs(uint32_t host_edge, double t, VisWorkspace& ws,
                     std::vector<VisArc>& out) const;

    void computeArcsAt(const Vec2& c, uint32_t host_edge, bool at_vertex,
                       VisWorkspace& ws, std::vector<VisArc>& out) const;

    const VisibilityParams& params() const { return p_; }

private:
    const Domain& dom_;
    VisibilityParams p_;

    void collectEdges(const Vec2& c, uint32_t host_edge, const Vec2& wedge_a,
                      const Vec2& wedge_b, VisWorkspace& ws) const;
};

bool insideWedge(const Vec2& a, const Vec2& b, const Vec2& d);

void wedgeAt(const Domain& dom, uint32_t host_edge, bool at_vertex, const Vec2& c,
             Vec2& a, Vec2& b);

bool bruteVisible(const Domain& dom, const Vec2& c, const Vec2& wedge_a, const Vec2& wedge_b,
                  const Vec2& x, bool strict);

uint32_t bruteBlockerOf(const Domain& dom, const Vec2& c, const Vec2& x, bool strict);

}
