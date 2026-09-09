#include "score_cmd.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "candidates.hpp"
#include "domain.hpp"
#include "evaluate.hpp"
#include "visibility.hpp"

namespace gis {

ParsedSolution parseSolutionFile(const std::string& path) {
    std::ifstream fh(path);
    if (!fh) throw std::runtime_error("cannot open " + path);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(fh, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        lines.push_back(line.substr(a, b - a + 1));
    }
    if (lines.size() < 2) throw std::runtime_error("solution file needs at least two lines");

    ParsedSolution out;
    {
        std::string h = lines[0];
        for (char& c : h)
            if (c == '(' || c == ')' || c == ',') c = ' ';
        std::istringstream is(h);
        if (!(is >> out.tau >> out.k)) throw std::runtime_error("bad header line");
    }

    {
        const std::string& s = lines[1];
        size_t i = 0;
        while (true) {
            const size_t o = s.find('(', i);
            if (o == std::string::npos) break;
            const size_t c = s.find(')', o);
            if (c == std::string::npos) throw std::runtime_error("unclosed ( in antenna line");
            std::string body = s.substr(o + 1, c - o - 1);
            const size_t comma = body.find(',');
            if (comma == std::string::npos) throw std::runtime_error("antenna is not a pair");
            out.antennas.push_back(Vec2(std::atof(body.substr(0, comma).c_str()),
                                        std::atof(body.substr(comma + 1).c_str())));
            i = c + 1;
        }
    }

    if (lines.size() >= 3) {
        std::string s = lines[2];
        for (char& c : s)
            if (c == ',') c = ' ';
        std::istringstream is(s);
        long long v;
        while (is >> v) out.claimed.push_back(v);
    }
    return out;
}

int runScore(const ScoreOptions& o) {
    Domain dom;
    dom.load(o.data);
    dom.setIncludeInteriorRings(o.include_interior_rings);
    dom.buildGrid(o.cell);

    ParsedSolution sol = parseSolutionFile(o.solution);
    const double tau = (o.tau >= 0.0) ? o.tau : sol.tau;

    std::printf("file:      %s\n", o.solution.c_str());
    std::printf("header:    tau=%.17g k=%u\n", sol.tau, sol.k);
    std::printf("antennas:  %zu\n", sol.antennas.size());
    std::printf("claimed:   %zu\n", sol.claimed.size());
    if (sol.antennas.size() != sol.k)
        std::printf("  WARNING: header says k=%u but the file lists %zu antennas\n", sol.k,
                    sol.antennas.size());

    std::vector<Candidate> cands;
    cands.reserve(sol.antennas.size());
    double worst_snap = 0.0;
    for (const Vec2& a : sol.antennas) {
        double d = 0.0;
        cands.push_back(snapToBoundary(dom, a, &d));
        worst_snap = std::max(worst_snap, d);
    }
    std::printf("worst distance from a boundary: %.3e m%s\n", worst_snap,
                worst_snap > 1e-6 ? "   <-- OFF THE BOUNDARY" : "");

    VisibilityParams vp;
    vp.num_bins = o.bins;
    vp.boundary_blocks = o.strict;
    vp.min_arc_len = o.min_arc_len;

    EvalParams ep;
    ep.tau = tau;
    ep.max_radius = o.eval_max_radius;
    ep.knife_eps = o.knife_eps;
    ep.claim_band = o.claim_band;
    ep.claim_all = o.claim_all;

    if (o.explain_building_id >= 0) {
        bool found = false;
        for (uint32_t b = 0; b < dom.numBuildings(); ++b)
            if (dom.buildingId(b) == o.explain_building_id) {
                ep.explain_building = b;
                std::fprintf(stderr,
                             "  explaining building id=%lld (index %u, perimeter %.4f)\n",
                             o.explain_building_id, b, dom.scoringPerimeter(b));
                found = true;
                break;
            }
        if (!found)
            std::fprintf(stderr, "  WARNING: building id=%lld not in the dataset\n",
                         o.explain_building_id);
    }

    EvalResult res = evaluateExact(dom, cands, ep, vp);

    std::printf("\nre-scored at tau=%.17g: %zu buildings serviced (safe %u, on-threshold %u)\n",
                tau, res.serviced.size(), res.n_safe, res.n_knife);

    std::printf("SCORE tau=%.17g k=%u serviced=%zu safe=%u knife=%u claims=%zu\n", tau,
                unsigned(sol.antennas.size()), res.serviced.size(), res.n_safe, res.n_knife,
                res.claims.size());

    if (!sol.claimed.empty()) {
        std::vector<long long> recomputed;
        recomputed.reserve(res.serviced.size());
        for (uint32_t b : res.serviced) recomputed.push_back(dom.buildingId(b));
        std::vector<long long> claimed = sol.claimed;
        std::sort(claimed.begin(), claimed.end());
        std::sort(recomputed.begin(), recomputed.end());

        std::vector<long long> over, under;
        std::set_difference(claimed.begin(), claimed.end(), recomputed.begin(),
                            recomputed.end(), std::back_inserter(over));
        std::set_difference(recomputed.begin(), recomputed.end(), claimed.begin(),
                            claimed.end(), std::back_inserter(under));
        std::printf("claim line: %zu claimed but not supported, %zu supported but not claimed\n",
                    over.size(), under.size());
        for (size_t i = 0; i < over.size() && i < 10; ++i) {
            double r = -1.0;
            for (uint32_t b = 0; b < dom.numBuildings(); ++b)
                if (dom.buildingId(b) == over[i]) {
                    r = res.ratio[b];
                    break;
                }
            std::printf("    unsupported claim id=%lld ratio=%.6f (needs %.4g)\n", over[i], r, tau);
        }
    }

    if (!o.dump_cov.empty()) {
        FILE* fh = std::fopen(o.dump_cov.c_str(), "w");
        if (fh) {
            for (uint32_t b = 0; b < dom.numBuildings(); ++b)
                std::fprintf(fh, "%lld %.10f\n", (long long)dom.buildingId(b), res.ratio[b]);
            std::fclose(fh);
            std::printf("wrote coverage dump %s\n", o.dump_cov.c_str());
        }
    }

    if (!o.rewrite.empty()) {
        FILE* fh = std::fopen(o.rewrite.c_str(), "w");
        if (!fh) throw std::runtime_error("cannot write " + o.rewrite);
        std::fprintf(fh, "(%.17g, %u)\n", sol.tau, sol.k);
        for (size_t i = 0; i < sol.antennas.size(); ++i)
            std::fprintf(fh, "%s(%.17g,%.17g)", i ? "," : "", sol.antennas[i].x,
                         sol.antennas[i].y);
        std::fprintf(fh, "\n");
        for (size_t i = 0; i < res.claims.size(); ++i)
            std::fprintf(fh, "%s%lld", i ? "," : "",
                         (long long)dom.buildingId(res.claims[i]));
        std::fprintf(fh, "\n");
        std::fclose(fh);
        std::printf("wrote %s\n", o.rewrite.c_str());
    }

    return 0;
}

}
