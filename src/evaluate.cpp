#include "evaluate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace gis {
namespace {

struct BldInterval {
    uint32_t building;
    double a0;
    double a1;
    bool operator<(const BldInterval& o) const {
        if (building != o.building) return building < o.building;
        return a0 < o.a0;
    }
};

}  // namespace

EvalResult evaluateExact(const Domain& dom, const std::vector<Candidate>& antennas,
                         const EvalParams& ep, const VisibilityParams& vp_in) {
    VisibilityParams vp = vp_in;
    // Uncapped by default: the optimiser may have worked with a shorter working radius,
    // and any extra coverage found here is real coverage we are entitled to claim.
    vp.max_radius = (ep.max_radius > 0.0) ? ep.max_radius : dom.diagonal() * 1.001;
    vp.min_arc_len = 0.0;

    const VisibilityEngine eng(dom, vp);
    const size_t n = antennas.size();

#ifdef _OPENMP
    std::vector<std::vector<BldInterval>> per_thread(omp_get_max_threads());
#else
    std::vector<std::vector<BldInterval>> per_thread(1);
#endif

#pragma omp parallel
    {
#ifdef _OPENMP
        std::vector<BldInterval>& sink = per_thread[omp_get_thread_num()];
#else
        std::vector<BldInterval>& sink = per_thread[0];
#endif
        VisWorkspace ws;
        ws.init(dom.numEdges(), vp.num_bins);
        std::vector<VisArc> arcs;

#pragma omp for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)n; ++i) {
            arcs.clear();
            eng.computeArcs(antennas[i].edge, antennas[i].t, ws, arcs);
            for (const VisArc& a : arcs) {
                const double len = dom.edgeLen(a.edge);
                const double base = dom.edgeCum(a.edge);
                const uint32_t b = dom.edgeBuilding(a.edge);
                sink.push_back({b, base + a.t0 * len, base + a.t1 * len});
                if (ep.explain_building >= 0 && b == uint32_t(ep.explain_building)) {
                    const Vec2 ap = dom.toWorld(
                        dom.edgeA(antennas[i].edge) +
                        (dom.edgeB(antennas[i].edge) - dom.edgeA(antennas[i].edge)) *
                            antennas[i].t);
#pragma omp critical
                    std::fprintf(stderr,
                                 "    antenna[%lld] (%.4f,%.4f) host_edge=%u t=%.4f -> "
                                 "edge %u t[%.4f,%.4f] arclen[%.4f,%.4f] len=%.4f\n",
                                 i, ap.x, ap.y, antennas[i].edge, antennas[i].t, a.edge,
                                 a.t0, a.t1, base + a.t0 * len, base + a.t1 * len,
                                 (a.t1 - a.t0) * len);
                }
            }
        }
    }

    std::vector<BldInterval> all;
    size_t total = 0;
    for (const auto& v : per_thread) total += v.size();
    all.reserve(total);
    for (const auto& v : per_thread) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());

    EvalResult res;
    res.ratio.assign(dom.numBuildings(), 0.0);

    // One linear sweep: intervals are grouped by building and sorted by start, so the
    // union length falls out without any per-building allocation.
    size_t i = 0;
    while (i < all.size()) {
        const uint32_t b = all[i].building;
        double covered = 0.0;
        double cur_lo = all[i].a0, cur_hi = all[i].a1;
        size_t j = i + 1;
        for (; j < all.size() && all[j].building == b; ++j) {
            if (all[j].a0 > cur_hi) {
                covered += cur_hi - cur_lo;
                cur_lo = all[j].a0;
                cur_hi = all[j].a1;
            } else if (all[j].a1 > cur_hi) {
                cur_hi = all[j].a1;
            }
        }
        covered += cur_hi - cur_lo;
        const double perim = dom.scoringPerimeter(b);
        res.ratio[b] = (perim > 0.0) ? (covered / perim) : 0.0;
        i = j;
    }

    const double band = ep.knife_eps * std::max(1.0, ep.tau);
    // Anything at or above this is worth writing down even though we cannot show it clears
    // tau; see EvalParams::claim_band for why the asymmetry is in our favour.
    const double claim_floor = ep.tau - ep.claim_band * std::max(1.0, ep.tau);
    for (uint32_t b = 0; b < dom.numBuildings(); ++b) {
        const double r = res.ratio[b];
        if (r >= ep.tau + band) {
            ++res.n_safe;
            res.serviced.push_back(b);
        } else if (r >= ep.tau - band) {
            ++res.n_knife;
            res.serviced.push_back(b);
        }
    }

    if (ep.claim_all) {
        res.claims.resize(dom.numBuildings());
        for (uint32_t b = 0; b < dom.numBuildings(); ++b) res.claims[b] = b;
    } else if (ep.claim_band <= 0.0) {
        res.claims = res.serviced;
    } else {
        for (uint32_t b = 0; b < dom.numBuildings(); ++b)
            if (res.ratio[b] >= claim_floor) res.claims.push_back(b);
    }
    res.n_specul = uint32_t(res.claims.size() - res.serviced.size());
    return res;
}

}  // namespace gis
