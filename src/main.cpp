#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "candidates.hpp"
#include "coverage.hpp"
#include "domain.hpp"
#include "viscache.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

#include "refine.hpp"
#include "score_cmd.hpp"
#include "solve_cmd.hpp"

namespace gis {
int runSelfTest();
int runDataCheck(const std::string& path, int n_candidates, double target_radius,
                 unsigned seed, bool uncapped);
}

namespace {

using Args = std::map<std::string, std::string>;

Args parseArgs(int argc, char** argv, int from) {
    Args a;
    for (int i = from; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) throw std::runtime_error("unexpected argument: " + s);
        s = s.substr(2);
        const size_t eq = s.find('=');
        if (eq != std::string::npos) {
            a[s.substr(0, eq)] = s.substr(eq + 1);
        } else if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {
            a[s] = argv[++i];
        } else {
            a[s] = "1";
        }
    }
    return a;
}

std::string opt(const Args& a, const std::string& k, const std::string& dflt) {
    auto it = a.find(k);
    return it == a.end() ? dflt : it->second;
}
double optD(const Args& a, const std::string& k, double dflt) {
    auto it = a.find(k);
    return it == a.end() ? dflt : std::atof(it->second.c_str());
}
long long optI(const Args& a, const std::string& k, long long dflt) {
    auto it = a.find(k);
    return it == a.end() ? dflt : std::atoll(it->second.c_str());
}
bool optB(const Args& a, const std::string& k) { return a.find(k) != a.end(); }

double nowSec() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

