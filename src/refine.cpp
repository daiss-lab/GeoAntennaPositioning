#include "refine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "candidates.hpp"
#include "domain.hpp"
#include "evaluate.hpp"
#include "score_cmd.hpp"
#include "visibility.hpp"

namespace gis {
namespace {

double unixNow() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
double nowSec() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

struct Seg {
    double lo, hi;
    uint32_t ant;
};

struct Contribution {
    std::vector<uint32_t> buildings;
    std::vector<std::vector<Seg>> per_bld;
};

double unionLength(const std::vector<Seg>& a, const std::vector<Seg>& b) {
    size_t i = 0, j = 0;
    double total = 0.0, cur_lo = 0.0, cur_hi = 0.0;
    bool open = false;
    while (i < a.size() || j < b.size()) {
        double lo, hi;
        if (j >= b.size() || (i < a.size() && a[i].lo <= b[j].lo)) {
            lo = a[i].lo;
            hi = a[i].hi;
            ++i;
        } else {
            lo = b[j].lo;
            hi = b[j].hi;
            ++j;
        }
        if (!open) {
            cur_lo = lo;
            cur_hi = hi;
            open = true;
        } else if (lo > cur_hi) {
            total += cur_hi - cur_lo;
            cur_lo = lo;
            cur_hi = hi;
        } else if (hi > cur_hi) {
            cur_hi = hi;
        }
    }
    if (open) total += cur_hi - cur_lo;
    return total;
}

void normalise(std::vector<Seg>& v) {
    if (v.empty()) return;
    std::sort(v.begin(), v.end(), [](const Seg& a, const Seg& b) { return a.lo < b.lo; });
    size_t w = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i].lo > v[w].hi) {
            v[++w] = v[i];
        } else if (v[i].hi > v[w].hi) {
            v[w].hi = v[i].hi;
        }
    }
    v.resize(w + 1);
}

}

