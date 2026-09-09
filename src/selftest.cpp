// selftest.cpp -- correctness checks for the visibility engine.
//
// Two independent layers:
//   * hand-built scenes that pin down the tangency rule of Definition 1, because that
//     rule is what makes corner self-coverage work and it is easy to break silently;
//   * randomised scenes compared against a brute-force O(edges) reference, which
//     validates the shell expansion and angular bucketing.
#include <array>
#include <cstdio>
#include <string>
#include <random>
#include <vector>

#include "domain.hpp"
#include "visibility.hpp"

namespace gis {
namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// Total visible length on building `b` from the arcs produced for one candidate.
double visibleLengthOn(const Domain& dom, const std::vector<VisArc>& arcs, uint32_t b) {
    double total = 0.0;
    for (const VisArc& a : arcs)
        if (dom.edgeBuilding(a.edge) == b) total += (a.t1 - a.t0) * dom.edgeLen(a.edge);
    return total;
}

std::vector<Vec2> rect(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// --- 1. tangency and self-coverage -------------------------------------------------
void testSelfCoverage() {
    std::printf("[selftest] corner self-coverage and tangency\n");
    Domain dom;
    dom.buildFromPolygons({rect(0, 0, 10, 20)});
    dom.buildGrid(8.0);

    VisibilityParams vp;
    VisibilityEngine eng(dom, vp);
    VisWorkspace ws;
    ws.init(dom.numEdges(), vp.num_bins);

    // Antenna at vertex 0 of the rectangle.  Its two incident walls are 10 and 20 long,
    // so exactly (10+20)/60 = 0.5 of the perimeter must come back visible.
    std::vector<VisArc> arcs;
    eng.computeArcs(0, 0.0, ws, arcs);
    const double vis = visibleLengthOn(dom, arcs, 0);
    check(std::fabs(vis - 30.0) < 1e-9, "corner antenna sees both incident walls in full");
    check(std::fabs(dom.perimeter(0) - 60.0) < 1e-12, "perimeter is 60");

    // Mid-wall antenna sees its own wall entirely but neither neighbour wall.
    arcs.clear();
    eng.computeArcs(0, 0.5, ws, arcs);
    const double vis_mid = visibleLengthOn(dom, arcs, 0);
    check(std::fabs(vis_mid - 10.0) < 1e-9, "mid-wall antenna sees exactly its own wall");

    // Strict mode is the hedge: contact with the boundary blocks, so self-coverage goes.
    VisibilityParams strict;
    strict.boundary_blocks = true;
    VisibilityEngine eng_s(dom, strict);
    arcs.clear();
    eng_s.computeArcs(0, 0.0, ws, arcs);
    check(visibleLengthOn(dom, arcs, 0) < 1e-9, "strict mode removes self-coverage");
}

// --- 2. occlusion between two buildings --------------------------------------------
void testOcclusion() {
    std::printf("[selftest] occlusion by a neighbouring block\n");
    // A viewer on the left building, a tall wall in the middle, a target on the right.
    Domain dom;
    dom.buildFromPolygons({rect(0, 0, 10, 10), rect(20, -50, 30, 50), rect(40, 0, 50, 10)});
    dom.buildGrid(8.0);

    VisibilityParams vp;
    VisibilityEngine eng(dom, vp);
    VisWorkspace ws;
    ws.init(dom.numEdges(), vp.num_bins);

    std::vector<VisArc> arcs;
    eng.computeArcs(1, 0.0, ws, arcs);  // vertex (10,0) of the left building
    check(visibleLengthOn(dom, arcs, 2) < 1e-9,
          "far building fully hidden behind the middle wall");
    check(visibleLengthOn(dom, arcs, 1) > 1e-9, "middle wall itself is visible");
}

// --- 3. randomised comparison against brute force ----------------------------------
//
// `spread` controls how far the buildings are scattered.  The wide setting matters: it
// forces the shell expansion and the depth-buffer prune to do real work, whereas a
// compact scene fits inside the first shell and exercises neither.  A bug that let the
// engine trust edges lying outside the radius its occluders were gathered from was
// invisible until this ran at 900 m.
void testRandomAgainstBruteForce(double spread, int n_scenes, int max_buildings,
                                 const char* label) {
    std::printf("[selftest] randomised scenes vs brute force (%s)\n", label);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> pos(-spread, spread);
    std::uniform_real_distribution<double> size(2.0, 12.0);
    std::uniform_real_distribution<double> ang(0.0, kPi);

    int compared = 0, mismatches = 0;

    for (int scene = 0; scene < n_scenes; ++scene) {
        // Axis-aligned and rotated boxes, rejected when they overlap an existing one so
        // the footprints stay disjoint simple polygons as the contest guarantees.
        std::vector<std::vector<Vec2>> polys;
        std::vector<std::array<double, 4>> boxes;
        for (int attempt = 0; attempt < 4000 && int(polys.size()) < max_buildings; ++attempt) {
            const double cx = pos(rng), cy = pos(rng);
            const double w = size(rng), h = size(rng);
            const double r = w > h ? w : h;
            bool clash = false;
            for (const auto& b : boxes) {
                const double dx = cx - b[0], dy = cy - b[1];
                if (std::sqrt(dx * dx + dy * dy) < r + b[2] + 3.0) {
                    clash = true;
                    break;
                }
            }
            if (clash) continue;
            boxes.push_back({cx, cy, r, 0.0});
            const double th = ang(rng);
            const double ct = std::cos(th), st = std::sin(th);
            std::vector<Vec2> ring;
            const double hx = w * 0.5, hy = h * 0.5;
            const double corners[4][2] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
            for (auto& cn : corners)
                ring.push_back({cx + cn[0] * ct - cn[1] * st, cy + cn[0] * st + cn[1] * ct});
            polys.push_back(ring);
        }
        if (polys.size() < 4) continue;

        Domain dom;
        dom.buildFromPolygons(polys);
        dom.buildGrid(10.0);

        VisibilityParams vp;
        vp.min_arc_len = 0.0;
        VisibilityEngine eng(dom, vp);
        VisWorkspace ws;
        ws.init(dom.numEdges(), vp.num_bins);

        std::uniform_int_distribution<uint32_t> pick(0, dom.numEdges() - 1);
        for (int trial = 0; trial < 12; ++trial) {
            const uint32_t host = pick(rng);
            const bool at_vertex = (trial % 2) == 0;
            const double t = at_vertex ? 0.0 : 0.37;

            std::vector<VisArc> arcs;
            eng.computeArcs(host, t, ws, arcs);

            const Vec2 A = dom.edgeA(host), B = dom.edgeB(host);
            const Vec2 c = at_vertex ? A : A + (B - A) * t;
            Vec2 wa, wb;
            wedgeAt(dom, host, at_vertex, c, wa, wb);

            // Interval lookup per target edge.
            for (uint32_t te = 0; te < dom.numEdges(); ++te) {
                std::vector<std::pair<double, double>> iv;
                for (const VisArc& a : arcs)
                    if (a.edge == te) iv.emplace_back(a.t0, a.t1);

                const Vec2 P = dom.edgeA(te), Q = dom.edgeB(te);
                for (int k = 0; k < 17; ++k) {
                    const double s = (k + 0.5) / 17.0;
                    const Vec2 x = P + (Q - P) * s;
                    const bool bf = bruteVisible(dom, c, wa, wb, x, false);
                    bool eng_says = false;
                    double margin = 1.0;
                    for (const auto& r : iv)
                        if (s >= r.first && s <= r.second) {
                            eng_says = true;
                            margin = std::min(s - r.first, r.second - s);
                        }
                    if (!eng_says)
                        for (const auto& r : iv)
                            margin = std::min(margin, std::min(std::fabs(s - r.first),
                                                               std::fabs(s - r.second)));
                    ++compared;
                    // Disagreements are only tolerated within a hair of an arc endpoint,
                    // where the sample point itself is geometrically ambiguous.
                    if (bf != eng_says && margin > 1e-9) {
                        ++mismatches;
                        if (mismatches <= 6) {
                            const double dist = (x - c).norm();
                            const uint32_t blk = bruteBlockerOf(dom, c, x, false);
                            const bool in_wedge = insideWedge(wa, wb, x - c);
                            std::printf(
                                "    mismatch#%d host=%u t=%.2f te=%u s=%.3f brute=%d "
                                "engine=%d dist=%.2f blocker=%d wedge=%d bldg(te)=%u "
                                "bldg(blk)=%d\n",
                                mismatches, host, t, te, s, int(bf), int(eng_says), dist,
                                (blk == UINT32_MAX ? -1 : int(blk)), int(in_wedge),
                                dom.edgeBuilding(te),
                                (blk == UINT32_MAX ? -1 : int(dom.edgeBuilding(blk))));
                        }
                    }
                }
            }
        }
    }
    std::printf("  compared %d sample points, %d mismatches\n", compared, mismatches);
    check(mismatches == 0, "engine matches brute force on random scenes");
}

}  // namespace

// Cross-checks the engine against the O(edges) reference on a real dataset.  The
// synthetic scenes cannot reproduce the configurations a real city produces -- shared
// walls, courtyards, deeply non-convex footprints -- so this is the test that matters.
int runDataCheck(const std::string& path, int n_candidates, double target_radius,
                 unsigned seed, bool uncapped) {
    Domain dom;
    dom.load(path);
    dom.buildGrid(25.0);
    std::printf("[datacheck] %u buildings, %u edges\n", dom.numBuildings(), dom.numEdges());

    VisibilityParams vp;
    vp.min_arc_len = 0.0;
    // `uncapped` reproduces exactly what the final evaluator does: no working radius, so
    // the engine takes the whole-scene path instead of expanding shells.
    vp.max_radius = uncapped ? dom.diagonal() * 1.001 : target_radius * 2.0;
    VisibilityEngine eng(dom, vp);
    std::printf("[datacheck] max_radius=%.1f target_radius=%.1f\n", vp.max_radius,
                target_radius);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> pick(0, dom.numEdges() - 1);

    long long compared = 0, mismatches = 0;
    int reported = 0;

    for (int trial = 0; trial < n_candidates; ++trial) {
        const uint32_t host = pick(rng);
        const bool at_vertex = (trial % 2) == 0;
        const double t = at_vertex ? 0.0 : 0.41;
        const Vec2 A = dom.edgeA(host), B = dom.edgeB(host);
        const Vec2 c = at_vertex ? A : A + (B - A) * t;

        VisWorkspace ws;
        ws.init(dom.numEdges(), vp.num_bins);
        std::vector<VisArc> arcs;
        eng.computeArcs(host, t, ws, arcs);

        Vec2 wa, wb;
        wedgeAt(dom, host, at_vertex, c, wa, wb);

        // Only edges near the viewer are sampled, but every edge in the dataset acts as
        // a potential blocker in the reference test.
        GridScratch gs;
        gs.reset(dom.numEdges());
        gs.nextEpoch();
        std::vector<uint32_t> targets;
        dom.queryRadius(c, target_radius, gs, targets);

        for (uint32_t te : targets) {
            std::vector<std::pair<double, double>> iv;
            for (const VisArc& a : arcs)
                if (a.edge == te) iv.emplace_back(a.t0, a.t1);
            const Vec2 P = dom.edgeA(te), Q = dom.edgeB(te);
            for (int k = 0; k < 5; ++k) {
                const double s = (k + 0.5) / 5.0;
                const Vec2 x = P + (Q - P) * s;
                const bool bf = bruteVisible(dom, c, wa, wb, x, false);
                bool says = false;
                double margin = 1.0;
                for (const auto& r : iv)
                    if (s >= r.first && s <= r.second) {
                        says = true;
                        margin = std::min(s - r.first, r.second - s);
                    }
                if (!says)
                    for (const auto& r : iv)
                        margin = std::min(margin, std::min(std::fabs(s - r.first),
                                                           std::fabs(s - r.second)));
                ++compared;
                if (bf == says || margin <= 1e-9) continue;
                ++mismatches;
                if (reported < 10) {
                    ++reported;
                    const uint32_t blk = bruteBlockerOf(dom, c, x, false);
                    std::printf(
                        "  mismatch: host_edge=%u (bldg %lld) t=%.2f -> edge=%u "
                        "(bldg %lld) s=%.2f dist=%.1f brute=%d engine=%d "
                        "blocker_edge=%d (bldg %lld) wedge=%d\n",
                        host, (long long)dom.buildingId(dom.edgeBuilding(host)), t, te,
                        (long long)dom.buildingId(dom.edgeBuilding(te)), s, (x - c).norm(),
                        int(bf), int(says), (blk == UINT32_MAX ? -1 : int(blk)),
                        (blk == UINT32_MAX ? -1LL
                                           : (long long)dom.buildingId(dom.edgeBuilding(blk))),
                        int(insideWedge(wa, wb, x - c)));
                }
            }
        }
    }
    std::printf("[datacheck] compared %lld points, %lld mismatches\n", compared, mismatches);
    return mismatches == 0 ? 0 : 1;
}

int runSelfTest() {
    g_failures = 0;
    testSelfCoverage();
    testOcclusion();
    testRandomAgainstBruteForce(60.0, 30, 14, "compact, single shell");
    testRandomAgainstBruteForce(900.0, 8, 220, "wide, multi-shell");
    if (g_failures == 0)
        std::printf("[selftest] all checks passed\n");
    else
        std::printf("[selftest] %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace gis
