#include "domain.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace gis {
namespace {

template <typename T>
void readVec(std::ifstream& fh, std::vector<T>& v, size_t n) {
    v.resize(n);
    if (n) fh.read(reinterpret_cast<char*>(v.data()), sizeof(T) * n);
    if (!fh) throw std::runtime_error("truncated input binary");
}

// Round to a multiple of 4096.  Subtracting such a value from a coordinate of a few
// hundred thousand metres is exact in IEEE-754 (the result's ulp is finer than the
// input's), so the translation is perfectly reversible.
double quantizeOrigin(double v) {
    return std::floor(v / 4096.0 + 0.5) * 4096.0;
}

}  // namespace

void Domain::load(const std::string& path) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) throw std::runtime_error("cannot open " + path);

    char magic[8];
    fh.read(magic, 8);
    if (!fh || std::memcmp(magic, "GISCUP01", 8) != 0)
        throw std::runtime_error("bad magic in " + path);

    uint32_t header[4];
    fh.read(reinterpret_cast<char*>(header), sizeof(header));
    n_buildings_ = header[0];
    n_vertices_ = header[1];
    has_interior_rings_ = (header[2] & 1u) != 0;

    readVec(fh, building_id_, n_buildings_);
    readVec(fh, vert_offset_, n_buildings_ + 1);
    readVec(fh, extra_perim_, n_buildings_);

    std::vector<double> raw;
    readVec(fh, raw, size_t(2) * n_vertices_);

    double minx = raw[0], maxx = raw[0], miny = raw[1], maxy = raw[1];
    for (uint32_t i = 0; i < n_vertices_; ++i) {
        minx = std::min(minx, raw[2 * i]);
        maxx = std::max(maxx, raw[2 * i]);
        miny = std::min(miny, raw[2 * i + 1]);
        maxy = std::max(maxy, raw[2 * i + 1]);
    }
    origin_ = Vec2(quantizeOrigin(0.5 * (minx + maxx)), quantizeOrigin(0.5 * (miny + maxy)));

    pts_.resize(n_vertices_);
    for (uint32_t i = 0; i < n_vertices_; ++i)
        pts_[i] = Vec2(raw[2 * i] - origin_.x, raw[2 * i + 1] - origin_.y);

    min_ = Vec2(minx - origin_.x, miny - origin_.y);
    max_ = Vec2(maxx - origin_.x, maxy - origin_.y);

    computeDerived();
}

void Domain::buildFromPolygons(const std::vector<std::vector<Vec2>>& polys) {
    n_buildings_ = uint32_t(polys.size());
    building_id_.resize(n_buildings_);
    vert_offset_.assign(1, 0);
    extra_perim_.assign(n_buildings_, 0.0);
    pts_.clear();
    origin_ = Vec2(0.0, 0.0);

    for (uint32_t b = 0; b < n_buildings_; ++b) {
        building_id_[b] = b + 1;
        std::vector<Vec2> ring = polys[b];
        double area2 = 0.0;
        for (size_t i = 0; i < ring.size(); ++i) {
            const Vec2& p = ring[i];
            const Vec2& q = ring[(i + 1) % ring.size()];
            area2 += p.x * q.y - q.x * p.y;
        }
        if (area2 < 0.0) std::reverse(ring.begin(), ring.end());
        for (const Vec2& p : ring) pts_.push_back(p);
        vert_offset_.push_back(uint32_t(pts_.size()));
    }
    n_vertices_ = uint32_t(pts_.size());

    min_ = max_ = pts_.empty() ? Vec2(0.0, 0.0) : pts_[0];
    for (const Vec2& p : pts_) {
        min_.x = std::min(min_.x, p.x);
        min_.y = std::min(min_.y, p.y);
        max_.x = std::max(max_.x, p.x);
        max_.y = std::max(max_.y, p.y);
    }
    computeDerived();
}

