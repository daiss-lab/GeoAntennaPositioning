// domain.hpp -- the building set: geometry, arclength parameterisation, spatial index.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.hpp"

namespace gis {

// Scratch state for spatial queries.  One per thread; the stamp array turns duplicate
// removal into an O(1) test without clearing anything between queries.
struct GridScratch {
    std::vector<uint32_t> stamp;
    uint32_t epoch = 0;

    void reset(size_t n_edges) {
        stamp.assign(n_edges, 0);
        epoch = 0;
    }
    void nextEpoch() { ++epoch; }
    bool testAndSet(uint32_t e) {
        if (stamp[e] == epoch) return false;
        stamp[e] = epoch;
        return true;
    }
};

class Domain {
public:
    // Loads the binary produced by python/prepare.py.  Throws std::runtime_error.
    void load(const std::string& path);

    // Builds directly from rings given in local coordinates; used by the self-tests.
    // Rings are rewound to counter-clockwise here, matching load().
    void buildFromPolygons(const std::vector<std::vector<Vec2>>& polys);

    uint32_t numBuildings() const { return n_buildings_; }
    uint32_t numEdges() const { return n_vertices_; }  // one edge starts at each vertex

    int64_t buildingId(uint32_t b) const { return building_id_[b]; }
    uint32_t vertBegin(uint32_t b) const { return vert_offset_[b]; }
    uint32_t vertEnd(uint32_t b) const { return vert_offset_[b + 1]; }
    uint32_t vertCount(uint32_t b) const { return vert_offset_[b + 1] - vert_offset_[b]; }

    // Edge index e runs from vertex e to nextVert(e); edge indices equal vertex indices.
    uint32_t nextVert(uint32_t v) const {
        const uint32_t b = edge_bld_[v];
        const uint32_t hi = vert_offset_[b + 1];
        const uint32_t w = v + 1;
        return (w < hi) ? w : vert_offset_[b];
    }
    uint32_t prevVert(uint32_t v) const {
        const uint32_t b = edge_bld_[v];
        const uint32_t lo = vert_offset_[b];
        return (v > lo) ? (v - 1) : (vert_offset_[b + 1] - 1);
    }

    const Vec2& pt(uint32_t v) const { return pts_[v]; }
    Vec2 edgeA(uint32_t e) const { return pts_[e]; }
    Vec2 edgeB(uint32_t e) const { return pts_[nextVert(e)]; }
    double edgeLen(uint32_t e) const { return edge_len_[e]; }
    // Arclength of the edge's start vertex, measured from the building's first vertex.
    double edgeCum(uint32_t e) const { return edge_cum_[e]; }
    uint32_t edgeBuilding(uint32_t e) const { return edge_bld_[e]; }

    // Perimeter used for coverage ratios.  When `include_interior_rings` is set the
    // length of rings discarded at import time is added back, which reproduces
    // Shapely's Polygon.length in case the evaluator measures it that way.
    double perimeter(uint32_t b) const { return perim_[b]; }
    double scoringPerimeter(uint32_t b) const {
        return perim_[b] + (include_interior_rings_ ? extra_perim_[b] : 0.0);
    }
    void setIncludeInteriorRings(bool v) { include_interior_rings_ = v; }
    bool hasInteriorRings() const { return has_interior_rings_; }

    // Original (untranslated) coordinates, for output.  The translation is an exact
    // power-of-two multiple, so this recovers the input doubles bit-for-bit.
    Vec2 toWorld(const Vec2& p) const { return p + origin_; }
    const Vec2& origin() const { return origin_; }

    Vec2 minCorner() const { return min_; }
    Vec2 maxCorner() const { return max_; }
    double diagonal() const { return (max_ - min_).norm(); }

    // --- spatial index ------------------------------------------------------------
    void buildGrid(double cell_size);
    double cellSize() const { return cell_; }

    // Appends every edge whose cell overlaps the disk of radius `radius` around `c`.
    // Conservative: some returned edges lie outside the disk.  Duplicate-free via
    // `scratch`, which the caller must have reset and whose epoch it must advance.
    void queryRadius(const Vec2& c, double radius, GridScratch& scratch,
                     std::vector<uint32_t>& out) const;

    // True when the grid covers fewer cells than a full scan would cost.
    bool radiusExceedsGrid(double radius) const;

    void appendAllEdges(GridScratch& scratch, std::vector<uint32_t>& out) const;

private:
    uint32_t n_buildings_ = 0;
    uint32_t n_vertices_ = 0;
    bool has_interior_rings_ = false;
    bool include_interior_rings_ = false;

    std::vector<int64_t> building_id_;
    std::vector<uint32_t> vert_offset_;
    std::vector<double> extra_perim_;
    std::vector<Vec2> pts_;
    Vec2 origin_{0.0, 0.0};
    Vec2 min_{0.0, 0.0}, max_{0.0, 0.0};

    std::vector<double> perim_;
    std::vector<double> edge_len_;
    std::vector<double> edge_cum_;
    std::vector<uint32_t> edge_bld_;

    // uniform grid, CSR
    double cell_ = 0.0;
    double inv_cell_ = 0.0;
    int32_t nx_ = 0, ny_ = 0;
    std::vector<uint32_t> cell_start_;
    std::vector<uint32_t> cell_items_;

    void computeDerived();
};

}  // namespace gis