void usage() {
    std::printf(
        "gis_cup -- GIS Cup 2026 antenna placement solver\n"
        "\n"
        "  gis_cup selftest\n"
        "      geometry correctness checks\n"
        "\n"
        "  gis_cup precompute --data F.bin --out CACHE.bin [options]\n"
        "      --spacing D       interior candidate spacing along edges, metres (4.0)\n"
        "      --slot-len L      perimeter slot length, metres.  0.05 measured best;\n"
        "                        solve must be given the same value (0.1)\n"
        "      --min-slots N     minimum slots per building (128)\n"
        "      --max-slots N     maximum slots per building (65536)\n"
        "      --cell C          spatial grid cell size, metres (25)\n"
        "      --bins B          angular bins (1024)\n"
        "      --min-arc A       drop visible fragments shorter than A metres (0.02)\n"
        "      --max-range R     cap on sight-line length, metres; 2400 measured best (600)\n"
        "      --slot-rule R     midpoint | conservative (midpoint)\n"
        "      --strict          boundary contact blocks (hedge against a strict judge)\n"
        "      --limit N         only process the first N candidates (benchmarking)\n"
        "      --threads T       OpenMP threads (all)\n"
        "\n"
        "  gis_cup solve --data F.bin --cache CACHE.bin --tau T --k K [options]\n"
        "      --out FILE        write the three-line answer here (rewritten at each\n"
        "                        checkpoint, so it is always valid and always scored)\n"
        "      --time S          search budget in seconds (60)\n"
        "      --deadline E      absolute unix timestamp to stop by; overrides --time\n"
        "                        when nearer.  0 disables (0)\n"
        "      --seed N          RNG seed; vary it across parallel replicas (1)\n"
        "      --destroy-min N   antennas torn out per LNS step, low end (2)\n"
        "      --destroy-max N   antennas torn out per LNS step, high end (8)\n"
        "      --pool-cap N      max candidates examined per repair (6000)\n"
        "      --replicas N      searches run in parallel; they share their best (1)\n"
        "      --repair-sample N random candidates examined per repair (2000)\n"
        "      --sync S          seconds between replica syncs and checkpoints (90)\n"
        "      --bundles N       completing antenna pairs kept per building; 0 off (3)\n"
        "      --bundle-own N    own-boundary candidates considered per building (40)\n"
        "      --grasp-prob P    fraction of greedy steps that sample near-best (0.35)\n"
        "      --grasp-width N   how many near-best moves to sample among (6)\n"
        "      --difficulty-beta B  weight the surrogate by (1/m_b)^B, where m_b is\n"
        "                        how many antennas building b needs on its own; 0 off\n"
        "      --difficulty-cap C   drop buildings needing more than C antennas; 0 off\n"
        "      --exact-lead N    re-score the top N replicas exactly at each sync and\n"
        "                        take the leader from that, not the slot score; 0 off (0)\n"
        "      --phi-exp G       partial-progress reward exponent; 1 keeps Phi\n"
        "                        submodular, >1 favours nearly-finished buildings (1)\n"
        "      --shortlist N     replicas scored exactly before choosing one (24)\n"
        "      --target-destroy 0|1  fourth LNS destroy shape, which picks a building\n"
        "                        just short of tau and steers the repair at it.\n"
        "                        Measured neutral-to-harmful; off by default (0)\n"
        "      --target-sample N buildings sampled when looking for that one (64)\n"
        "      --target-pool N   the target building's candidates added to the repair\n"
        "                        pool; well below --pool-cap on purpose (768)\n"
        "      --share-dir D     island model: a directory shared by every process\n"
        "                        solving this same sub-problem.  Each publishes its\n"
        "                        leader there each sync and adopts the best it finds,\n"
        "                        so many jobs on many nodes act as one search\n"
        "      --island N        this process's id within that directory (0)\n"
        "      --immigrants N    foreign elites adopted per sync; more than a few and\n"
        "                        the islands collapse onto one trajectory (2)\n"
        "      --slot-margin M   search for tau*(1+M) instead of tau, to offset the\n"
        "                        slot model's optimism.  A negative M measures the\n"
        "                        margin from a greedy calibration run instead of\n"
        "                        taking a fitted constant on faith (0 = off)\n"
        "      --slot-margin-cap C  ceiling on a measured margin (0.08)\n"
        "      --eval-range R    cap sight-lines during final scoring (0 = uncapped)\n"
        "      --claim-band F    also claim buildings within F (relative) below tau\n"
        "      --claim-all       claim every building in the dataset\n"
        "      --interior-rings  count discarded interior rings in the perimeter\n"
        "      --strict          cache was built with boundary-blocking semantics\n"
        "\n"
        "  gis_cup refine --data F.bin --solution ANSWER.txt [options]\n"
        "      moves each antenna to the best position among its own edge's tangency\n"
        "      events and the nearest vertices of neighbouring edges, scored by the\n"
        "      exact evaluator.  Never returns a worse answer than it was given.\n"
        "      --out FILE        write here instead of overwriting the input\n"
        "      --tau T           refine against this tau instead of the file's header\n"
        "      --rounds N        sweeps over all antennas; stops early if one gains\n"
        "                        nothing (4)\n"
        "      --trials N        candidate positions tested per antenna per sweep (48)\n"
        "      --relocate-edges N  also try the N nearest edge start vertices, so an\n"
        "                        antenna can leave the edge solve put it on; 0 off (48)\n"
        "      --relocate-radius R  metres to look for those edges (120)\n"
        "      --event-verts N   nearest vertices used to generate an edge's tangency\n"
        "                        events (28)\n"
        "      --event-radius R  how far to look for those vertices, metres (220)\n"
        "      --time S          budget in seconds; 0 = no limit (0)\n"
        "      --deadline E      absolute unix timestamp to stop by; overrides --time\n"
        "                        when nearer.  0 disables (0)\n"
        "      --eval-range R    cap sight-lines during scoring (0 = uncapped)\n"
        "      --claim-band F    also claim buildings within F (relative) below tau\n"
        "      --claim-all       claim every building in the dataset\n"
        "      --interior-rings  count discarded interior rings in the perimeter\n"
        "      --strict          boundary contact blocks\n"
        "\n"
        "  gis_cup score --data F.bin --solution ANSWER.txt [options]\n"
        "      re-reads an emitted answer the way a third party would and scores it\n"
        "      --rewrite FILE    write the answer back with a recomputed claim line\n"
        "      --tau T           score at this tau instead of the file's header\n"
        "      --dump-cov FILE   per-building exact coverage ratios\n"
        "      --eval-range R    cap sight-lines during scoring (0 = uncapped)\n"
        "      --claim-band F    also claim buildings within F (relative) below tau\n"
        "      --claim-all       claim every building in the dataset\n"
        "      --explain-building ID\n"
        "                        narrate every arc landing on this building, to stderr\n"
        "\n");
}

