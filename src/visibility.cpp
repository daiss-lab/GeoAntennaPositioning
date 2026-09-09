#include "visibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gis {
namespace {

bool strictlyInsideOpenSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    if (orient(a, b, p) != 0) return false;
    const Vec2 ab = b - a;
    const double d = dot(p - a, ab);
    return d > 0.0 && d < ab.norm2();
}

bool collinearOverlap(const Vec2& c, const Vec2& x, const Vec2& a, const Vec2& b) {
    if (orient(c, x, a) != 0 || orient(c, x, b) != 0) return false;
    const Vec2 d = x - c;
    const double len2 = d.norm2();
    if (len2 <= 0.0) return false;
    double ta = dot(a - c, d) / len2;
    double tb = dot(b - c, d) / len2;
    if (ta > tb) std::swap(ta, tb);
    return std::min(tb, 1.0) - std::max(ta, 0.0) > 0.0;
}

inline bool blocks(const Vec2& c, const Vec2& x, const Vec2& a, const Vec2& b, bool strict) {
    if (properCross(c, x, a, b)) return true;
    if (!strict) return false;
    if (collinearOverlap(c, x, a, b)) return true;
    if (strictlyInsideOpenSegment(a, c, x)) return true;
    if (strictlyInsideOpenSegment(b, c, x)) return true;
    return false;
}

inline double rayHitParam(const Vec2& c, const Vec2& P, const Vec2& QP, const Vec2& d) {
    const double denom = cross(d, QP);
    if (denom == 0.0) return std::nan("");
    const double s = -cross(d, P - c) / denom;
    if (!(s > 0.0 && s < 1.0)) return std::nan("");
    const Vec2 x = P + QP * s;
    if (dot(x - c, d) <= 0.0) return std::nan("");
    return s;
}

template <typename Test>
inline void accumulateShadow(const Vec2& P, const Vec2& QP, double b0, double b1,
                             Test&& hidden, std::vector<std::pair<double, double>>& out) {
    double brk[4] = {0.0, 1.0, 0.0, 0.0};
    int n = 2;
    if (!std::isnan(b0)) brk[n++] = b0;
    if (!std::isnan(b1)) brk[n++] = b1;
    std::sort(brk, brk + n);
    for (int i = 0; i + 1 < n; ++i) {
        const double u = brk[i], v = brk[i + 1];
        if (v <= u) continue;
        if (hidden(P + QP * (0.5 * (u + v)))) out.emplace_back(u, v);
    }
}

}

bool insideWedge(const Vec2& a, const Vec2& b, const Vec2& d) {
    const int sab = crossSign(a, b);
    if (sab > 0) {
        return crossSign(a, d) > 0 && crossSign(d, b) > 0;
    }
    if (sab < 0) {
        return !(crossSign(b, d) >= 0 && crossSign(d, a) >= 0);
    }
    if (dot(a, b) < 0.0) return crossSign(a, d) > 0;
    return false;
}

void wedgeAt(const Domain& dom, uint32_t host_edge, bool at_vertex, const Vec2& c,
             Vec2& a, Vec2& b) {
    if (at_vertex) {
        const uint32_t v = host_edge;
        a = dom.pt(dom.nextVert(v)) - c;
        b = dom.pt(dom.prevVert(v)) - c;
    } else {
        a = dom.edgeB(host_edge) - c;
        b = dom.edgeA(host_edge) - c;
    }
}

bool bruteVisible(const Domain& dom, const Vec2& c, const Vec2& wedge_a, const Vec2& wedge_b,
                  const Vec2& x, bool strict) {
    const Vec2 d = x - c;
    if (d.norm2() == 0.0) return true;
    if (insideWedge(wedge_a, wedge_b, d)) return false;
    for (uint32_t e = 0; e < dom.numEdges(); ++e)
        if (blocks(c, x, dom.edgeA(e), dom.edgeB(e), strict)) return false;
    return true;
}

