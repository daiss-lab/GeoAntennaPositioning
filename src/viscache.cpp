#include "viscache.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace gis {
namespace {

constexpr char kMagic[8] = {'G', 'I', 'S', 'V', 'I', 'S', '0', '1'};
constexpr size_t kChunk = 128;

template <typename T>
void writeVec(std::ofstream& fh, const std::vector<T>& v) {
    if (!v.empty()) fh.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
}

template <typename T>
void readVec(std::ifstream& fh, std::vector<T>& v, size_t n) {
    v.resize(n);
    if (n) fh.read(reinterpret_cast<char*>(v.data()), sizeof(T) * n);
    if (!fh) throw std::runtime_error("truncated visibility cache");
}

}  // namespace

void buildVisCache(const Domain& dom, const SlotLayout& layout,
                   const std::vector<Candidate>& cands, const VisibilityParams& vp,
                   SlotRule rule, VisCache& out, size_t progress_every) {
    const size_t n = cands.size();
    out.cands = cands;
    out.slot_target_len = 0.0;  // filled by the caller
    out.total_slots = layout.total_slots;
    out.num_buildings = dom.numBuildings();
    out.boundary_blocks = vp.boundary_blocks ? 1 : 0;
    out.slot_rule = (rule == SlotRule::kConservative) ? 1 : 0;

    const size_t n_chunks = (n + kChunk - 1) / kChunk;
    std::vector<std::vector<Run>> chunk_runs(n_chunks);
    std::vector<uint32_t> per_cand(n, 0);
    std::atomic<size_t> done{0};

    VisibilityEngine eng(dom, vp);

#pragma omp parallel
    {
        VisWorkspace ws;
        ws.init(dom.numEdges(), vp.num_bins);
        std::vector<VisArc> arcs;
        std::vector<uint32_t> scratch;
        std::vector<Run> runs;

#pragma omp for schedule(dynamic, 1)
        for (long long ci = 0; ci < (long long)n_chunks; ++ci) {
            const size_t lo = size_t(ci) * kChunk;
            const size_t hi = std::min(n, lo + kChunk);
            std::vector<Run>& dst = chunk_runs[ci];
            dst.clear();

            for (size_t i = lo; i < hi; ++i) {
                arcs.clear();
                eng.computeArcs(cands[i].edge, cands[i].t, ws, arcs);
                arcsToRuns(dom, layout, arcs, rule, scratch, runs);
                per_cand[i] = uint32_t(runs.size());
                dst.insert(dst.end(), runs.begin(), runs.end());
            }

            const size_t d = done.fetch_add(hi - lo) + (hi - lo);
            if (progress_every && (d % progress_every) < (hi - lo)) {
#pragma omp critical
                std::fprintf(stderr, "  visibility: %zu / %zu candidates\n", d, n);
            }
        }
    }

    out.run_offset.resize(n + 1);
    out.run_offset[0] = 0;
    for (size_t i = 0; i < n; ++i) out.run_offset[i + 1] = out.run_offset[i] + per_cand[i];

    out.runs.resize(out.run_offset[n]);
    // Chunks are contiguous in candidate order, so each one lands in a single memcpy.
#pragma omp parallel for schedule(static)
    for (long long ci = 0; ci < (long long)n_chunks; ++ci) {
        const size_t lo = size_t(ci) * kChunk;
        const std::vector<Run>& src = chunk_runs[ci];
        if (!src.empty())
            std::memcpy(out.runs.data() + out.run_offset[lo], src.data(),
                        src.size() * sizeof(Run));
    }
}

void VisCache::save(const std::string& path) const {
    std::ofstream fh(path, std::ios::binary);
    if (!fh) throw std::runtime_error("cannot write " + path);
    fh.write(kMagic, 8);
    const uint64_t n = cands.size();
    const uint64_t nr = runs.size();
    fh.write(reinterpret_cast<const char*>(&n), 8);
    fh.write(reinterpret_cast<const char*>(&nr), 8);
    fh.write(reinterpret_cast<const char*>(&slot_target_len), 8);
    fh.write(reinterpret_cast<const char*>(&total_slots), 4);
    fh.write(reinterpret_cast<const char*>(&num_buildings), 4);
    fh.write(reinterpret_cast<const char*>(&boundary_blocks), 1);
    fh.write(reinterpret_cast<const char*>(&slot_rule), 1);
    const char pad[6] = {0, 0, 0, 0, 0, 0};
    fh.write(pad, 6);
    writeVec(fh, cands);
    writeVec(fh, run_offset);
    writeVec(fh, runs);
    if (!fh) throw std::runtime_error("write failed for " + path);
}

void VisCache::load(const std::string& path) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) throw std::runtime_error("cannot open " + path);
    char magic[8];
    fh.read(magic, 8);
    if (!fh || std::memcmp(magic, kMagic, 8) != 0)
        throw std::runtime_error("bad magic in " + path);
    uint64_t n = 0, nr = 0;
    fh.read(reinterpret_cast<char*>(&n), 8);
    fh.read(reinterpret_cast<char*>(&nr), 8);
    fh.read(reinterpret_cast<char*>(&slot_target_len), 8);
    fh.read(reinterpret_cast<char*>(&total_slots), 4);
    fh.read(reinterpret_cast<char*>(&num_buildings), 4);
    fh.read(reinterpret_cast<char*>(&boundary_blocks), 1);
    fh.read(reinterpret_cast<char*>(&slot_rule), 1);
    char pad[6];
    fh.read(pad, 6);
    readVec(fh, cands, n);
    readVec(fh, run_offset, n + 1);
    readVec(fh, runs, nr);
}

}  // namespace gis