int cmdPrecompute(const Args& a) {
    const std::string data = opt(a, "data", "");
    const std::string out_path = opt(a, "out", "");
    if (data.empty() || out_path.empty()) throw std::runtime_error("--data and --out required");

#ifdef _OPENMP
    const long long threads = optI(a, "threads", 0);
    if (threads > 0) omp_set_num_threads(int(threads));
#endif

    gis::Domain dom;
    dom.load(data);
    dom.buildGrid(optD(a, "cell", 25.0));
    std::printf("domain: %u buildings, %u edges, diagonal %.1f m\n", dom.numBuildings(),
                dom.numEdges(), dom.diagonal());

    const double slot_len = optD(a, "slot-len", 0.1);
    gis::SlotLayout layout;
    layout.build(dom, slot_len, uint32_t(optI(a, "min-slots", 128)),
                 uint32_t(optI(a, "max-slots", 65536)));
    std::printf("slots:  %u total (%.3f m target)\n", layout.total_slots, slot_len);

    gis::CandidateParams cp;
    cp.edge_spacing = optD(a, "spacing", 4.0);
    std::vector<gis::Candidate> cands = gis::generateCandidates(dom, cp);
    const long long limit = optI(a, "limit", 0);
    if (limit > 0 && size_t(limit) < cands.size()) cands.resize(size_t(limit));
    std::printf("candidates: %zu (spacing %.2f m)\n", cands.size(), cp.edge_spacing);

    gis::VisibilityParams vp;
    vp.num_bins = int(optI(a, "bins", 1024));
    vp.min_arc_len = optD(a, "min-arc", 0.02);
    vp.max_radius = optD(a, "max-range", 600.0);
    vp.initial_radius = optD(a, "first-range", 128.0);
    vp.boundary_blocks = optB(a, "strict");
    std::printf("semantics: %s\n",
                vp.boundary_blocks ? "STRICT (boundary blocks)" : "Definition 1 (interior only)");

    const double t0 = nowSec();
    gis::VisCache cache;
    const std::string rule_s = opt(a, "slot-rule", "midpoint");
    const gis::SlotRule rule =
        (rule_s == "midpoint") ? gis::SlotRule::kMidpoint : gis::SlotRule::kConservative;
    std::printf("slot rule: %s\n", rule_s.c_str());
    gis::buildVisCache(dom, layout, cands, vp, rule, cache, 20000);
    const double t1 = nowSec();
    cache.slot_target_len = slot_len;

    unsigned long long total_runs = cache.runs.size();
    unsigned long long total_slots = 0;
    for (const gis::Run& r : cache.runs) total_slots += r.len;
    std::printf("visibility: %.2f s  (%.1f cand/s)\n", t1 - t0, cands.size() / (t1 - t0));
    std::printf("  runs: %llu total, %.1f per candidate\n", total_runs,
                double(total_runs) / double(cands.size()));
    std::printf("  covered slots: %.1f per candidate\n",
                double(total_slots) / double(cands.size()));
    std::printf("  cache size: %.2f GiB\n",
                double(total_runs * sizeof(gis::Run) + cands.size() * 24) / (1 << 30));

    if (opt(a, "no-write", "") != "1") {
        cache.save(out_path);
        std::printf("wrote %s\n", out_path.c_str());
    }
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "selftest") return gis::runSelfTest();
        if (cmd == "datacheck") {
            const Args a = parseArgs(argc, argv, 2);
            return gis::runDataCheck(opt(a, "data", "data/sample.bin"),
                                     int(optI(a, "candidates", 40)),
                                     optD(a, "radius", 150.0),
                                     unsigned(optI(a, "seed", 7)), optB(a, "uncapped"));
        }
        if (cmd == "precompute") return cmdPrecompute(parseArgs(argc, argv, 2));
        if (cmd == "solve") {
            const Args a = parseArgs(argc, argv, 2);
#ifdef _OPENMP
            const long long threads = optI(a, "threads", 0);
            if (threads > 0) omp_set_num_threads(int(threads));
#endif
            gis::SolveOptions o;
            o.data = opt(a, "data", "");
            o.cache = opt(a, "cache", "");
            o.out = opt(a, "out", "");
            o.tau = optD(a, "tau", 0.5);
            o.k = uint32_t(optI(a, "k", 100));
            o.time_budget = optD(a, "time", 60.0);
            o.seed = uint64_t(optI(a, "seed", 1));
            o.cell = optD(a, "cell", 25.0);
            o.slot_len = optD(a, "slot-len", 0.1);
            o.min_slots = uint32_t(optI(a, "min-slots", 128));
            o.max_slots = uint32_t(optI(a, "max-slots", 65536));
            o.bins = int(optI(a, "bins", 1024));
            o.strict = optB(a, "strict");
            o.include_interior_rings = optB(a, "interior-rings");
            o.eval_max_radius = optD(a, "eval-range", 0.0);
            o.claim_band = optD(a, "claim-band", 0.0);
            o.claim_all = optB(a, "claim-all");
            o.repair_sample = uint32_t(optI(a, "repair-sample", 2000));
            o.pool_cap = uint32_t(optI(a, "pool-cap", 6000));
            o.replicas = int(optI(a, "replicas", 1));
            o.deadline_epoch = optD(a, "deadline", 0.0);
            o.sync_interval = optD(a, "sync", 90.0);
            o.phi_exponent = optD(a, "phi-exp", -1.0);
            o.difficulty_beta = optD(a, "difficulty-beta", 0.0);
            o.difficulty_cap = uint32_t(optI(a, "difficulty-cap", 0));
            o.difficulty_width = uint32_t(optI(a, "difficulty-width", 64));
            o.bundles = uint32_t(optI(a, "bundles", 3));
            o.bundle_own_cap = uint32_t(optI(a, "bundle-own", 40));
            o.grasp_prob = optD(a, "grasp-prob", 0.35);
            o.grasp_width = uint32_t(optI(a, "grasp-width", 6));
            o.exact_shortlist = int(optI(a, "shortlist", 24));
            o.exact_lead = int(optI(a, "exact-lead", 0));
            o.dump_cov = opt(a, "dump-cov", "");
            o.explain_building_id = optI(a, "explain-building", -1);
            o.destroy_min = int(optI(a, "destroy-min", 2));
            o.destroy_max = int(optI(a, "destroy-max", 8));
            o.target_destroy = optI(a, "target-destroy", 0) != 0;
            o.target_sample = uint32_t(optI(a, "target-sample", 64));
            o.target_pool = uint32_t(optI(a, "target-pool", 768));
            o.share_dir = opt(a, "share-dir", "");
            o.island = int(optI(a, "island", 0));
            o.immigrants = int(optI(a, "immigrants", 2));
            o.slot_margin = optD(a, "slot-margin", 0.0);
            o.slot_margin_cap = optD(a, "slot-margin-cap", 0.08);
            o.slot_margin_quantile = optD(a, "slot-margin-quantile", 0.80);
            o.slot_margin_floor_slots = optD(a, "slot-margin-floor-slots", 2.0);
            o.slot_margin_trigger = optD(a, "slot-margin-trigger", 0.005);
            if (o.data.empty() || o.cache.empty())
                throw std::runtime_error("--data and --cache required");
            return gis::runSolve(o);
        }
        if (cmd == "refine") {
            const Args a = parseArgs(argc, argv, 2);
#ifdef _OPENMP
            const long long threads = optI(a, "threads", 0);
            if (threads > 0) omp_set_num_threads(int(threads));
#endif
            gis::RefineOptions r;
            r.data = opt(a, "data", "");
            r.solution = opt(a, "solution", "");
            r.out = opt(a, "out", "");
            r.tau = optD(a, "tau", -1.0);
            r.rounds = int(optI(a, "rounds", 4));
            r.trials = int(optI(a, "trials", 48));
            r.event_verts = int(optI(a, "event-verts", 28));
            r.relocate_edges = int(optI(a, "relocate-edges", 48));
            r.relocate_radius = optD(a, "relocate-radius", 120.0);
            r.event_radius = optD(a, "event-radius", 220.0);
            r.time_budget_sec = optD(a, "time", 0.0);
            r.deadline_epoch = optD(a, "deadline", 0.0);
            r.eval_max_radius = optD(a, "eval-range", 0.0);
            r.knife_eps = optD(a, "knife-eps", 1e-9);
            r.claim_band = optD(a, "claim-band", 0.0);
            r.claim_all = optB(a, "claim-all");
            r.bins = int(optI(a, "bins", 1024));
            r.cell = optD(a, "cell", 25.0);
            r.strict = optB(a, "strict");
            r.include_interior_rings = optB(a, "interior-rings");
            if (r.data.empty() || r.solution.empty())
                throw std::runtime_error("--data and --solution required");
            return gis::runRefine(r);
        }
        if (cmd == "score") {
            const Args a = parseArgs(argc, argv, 2);
#ifdef _OPENMP
            const long long threads = optI(a, "threads", 0);
            if (threads > 0) omp_set_num_threads(int(threads));
#endif
            gis::ScoreOptions s;
            s.data = opt(a, "data", "");
            s.solution = opt(a, "solution", "");
            s.rewrite = opt(a, "rewrite", "");
            s.tau = optD(a, "tau", -1.0);
            s.eval_max_radius = optD(a, "eval-range", 0.0);
            s.knife_eps = optD(a, "knife-eps", 1e-9);
            s.claim_band = optD(a, "claim-band", 0.0);
            s.claim_all = optB(a, "claim-all");
            s.bins = int(optI(a, "bins", 1024));
            s.cell = optD(a, "cell", 25.0);
            s.strict = optB(a, "strict");
            s.include_interior_rings = optB(a, "interior-rings");
            s.dump_cov = opt(a, "dump-cov", "");
            s.explain_building_id = optI(a, "explain-building", -1);
            s.min_arc_len = optD(a, "min-arc", 1e-6);
            if (s.data.empty() || s.solution.empty())
                throw std::runtime_error("--data and --solution required");
            return gis::runScore(s);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    usage();
    return 2;
}