uint32_t bruteBlockerOf(const Domain& dom, const Vec2& c, const Vec2& x, bool strict) {
    for (uint32_t e = 0; e < dom.numEdges(); ++e)
        if (blocks(c, x, dom.edgeA(e), dom.edgeB(e), strict)) return e;
    return UINT32_MAX;
}

void VisWorkspace::init(size_t n_edges, int num_bins) {
    grid.reset(n_edges);
    seen_stamp.assign(n_edges, 0);
    seen_epoch = 0;
    bin_depth.assign(num_bins, 0.0f);
    bin_start.assign(num_bins + 1, 0);
    bin_cursor.assign(num_bins, 0);
    edges.clear();
    arc_lo.clear();
    arc_hi.clear();
    blocks.clear();
    near_dist.clear();
    far_dist.clear();
}

void VisibilityEngine::collectEdges(const Vec2& c, uint32_t host_edge, const Vec2& wedge_a,
                                    const Vec2& wedge_b, VisWorkspace& ws) const {
    const int nb = p_.num_bins;
    const double inv_binw = nb / kTwoPi;

    ws.edges.clear();
    ws.arc_lo.clear();
    ws.arc_hi.clear();
    ws.blocks.clear();
    ws.near_dist.clear();
    ws.far_dist.clear();
    ws.horizon.clear();
    ws.grid.nextEpoch();
    std::fill(ws.bin_depth.begin(), ws.bin_depth.end(),
              std::numeric_limits<float>::infinity());

    auto addDepth = [&](double lo, double span, double depth) {
        const int k0 = int(std::ceil(lo * inv_binw));
        const int k1 = int(std::floor((lo + span) * inv_binw));
        for (int k = k0; k < k1; ++k) {
            float& slot = ws.bin_depth[((k % nb) + nb) % nb];
            if (float(depth) < slot) slot = float(depth);
        }
    };

    auto addHorizon = [&](double lo, double span) {
        if (span <= 0.0) return;
        const double hi = lo + span;
        if (hi <= kTwoPi) {
            ws.horizon.emplace_back(lo, hi);
        } else {
            ws.horizon.emplace_back(lo, kTwoPi);
            ws.horizon.emplace_back(0.0, hi - kTwoPi);
        }
    };

    addDepth(normAngle(angleOf(wedge_a)), ccwSpan(angleOf(wedge_a), angleOf(wedge_b)), 0.0);

    auto ingest = [&](size_t from) {
        for (size_t i = from; i < ws.edges.size(); ++i) {
            const uint32_t e = ws.edges[i];
            const Vec2 da = dom_.edgeA(e) - c;
            const Vec2 db = dom_.edgeB(e) - c;
            const int s = crossSign(da, db);
            double lo, hi;
            if (s > 0) {
                lo = normAngle(angleOf(da));
                hi = normAngle(angleOf(db));
            } else if (s < 0) {
                lo = normAngle(angleOf(db));
                hi = normAngle(angleOf(da));
            } else {
                lo = normAngle(angleOf(da));
                hi = lo;
            }
            ws.arc_lo.push_back(lo);
            ws.arc_hi.push_back(hi);
            double tmp;
            ws.near_dist.push_back(
                float(std::sqrt(segPointDist2(dom_.edgeA(e), dom_.edgeB(e), c, &tmp))));
            ws.far_dist.push_back(float(std::max(da.norm(), db.norm())));
            const bool is_blocker = (s != 0) || p_.boundary_blocks;
            ws.blocks.push_back(is_blocker ? 1 : 0);
            if (!is_blocker || e == host_edge) continue;
            addDepth(lo, ccwSpan(lo, hi), std::max(da.norm(), db.norm()));
        }
    };

    auto horizonClosed = [&](double radius) {
        ws.horizon.clear();
        {
            const double lo = normAngle(angleOf(wedge_a));
            addHorizon(lo, ccwSpan(angleOf(wedge_a), angleOf(wedge_b)));
        }
        for (size_t i = 0; i < ws.edges.size(); ++i) {
            if (!ws.blocks[i] || ws.edges[i] == host_edge) continue;
            if (double(ws.far_dist[i]) > radius) continue;
            addHorizon(ws.arc_lo[i], ccwSpan(ws.arc_lo[i], ws.arc_hi[i]));
        }
        if (ws.horizon.empty()) return false;
        std::sort(ws.horizon.begin(), ws.horizon.end());
        double reach = 0.0;
        for (const auto& iv : ws.horizon) {
            if (iv.first > reach + 1e-12) return false;
            reach = std::max(reach, iv.second);
            if (reach >= kTwoPi - 1e-12) return true;
        }
        return reach >= kTwoPi - 1e-12;
    };

    double radius = p_.initial_radius;
    const double cap = std::min(p_.max_radius, dom_.diagonal() * 1.001);
    for (;;) {
        const size_t before = ws.edges.size();
        if (radius >= cap) {
            if (dom_.radiusExceedsGrid(cap)) {
                dom_.appendAllEdges(ws.grid, ws.edges);
                ws.valid_radius = std::numeric_limits<double>::infinity();
            } else {
                dom_.queryRadius(c, cap, ws.grid, ws.edges);
                ws.valid_radius = cap;
            }
            ingest(before);
            return;
        }
        dom_.queryRadius(c, radius, ws.grid, ws.edges);
        ingest(before);
        if (horizonClosed(radius)) {
            ws.valid_radius = radius;
            return;
        }
        radius = std::min(radius * 2.0, cap);
    }
}

