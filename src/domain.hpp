#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.hpp"

namespace gis {

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
    void load(const std::string& path);

    void buildFromPolygons(const std::vector<std::vector<Vec2>>& polys);

    uint32_t numBuildings() const { return n_buildings_; }
    uint32_t numEdges() const { return n_vertices_; }

    int64_t buildingId(uint32_t b) const { return building_id_[b]; }
    uint32_t vertBegin(uint32_t b) const { return vert_offset_[b]; }
    uint32_t vertEnd(uint32_t b) const { return vert_offset_[b + 1]; }
    uint32_t vertCount(uint32_t b) const { return vert_offset_[b + 1] - vert_offset_[b]; }

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
    double edgeCum(uint32_t e) const { return edge_cum_[e]; }
    uint32_t edgeBuilding(uint32_t e) const { return edge_bld_[e]; }

    double perimeter(uint32_t b) const { return perim_[b]; }
    double scoringPerimeter(uint32_t b) const {
        return perim_[b] + (include_interior_rings_ ? extra_perim_[b] : 0.0);
    }
    void setIncludeInteriorRings(bool v) { include_interior_rings_ = v; }
    bool hasInteriorRings() const { return has_interior_rings_; }

    Vec2 toWorld(const Vec2& p) const { return p + origin_; }
    const Vec2& origin() const { return origin_; }

    Vec2 minCorner() const { return min_; }
    Vec2 maxCorner() const { return max_; }
    double diagonal() const { return (max_ - min_).norm(); }

    void buildGrid(double cell_size);
    double cellSize() const { return cell_; }

    void queryRadius(const Vec2& c, double radius, GridScratch& scratch,
                     std::vector<uint32_t>& out) const;

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

    double cell_ = 0.0;
    double inv_cell_ = 0.0;
    int32_t nx_ = 0, ny_ = 0;
    std::vector<uint32_t> cell_start_;
    std::vector<uint32_t> cell_items_;

    void computeDerived();
};

}