int runRefine(const RefineOptions& o) {
    Domain dom;
    dom.load(o.data);
    dom.setIncludeInteriorRings(o.include_interior_rings);
    dom.buildGrid(o.cell);

    ParsedSolution sol = parseSolutionFile(o.solution);
    const double tau = (o.tau >= 0.0) ? o.tau : sol.tau;
    const size_t k = sol.antennas.size();

    std::vector<Candidate> cands;
    cands.reserve(k);
    double worst_snap = 0.0;
    for (const Vec2& a : sol.antennas) {
        double d = 0.0;
        cands.push_back(snapToBoundary(dom, a, &d));
        worst_snap = std::max(worst_snap, d);
    }

    std::fprintf(stderr, "refine: %s  tau=%.17g k=%zu  (worst snap %.3e m)\n",
                 o.solution.c_str(), tau, k, worst_snap);

    VisibilityParams vp;
    vp.num_bins = o.bins;
    vp.boundary_blocks = o.strict;
    vp.max_radius = (o.eval_max_radius > 0.0) ? o.eval_max_radius : dom.diagonal() * 1.001;
    vp.min_arc_len = 0.0;
    const VisibilityEngine eng(dom, vp);

    const uint32_t nb = dom.numBuildings();
    std::vector<double> need(nb);
    for (uint32_t b = 0; b < nb; ++b) need[b] = tau * dom.scoringPerimeter(b);
    const double band = o.knife_eps * std::max(1.0, tau);

    auto contributionOf = [&](const Candidate& c, VisWorkspace& ws,
                              std::vector<VisArc>& arcs) {
        arcs.clear();
        eng.computeArcs(c.edge, c.t, ws, arcs);
        Contribution out;
        std::vector<std::pair<uint32_t, Seg>> flat;
        flat.reserve(arcs.size());
        for (const VisArc& a : arcs) {
            const double len = dom.edgeLen(a.edge);
            const double base = dom.edgeCum(a.edge);
            flat.push_back({dom.edgeBuilding(a.edge),
                            Seg{base + a.t0 * len, base + a.t1 * len, 0}});
        }
        std::sort(flat.begin(), flat.end(),
                  [](const std::pair<uint32_t, Seg>& x, const std::pair<uint32_t, Seg>& y) {
                      return x.first < y.first;
                  });
        for (size_t i = 0; i < flat.size();) {
            size_t j = i;
            std::vector<Seg> segs;
            while (j < flat.size() && flat[j].first == flat[i].first) segs.push_back(flat[j++].second);
            out.buildings.push_back(flat[i].first);
            out.per_bld.push_back(std::move(segs));
            i = j;
        }
        return out;
    };

    std::vector<Contribution> contrib(k);
#pragma omp parallel
    {
        VisWorkspace ws;
        ws.init(dom.numEdges(), vp.num_bins);
        std::vector<VisArc> arcs;
#pragma omp for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)k; ++i)
            contrib[size_t(i)] = contributionOf(cands[size_t(i)], ws, arcs);
    }

    std::vector<std::vector<Seg>> per_bld(nb);
    for (size_t i = 0; i < k; ++i)
        for (size_t z = 0; z < contrib[i].buildings.size(); ++z) {
            const uint32_t b = contrib[i].buildings[z];
            for (Seg s : contrib[i].per_bld[z]) {
                s.ant = uint32_t(i);
                per_bld[b].push_back(s);
            }
        }

    std::vector<double> cur_len(nb, 0.0);
    std::vector<uint8_t> serviced(nb, 0);
    uint32_t score = 0;
    double total_len = 0.0;
    {
        std::vector<Seg> tmp;
        for (uint32_t b = 0; b < nb; ++b) {
            tmp = per_bld[b];
            normalise(tmp);
            double L = 0.0;
            for (const Seg& s : tmp) L += s.hi - s.lo;
            cur_len[b] = L;
            total_len += L;
            serviced[b] = (need[b] > 0.0 && L >= need[b] - band) ? 1 : 0;
            score += serviced[b];
        }
    }
    std::fprintf(stderr, "  start: %u buildings serviced\n", score);

    std::vector<double> tv;
    auto trialsFor = [&](const Candidate& cur, GridScratch& scratch,
                         std::vector<uint32_t>& edges, std::vector<Candidate>& out) {
        const uint32_t e = cur.edge;
        const double cur_t = cur.t;
        tv.clear();
        tv.push_back(0.0);
        if (cur_t != 0.0) tv.push_back(cur_t);

        const Vec2 A = dom.edgeA(e), B = dom.edgeB(e);
        const Vec2 dir = B - A;
        const Vec2 mid = A + dir * 0.5;

        edges.clear();
        scratch.nextEpoch();
        dom.queryRadius(mid, o.event_radius, scratch, edges);

        std::vector<std::pair<double, Vec2>> verts;
        verts.reserve(edges.size());
        for (uint32_t g : edges) {
            const Vec2 v = dom.edgeA(g);
            verts.push_back({(v - mid).norm2(), v});
        }
        std::sort(verts.begin(), verts.end(),
                  [](const std::pair<double, Vec2>& x, const std::pair<double, Vec2>& y) {
                      return x.first < y.first;
                  });
        const size_t m = std::min<size_t>(verts.size(), size_t(std::max(2, o.event_verts)));

        for (size_t i = 0; i < m; ++i)
            for (size_t j = i + 1; j < m; ++j) {
                const Vec2 d = verts[j].second - verts[i].second;
                const double den = cross(d, dir);
                if (std::fabs(den) < 1e-12) continue;
                const double s = -cross(d, A - verts[i].second) / den;
                if (s > 1e-9 && s < 1.0 - 1e-9) tv.push_back(s);
            }

        std::sort(tv.begin(), tv.end());
        tv.erase(std::unique(tv.begin(), tv.end(),
                             [](double x, double y) { return std::fabs(x - y) < 1e-9; }),
                 tv.end());

        const size_t want = size_t(std::max(1, o.trials));
        if (tv.size() > want) {
            std::vector<double> keep;
            keep.reserve(want);
            keep.push_back(0.0);
            const double stride = double(tv.size() - 1) / double(want - 1);
            for (size_t i = 1; i < want; ++i) {
                const double v = tv[size_t(std::llround(double(i) * stride))];
                if (v != 0.0) keep.push_back(v);
            }
            std::sort(keep.begin(), keep.end());
            keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
            tv.swap(keep);
        }

        out.clear();
        out.reserve(tv.size() + size_t(std::max(0, o.relocate_edges)));
        for (double t : tv) out.push_back(Candidate{e, t});

        if (o.relocate_edges > 0) {
            const Vec2 here = candidateLocalPos(dom, cur);
            edges.clear();
            scratch.nextEpoch();
            dom.queryRadius(here, o.relocate_radius, scratch, edges);

            std::vector<std::pair<double, uint32_t>> near;
            near.reserve(edges.size());
            for (uint32_t g : edges) {
                if (g == e) continue;
                near.push_back({(dom.edgeA(g) - here).norm2(), g});
            }
            const size_t take = std::min<size_t>(near.size(), size_t(o.relocate_edges));
            std::partial_sort(near.begin(), near.begin() + take, near.end());
            for (size_t z = 0; z < take; ++z) out.push_back(Candidate{near[z].second, 0.0});
        }
    };

    double t_end = (o.time_budget_sec > 0.0) ? nowSec() + o.time_budget_sec : 1e18;
    if (o.deadline_epoch > 0.0) {
        const double left = o.deadline_epoch - unixNow() - 30.0;
        t_end = std::min(t_end, nowSec() + std::max(0.0, left));
    }

    std::vector<std::vector<Seg>> rest_cache;
    std::unordered_map<uint32_t, size_t> rest_of;
    std::vector<Candidate> ts;
    std::vector<Contribution> trial_contrib;
    GridScratch scratch;
    scratch.reset(dom.numEdges());
    std::vector<uint32_t> qedges;
    bool out_of_time = false;

    for (int round = 0; round < o.rounds && !out_of_time; ++round) {
        uint32_t moved = 0, gained = 0;
        const double t_round = nowSec();

        for (size_t i = 0; i < k; ++i) {
            if (nowSec() > t_end) {
                out_of_time = true;
                break;
            }
            trialsFor(cands[i], scratch, qedges, ts);
            if (ts.size() <= 1) continue;

            trial_contrib.assign(ts.size(), Contribution{});
#pragma omp parallel
            {
                VisWorkspace ws;
                ws.init(dom.numEdges(), vp.num_bins);
                std::vector<VisArc> arcs;
#pragma omp for schedule(dynamic, 1)
                for (long long z = 0; z < (long long)ts.size(); ++z)
                    trial_contrib[size_t(z)] = contributionOf(ts[size_t(z)], ws, arcs);
            }

            rest_of.clear();
            rest_cache.clear();
            auto restFor = [&](uint32_t b) -> const std::vector<Seg>& {
                auto it = rest_of.find(b);
                if (it != rest_of.end()) return rest_cache[it->second];
                std::vector<Seg> v;
                v.reserve(per_bld[b].size());
                for (const Seg& s : per_bld[b])
                    if (s.ant != uint32_t(i)) v.push_back(s);
                normalise(v);
                rest_of.emplace(b, rest_cache.size());
                rest_cache.push_back(std::move(v));
                return rest_cache.back();
            };

            int best_z = -1;
            int best_dscore = 0;
            double best_dlen = 0.0;
            std::vector<Seg> merged;

            for (size_t z = 0; z < ts.size(); ++z) {
                if (ts[z].edge == cands[i].edge && ts[z].t == cands[i].t) continue;
                int dscore = 0;
                double dlen = 0.0;

                for (size_t q = 0; q < contrib[i].buildings.size(); ++q) {
                    const uint32_t b = contrib[i].buildings[q];
                    const std::vector<Seg>& rest = restFor(b);
                    const auto& tb = trial_contrib[z].buildings;
                    const auto pos = std::lower_bound(tb.begin(), tb.end(), b);
                    double L;
                    if (pos != tb.end() && *pos == b) {
                        merged = trial_contrib[z].per_bld[size_t(pos - tb.begin())];
                        normalise(merged);
                        L = unionLength(rest, merged);
                    } else {
                        L = 0.0;
                        for (const Seg& s : rest) L += s.hi - s.lo;
                    }
                    const uint8_t sv = (need[b] > 0.0 && L >= need[b] - band) ? 1 : 0;
                    dscore += int(sv) - int(serviced[b]);
                    dlen += L - cur_len[b];
                }
                for (size_t q = 0; q < trial_contrib[z].buildings.size(); ++q) {
                    const uint32_t b = trial_contrib[z].buildings[q];
                    const auto& ob = contrib[i].buildings;
                    if (std::binary_search(ob.begin(), ob.end(), b)) continue;
                    const std::vector<Seg>& rest = restFor(b);
                    merged = trial_contrib[z].per_bld[q];
                    normalise(merged);
                    const double L = unionLength(rest, merged);
                    const uint8_t sv = (need[b] > 0.0 && L >= need[b] - band) ? 1 : 0;
                    dscore += int(sv) - int(serviced[b]);
                    dlen += L - cur_len[b];
                }

                if (dscore > best_dscore || (dscore == best_dscore && dscore >= 0 &&
                                             dlen > best_dlen + 1e-9)) {
                    best_dscore = dscore;
                    best_dlen = dlen;
                    best_z = int(z);
                }
            }

            if (best_z < 0 || (best_dscore == 0 && best_dlen <= 1e-6)) continue;

            for (size_t q = 0; q < contrib[i].buildings.size(); ++q) {
                const uint32_t b = contrib[i].buildings[q];
                auto& v = per_bld[b];
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [&](const Seg& s) { return s.ant == uint32_t(i); }),
                        v.end());
            }
            contrib[i] = trial_contrib[size_t(best_z)];
            cands[i] = ts[size_t(best_z)];
            for (size_t q = 0; q < contrib[i].buildings.size(); ++q) {
                const uint32_t b = contrib[i].buildings[q];
                for (Seg s : contrib[i].per_bld[q]) {
                    s.ant = uint32_t(i);
                    per_bld[b].push_back(s);
                }
            }

            std::vector<uint32_t> touched;
            for (auto& kv : rest_of) touched.push_back(kv.first);
            for (const uint32_t b : contrib[i].buildings) touched.push_back(b);
            std::sort(touched.begin(), touched.end());
            touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
            for (const uint32_t b : touched) {
                std::vector<Seg> tmp = per_bld[b];
                normalise(tmp);
                double L = 0.0;
                for (const Seg& s : tmp) L += s.hi - s.lo;
                const uint8_t sv = (need[b] > 0.0 && L >= need[b] - band) ? 1 : 0;
                score += uint32_t(int(sv) - int(serviced[b]));
                total_len += L - cur_len[b];
                serviced[b] = sv;
                cur_len[b] = L;
            }

            ++moved;
            gained += uint32_t(std::max(0, best_dscore));
        }

        std::fprintf(stderr,
                     "  round %d: %u antennas moved, score %u (+%u), covered %.1f m "
                     "(%.0f s)\n",
                     round + 1, moved, score, gained, total_len, nowSec() - t_round);
        if (moved == 0) break;
    }
    if (out_of_time) std::fprintf(stderr, "  stopped on the time budget\n");

    EvalParams ep;
    ep.tau = tau;
    ep.max_radius = o.eval_max_radius;
    ep.knife_eps = o.knife_eps;
    ep.claim_band = o.claim_band;
    ep.claim_all = o.claim_all;
    const EvalResult res = evaluateExact(dom, cands, ep, vp);
    std::fprintf(stderr, "  final: %zu buildings serviced (safe %u, on-threshold %u)\n",
                 res.serviced.size(), res.n_safe, res.n_knife);
    if (res.serviced.size() != score)
        std::fprintf(stderr,
                     "  NOTE: incremental bookkeeping said %u; the evaluator is "
                     "authoritative\n",
                     score);

    const std::string out_path = o.out.empty() ? o.solution : o.out;
    FILE* fh = std::fopen(out_path.c_str(), "w");
    if (!fh) throw std::runtime_error("cannot write " + out_path);
    std::fprintf(fh, "(%.17g, %u)\n", sol.tau, sol.k);
    for (size_t i = 0; i < cands.size(); ++i) {
        const Vec2 w = candidateWorldPos(dom, cands[i]);
        std::fprintf(fh, "%s(%.17g,%.17g)", i ? "," : "", w.x, w.y);
    }
    std::fprintf(fh, "\n");
    for (size_t i = 0; i < res.claims.size(); ++i)
        std::fprintf(fh, "%s%lld", i ? "," : "", (long long)dom.buildingId(res.claims[i]));
    std::fprintf(fh, "\n");
    std::fclose(fh);
    std::fprintf(stderr, "  wrote %s\n", out_path.c_str());
    return 0;
}

}