void VisibilityEngine::computeArcs(uint32_t host_edge, double t, VisWorkspace& ws,
                                   std::vector<VisArc>& out) const {
    const Vec2 a = dom_.edgeA(host_edge);
    const Vec2 b = dom_.edgeB(host_edge);
    const Vec2 c = (t == 0.0) ? a : a + (b - a) * t;
    computeArcsAt(c, host_edge, t == 0.0, ws, out);
}

void VisibilityEngine::computeArcsAt(const Vec2& c, uint32_t host_edge, bool at_vertex,
                                     VisWorkspace& ws, std::vector<VisArc>& out) const {
    Vec2 wedge_a, wedge_b;
    wedgeAt(dom_, host_edge, at_vertex, c, wedge_a, wedge_b);

    collectEdges(c, host_edge, wedge_a, wedge_b, ws);

    const int nb = p_.num_bins;
    const double inv_binw = nb / kTwoPi;
    const size_t m = ws.edges.size();

    std::fill(ws.bin_start.begin(), ws.bin_start.end(), 0);
    auto forEachOverlapBin = [&](double lo, double hi, auto&& fn) {
        const double span = ccwSpan(lo, hi);
        int k0 = int(lo * inv_binw);
        if (k0 >= nb) k0 = nb - 1;
        int cnt = int(span * inv_binw) + 2;
        if (cnt > nb) cnt = nb;
        for (int j = 0; j < cnt; ++j) fn((k0 + j) % nb);
    };

    size_t total = 0;
    for (size_t i = 0; i < m; ++i) {
        if (!ws.blocks[i]) continue;
        forEachOverlapBin(ws.arc_lo[i], ws.arc_hi[i], [&](int k) {
            ++ws.bin_start[k + 1];
            ++total;
        });
    }
    for (int k = 0; k < nb; ++k) ws.bin_start[k + 1] += ws.bin_start[k];
    ws.bin_items.resize(total);
    std::copy(ws.bin_start.begin(), ws.bin_start.end() - 1, ws.bin_cursor.begin());
    for (size_t i = 0; i < m; ++i) {
        if (!ws.blocks[i]) continue;
        forEachOverlapBin(ws.arc_lo[i], ws.arc_hi[i],
                          [&](int k) { ws.bin_items[ws.bin_cursor[k]++] = uint32_t(i); });
    }

    const bool strict = p_.boundary_blocks;

    for (size_t ti = 0; ti < m; ++ti) {
        const uint32_t te = ws.edges[ti];

        if (double(ws.near_dist[ti]) > ws.valid_radius) continue;

        {
            const float d = ws.near_dist[ti];
            bool hidden = true;
            const double lo = ws.arc_lo[ti], hi = ws.arc_hi[ti];
            const double span = ccwSpan(lo, hi);
            int k0 = int(lo * inv_binw);
            if (k0 >= nb) k0 = nb - 1;
            int cnt = int(span * inv_binw) + 2;
            if (cnt > nb) cnt = nb;
            for (int j = 0; j < cnt && hidden; ++j)
                if (ws.bin_depth[(k0 + j) % nb] >= d) hidden = false;
            if (hidden) continue;
        }

        const Vec2 P = dom_.edgeA(te);
        const Vec2 Q = dom_.edgeB(te);
        const Vec2 QP = Q - P;

        ws.shadows.clear();

        if (std::isfinite(ws.valid_radius)) {
            const Vec2 f = P - c;
            const double qa = QP.norm2();
            const double qb = 2.0 * dot(f, QP);
            const double qc = f.norm2() - ws.valid_radius * ws.valid_radius;
            if (qa > 0.0) {
                const double disc = qb * qb - 4.0 * qa * qc;
                if (disc <= 0.0) {
                    ws.shadows.emplace_back(0.0, 1.0);
                } else {
                    const double sq = std::sqrt(disc);
                    const double s1 = (-qb - sq) / (2.0 * qa);
                    const double s2 = (-qb + sq) / (2.0 * qa);
                    if (s1 > 0.0) ws.shadows.emplace_back(0.0, std::min(1.0, s1));
                    if (s2 < 1.0) ws.shadows.emplace_back(std::max(0.0, s2), 1.0);
                }
            }
        }

        accumulateShadow(P, QP, rayHitParam(c, P, QP, wedge_a),
                         rayHitParam(c, P, QP, wedge_b),
                         [&](const Vec2& x) {
                             const Vec2 d = x - c;
                             if (d.norm2() == 0.0) return false;
                             return insideWedge(wedge_a, wedge_b, d);
                         },
                         ws.shadows);

        ++ws.seen_epoch;
        auto consider = [&](int k) {
            for (uint32_t j = ws.bin_start[k]; j < ws.bin_start[k + 1]; ++j) {
                const uint32_t bi = ws.bin_items[j];
                if (bi == ti && !strict) continue;
                const uint32_t be = ws.edges[bi];
                if (ws.seen_stamp[be] == ws.seen_epoch) continue;
                ws.seen_stamp[be] = ws.seen_epoch;
                const Vec2 A = dom_.edgeA(be);
                const Vec2 B = dom_.edgeB(be);
                accumulateShadow(P, QP, rayHitParam(c, P, QP, A - c),
                                 rayHitParam(c, P, QP, B - c),
                                 [&](const Vec2& x) {
                                     const Vec2 d = x - c;
                                     if (d.norm2() == 0.0) return false;
                                     return blocks(c, x, A, B, strict);
                                 },
                                 ws.shadows);
            }
        };
        {
            const Vec2 da = P - c, db = Q - c;
            const int s = crossSign(da, db);
            if (s == 0) {
                consider(int(normAngle(angleOf(da)) * inv_binw) % nb);
                consider(int(normAngle(angleOf(db)) * inv_binw) % nb);
            } else {
                const double lo = (s > 0) ? normAngle(angleOf(da)) : normAngle(angleOf(db));
                const double hi = (s > 0) ? normAngle(angleOf(db)) : normAngle(angleOf(da));
                forEachOverlapBin(lo, hi, consider);
            }
        }

        if (ws.shadows.empty()) {
            if (dom_.edgeLen(te) >= p_.min_arc_len) out.push_back({te, 0.0, 1.0});
            continue;
        }

        std::sort(ws.shadows.begin(), ws.shadows.end());
        const double len = dom_.edgeLen(te);
        const double min_t = (len > 0.0) ? (p_.min_arc_len / len) : 1.0;
        double cursor = 0.0;
        for (const auto& sh : ws.shadows) {
            if (sh.first > cursor + min_t) out.push_back({te, cursor, sh.first});
            cursor = std::max(cursor, sh.second);
            if (cursor >= 1.0) break;
        }
        if (cursor < 1.0 - min_t) out.push_back({te, cursor, 1.0});
    }
}

}
