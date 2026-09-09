#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "solve_cmd.hpp"

#include "candidates.hpp"
#include "coverage.hpp"
#include "domain.hpp"
#include "evaluate.hpp"
#include "optimize.hpp"
#include "viscache.hpp"

namespace gis {

namespace {

double nowSec() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

double unixNow() {
    return double(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count()) /
           1000.0;
}

void writeSolution(const std::string& path, const Domain& dom, double tau, uint32_t k,
                   const std::vector<Candidate>& antennas, const EvalResult& res) {
    const std::string tmp = path + ".tmp";
    FILE* fh = std::fopen(tmp.c_str(), "w");
    if (!fh) throw std::runtime_error("cannot write " + tmp);

    std::fprintf(fh, "(%.17g, %u)\n", tau, k);

    for (size_t i = 0; i < antennas.size(); ++i) {
        const Vec2 p = candidateWorldPos(dom, antennas[i]);
        std::fprintf(fh, "%s(%.17g,%.17g)", i ? "," : "", p.x, p.y);
    }
    std::fprintf(fh, "\n");

    for (size_t i = 0; i < res.claims.size(); ++i)
        std::fprintf(fh, "%s%lld", i ? "," : "", (long long)dom.buildingId(res.claims[i]));
    std::fprintf(fh, "\n");
    std::fclose(fh);
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        throw std::runtime_error("cannot move " + tmp + " into place");
}

std::vector<Candidate> materialise(const VisCache& cache, const std::vector<uint32_t>& sel,
                                   uint32_t k) {
    std::vector<Candidate> antennas;
    antennas.reserve(k);
    std::vector<uint8_t> used(cache.numCandidates(), 0);
    for (uint32_t ci : sel) {
        antennas.push_back(cache.cands[ci]);
        used[ci] = 1;
    }
    for (uint32_t i = 0; antennas.size() < k && i < cache.numCandidates(); ++i)
        if (!used[i]) antennas.push_back(cache.cands[i]);
    return antennas;
}

}

int runSolve(const SolveOptions& o) {
    Domain dom;
    dom.load(o.data);
    dom.setIncludeInteriorRings(o.include_interior_rings);
    dom.buildGrid(o.cell);

    SlotLayout layout;
    layout.build(dom, o.slot_len, o.min_slots, o.max_slots);

    VisCache cache;
    cache.load(o.cache);
    if (cache.total_slots != layout.total_slots || cache.num_buildings != dom.numBuildings())
        throw std::runtime_error(
            "visibility cache was built for a different discretisation; re-run precompute");
    if ((cache.boundary_blocks != 0) != o.strict)
        throw std::runtime_error("cache visibility semantics differ from --strict setting");

    std::fprintf(stderr, "solve tau=%.4g k=%u  (%u buildings, %zu candidates)\n", o.tau, o.k,
                 dom.numBuildings(), cache.numCandidates());

    Optimizer opt(dom, layout, cache);
    const uint32_t excluded = opt.setThreshold(o.tau, 0.0);
    std::fprintf(stderr, "  %u buildings cannot reach tau with any placement (excluded)\n",
                 excluded);

    VisibilityParams vp;
    vp.num_bins = o.bins;
    vp.boundary_blocks = o.strict;

    EvalParams ep;
    ep.tau = o.tau;
    ep.max_radius = o.eval_max_radius;
    ep.claim_band = o.claim_band;
    ep.claim_all = o.claim_all;
    if (o.explain_building_id >= 0) {
        for (uint32_t b = 0; b < dom.numBuildings(); ++b)
            if (dom.buildingId(b) == o.explain_building_id) {
                ep.explain_building = b;
                std::fprintf(stderr,
                             "  explaining building id=%lld (index %u, perimeter %.4f)\n",
                             o.explain_building_id, b, dom.scoringPerimeter(b));
            }
    }

    OptimizeParams op;
    op.tau = o.tau;
    op.k = o.k;
    op.seed = o.seed;
    op.repair_sample = o.repair_sample;
    op.pool_cap = o.pool_cap;
    op.replicas = o.replicas;
    op.destroy_min = o.destroy_min;
    op.destroy_max = o.destroy_max;
    op.target_destroy = o.target_destroy;
    op.target_sample = o.target_sample;
    op.target_pool = o.target_pool;
    op.sync_interval_sec = o.sync_interval;
    op.phi_exponent = (o.phi_exponent >= 0.0) ? o.phi_exponent : 1.0;
    std::fprintf(stderr, "  phi exponent %.4g (%s)\n", op.phi_exponent,
                 (o.phi_exponent >= 0.0) ? "from --phi-exp" : "default");
    op.difficulty_beta = o.difficulty_beta;
    op.difficulty_cap = o.difficulty_cap;
    op.difficulty_width = o.difficulty_width;
    if (o.difficulty_beta > 0.0 || o.difficulty_cap > 0)
        std::fprintf(stderr, "  difficulty weighting: beta %.3g, cap %u, width %u\n",
                     o.difficulty_beta, o.difficulty_cap, o.difficulty_width);
    op.bundles_per_building = o.bundles;
    op.bundle_own_cap = o.bundle_own_cap;
    op.grasp_prob = o.grasp_prob;
    op.grasp_width = o.grasp_width;
    op.share_dir = o.share_dir;
    op.island = o.island;
    op.immigrants = o.immigrants;
    if (!op.share_dir.empty())
        std::fprintf(stderr, "  island %d sharing through %s (%d immigrants per sync)\n",
                     op.island, op.share_dir.c_str(), op.immigrants);

    op.time_budget_sec = o.time_budget;
    if (o.deadline_epoch > 0.0) {
        const double left = o.deadline_epoch - unixNow() - 120.0;
        if (left < op.time_budget_sec) op.time_budget_sec = left;
        std::fprintf(stderr, "  deadline in %.0f s; search budget set to %.0f s\n",
                     o.deadline_epoch - unixNow(), op.time_budget_sec);
    }
    if (op.time_budget_sec < 0.0) op.time_budget_sec = 0.0;

    double margin = o.slot_margin;
    if (margin < 0.0) {
        const double t_cal = nowSec();
        OptimizeParams cal = op;
        cal.time_budget_sec = 0.0;
        cal.replicas = 1;
        cal.verbose = false;
        cal.share_dir.clear();
        cal.on_sync = nullptr;
        const std::vector<Solution> cs = opt.solve(cal);

        margin = 0.0;
        if (!cs.empty() && !cs[0].cand_idx.empty()) {
            const std::vector<Candidate> ants = materialise(cache, cs[0].cand_idx, o.k);
            EvalParams cp = ep;
            cp.explain_building = -1;
            const EvalResult r = evaluateExact(dom, ants, cp, vp);
            const std::vector<double> slot_ratio = opt.slotRatios(cs[0].cand_idx);

            std::vector<double> shortfall;
            for (uint32_t b = 0; b < dom.numBuildings(); ++b) {
                if (slot_ratio[b] < o.tau) continue;
                if (r.ratio[b] >= o.tau) continue;
                shortfall.push_back((o.tau - r.ratio[b]) / o.tau);
            }
            const double over_frac =
                r.serviced.empty() ? 0.0
                                   : double(shortfall.size()) / double(r.serviced.size());

            double q_margin = 0.0, floor_margin = 0.0;
            if (!shortfall.empty() && over_frac >= o.slot_margin_trigger) {
                std::sort(shortfall.begin(), shortfall.end());
                const size_t q =
                    size_t(o.slot_margin_quantile * double(shortfall.size() - 1) + 0.5);
                q_margin = shortfall[q];

                uint32_t median_n = layout.count(0);
                {
                    std::vector<uint32_t> ns;
                    ns.reserve(dom.numBuildings());
                    for (uint32_t b = 0; b < dom.numBuildings(); ++b) ns.push_back(layout.count(b));
                    std::nth_element(ns.begin(), ns.begin() + ns.size() / 2, ns.end());
                    median_n = ns[ns.size() / 2];
                }
                if (median_n > 0)
                    floor_margin = o.slot_margin_floor_slots / (o.tau * double(median_n));
                margin = std::max(q_margin, floor_margin);
                if (margin > o.slot_margin_cap) margin = o.slot_margin_cap;
            }
            std::fprintf(stderr,
                         "  calibration: greedy slot-score %u -> exact %zu, %zu buildings "
                         "over-claimed (%.2f%% of served) (%.1f s)\n",
                         cs[0].score, r.serviced.size(), shortfall.size(), 100.0 * over_frac,
                         nowSec() - t_cal);
            std::fprintf(stderr, "    quantile margin %.3g, one-slot floor %.3g\n", q_margin,
                         floor_margin);
        }
        std::fprintf(stderr, "  slot margin %.4g (measured)\n", margin);
    }
    if (margin > 0.0) {
        opt.setThreshold(o.tau, margin);
        std::fprintf(stderr, "  search aims at tau=%.5g instead of %.5g\n",
                     o.tau * (1.0 + margin), o.tau);
    }

    if (o.exact_lead > 0) {
        op.exact_score = [&](const std::vector<uint32_t>& sel) -> uint32_t {
            const std::vector<Candidate> antennas = materialise(cache, sel, o.k);
            EvalParams cp = ep;
            cp.explain_building = -1;
            return uint32_t(evaluateExact(dom, antennas, cp, vp).serviced.size());
        };
        op.exact_top = o.exact_lead;
        std::fprintf(stderr, "  leader chosen by exact score over the top %d replicas\n",
                     o.exact_lead);
    }

    uint32_t ckpt_best = 0;
    bool have_ckpt = false;
    if (!o.out.empty()) {
        op.on_sync = [&](const Solution& s) {
            if (have_ckpt && s.score <= ckpt_best) return;
            const std::vector<Candidate> antennas = materialise(cache, s.cand_idx, o.k);
            EvalParams cp = ep;
            cp.explain_building = -1;
            const EvalResult r = evaluateExact(dom, antennas, cp, vp);
            writeSolution(o.out, dom, o.tau, o.k, antennas, r);
            ckpt_best = s.score;
            have_ckpt = true;
            std::fprintf(stderr, "  checkpoint: %zu buildings serviced -> %s\n",
                         r.serviced.size(), o.out.c_str());
        };
    }

    const double t0 = nowSec();
    std::vector<Solution> sols = opt.solve(op);
    std::fprintf(stderr, "  search done in %.1f s, best slot-score %u\n", nowSec() - t0,
                 sols.empty() ? 0u : sols[0].score);

    const double t1 = nowSec();
    int best_i = 0, evaluated = 0;
    EvalResult best_res;
    std::vector<Candidate> best_antennas;
    std::vector<uint64_t> seen_hash;
    for (size_t i = 0; i < sols.size() && evaluated < o.exact_shortlist; ++i) {
        std::vector<uint32_t> key = sols[i].cand_idx;
        std::sort(key.begin(), key.end());
        uint64_t h = 1469598103934665603ULL;
        for (uint32_t v : key) h = (h ^ v) * 1099511628211ULL;
        if (std::find(seen_hash.begin(), seen_hash.end(), h) != seen_hash.end()) continue;
        seen_hash.push_back(h);

        std::vector<Candidate> antennas = materialise(cache, sols[i].cand_idx, o.k);
        EvalParams cp = ep;
        if (evaluated != 0) cp.explain_building = -1;
        EvalResult r = evaluateExact(dom, antennas, cp, vp);
        std::fprintf(stderr, "    placement %zu: slot-score %u -> exact %zu\n", i,
                     sols[i].score, r.serviced.size());
        if (evaluated == 0 || r.serviced.size() > best_res.serviced.size()) {
            best_i = int(i);
            best_res = std::move(r);
            best_antennas = std::move(antennas);
        }
        ++evaluated;
    }
    std::fprintf(stderr, "  exact evaluation of %d distinct placements in %.1f s (kept #%d)\n",
                 evaluated, nowSec() - t1, best_i);

    const EvalResult& res = best_res;
    std::fprintf(stderr, "  RESULT tau=%.4g k=%u  score=%zu  (safe %u, on-threshold %u)\n",
                 o.tau, o.k, res.serviced.size(), res.n_safe, res.n_knife);

    if (!o.dump_cov.empty()) {
        FILE* fh = std::fopen(o.dump_cov.c_str(), "w");
        if (fh) {
            for (uint32_t b = 0; b < dom.numBuildings(); ++b)
                std::fprintf(fh, "%lld %.10f\n", (long long)dom.buildingId(b), res.ratio[b]);
            std::fclose(fh);
            std::fprintf(stderr, "  wrote coverage dump %s\n", o.dump_cov.c_str());
        }
    }
    if (ep.explain_building >= 0)
        std::fprintf(stderr, "  explained building exact ratio = %.6f\n",
                     res.ratio[ep.explain_building]);

    if (!o.out.empty()) {
        writeSolution(o.out, dom, o.tau, o.k, best_antennas, res);
        std::fprintf(stderr, "  wrote %s\n", o.out.c_str());
    }
    std::printf("%.17g %u %zu %u %u\n", o.tau, o.k, res.serviced.size(), res.n_safe,
                res.n_knife);
    return 0;
}

}