void Domain::computeDerived() {
    edge_bld_.resize(n_vertices_);
    for (uint32_t b = 0; b < n_buildings_; ++b)
        for (uint32_t v = vert_offset_[b]; v < vert_offset_[b + 1]; ++v) edge_bld_[v] = b;

    edge_len_.resize(n_vertices_);
    edge_cum_.resize(n_vertices_);
    perim_.assign(n_buildings_, 0.0);

    for (uint32_t b = 0; b < n_buildings_; ++b) {
        double acc = 0.0;
        for (uint32_t v = vert_offset_[b]; v < vert_offset_[b + 1]; ++v) {
            edge_cum_[v] = acc;
            const double len = (pts_[nextVert(v)] - pts_[v]).norm();
            edge_len_[v] = len;
            acc += len;
        }
        perim_[b] = acc;
    }
}

void Domain::buildGrid(double cell_size) {
    cell_ = cell_size;
    inv_cell_ = 1.0 / cell_size;

    const Vec2 span = max_ - min_;
    nx_ = std::max(1, int32_t(span.x * inv_cell_) + 1);
    ny_ = std::max(1, int32_t(span.y * inv_cell_) + 1);

    const size_t n_cells = size_t(nx_) * size_t(ny_);
    std::vector<uint32_t> counts(n_cells + 1, 0);

    // Two passes so the CSR arrays are sized exactly.
    auto forEachCell = [&](uint32_t e, auto&& fn) {
        const Vec2 a = edgeA(e), b = edgeB(e);
        int32_t x0 = int32_t((std::min(a.x, b.x) - min_.x) * inv_cell_);
        int32_t x1 = int32_t((std::max(a.x, b.x) - min_.x) * inv_cell_);
        int32_t y0 = int32_t((std::min(a.y, b.y) - min_.y) * inv_cell_);
        int32_t y1 = int32_t((std::max(a.y, b.y) - min_.y) * inv_cell_);
        x0 = std::max(0, std::min(nx_ - 1, x0));
        x1 = std::max(0, std::min(nx_ - 1, x1));
        y0 = std::max(0, std::min(ny_ - 1, y0));
        y1 = std::max(0, std::min(ny_ - 1, y1));
        for (int32_t y = y0; y <= y1; ++y)
            for (int32_t x = x0; x <= x1; ++x) fn(size_t(y) * nx_ + x);
    };

    for (uint32_t e = 0; e < n_vertices_; ++e)
        forEachCell(e, [&](size_t c) { ++counts[c + 1]; });
    for (size_t i = 0; i < n_cells; ++i) counts[i + 1] += counts[i];

    cell_start_ = counts;
    cell_items_.resize(counts[n_cells]);
    std::vector<uint32_t> cursor(cell_start_.begin(), cell_start_.end() - 1);
    for (uint32_t e = 0; e < n_vertices_; ++e)
        forEachCell(e, [&](size_t c) { cell_items_[cursor[c]++] = e; });
}

bool Domain::radiusExceedsGrid(double radius) const {
    const double cells = (2.0 * radius * inv_cell_ + 1.0);
    // Past this point scanning every edge is cheaper than walking the cell range.
    return cells * cells > double(n_vertices_);
}

void Domain::queryRadius(const Vec2& c, double radius, GridScratch& scratch,
                         std::vector<uint32_t>& out) const {
    int32_t x0 = int32_t((c.x - radius - min_.x) * inv_cell_);
    int32_t x1 = int32_t((c.x + radius - min_.x) * inv_cell_);
    int32_t y0 = int32_t((c.y - radius - min_.y) * inv_cell_);
    int32_t y1 = int32_t((c.y + radius - min_.y) * inv_cell_);
    x0 = std::max(0, std::min(nx_ - 1, x0));
    x1 = std::max(0, std::min(nx_ - 1, x1));
    y0 = std::max(0, std::min(ny_ - 1, y0));
    y1 = std::max(0, std::min(ny_ - 1, y1));

    for (int32_t y = y0; y <= y1; ++y) {
        const size_t row = size_t(y) * nx_;
        for (int32_t x = x0; x <= x1; ++x) {
            const size_t cell = row + x;
            for (uint32_t i = cell_start_[cell]; i < cell_start_[cell + 1]; ++i) {
                const uint32_t e = cell_items_[i];
                if (scratch.testAndSet(e)) out.push_back(e);
            }
        }
    }
}

void Domain::appendAllEdges(GridScratch& scratch, std::vector<uint32_t>& out) const {
    for (uint32_t e = 0; e < n_vertices_; ++e)
        if (scratch.testAndSet(e)) out.push_back(e);
}

}  // namespace gis
