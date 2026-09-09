#include "optimize.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>

#include <dirent.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "candidates.hpp"

namespace gis {
namespace {

double nowSec() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

inline uint64_t maskFrom(uint32_t bit) { return ~0ULL << bit; }
inline uint64_t maskTo(uint32_t bit) { return ~0ULL >> (63 - bit); }

inline uint64_t nextRand(uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 0x2545F4914F6CDD1DULL;
}
inline uint32_t randBelow(uint64_t& s, uint32_t n) {
    return n ? uint32_t(nextRand(s) % n) : 0u;
}
inline double randUnit(uint64_t& s) {
    return double(nextRand(s) >> 11) * (1.0 / 9007199254740992.0);
}
inline uint64_t seedFrom(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x ? x : 0x9E3779B97F4A7C15ULL;
}

uint64_t shareSignature(double tau, uint32_t k, uint32_t n_cands, uint32_t n_slots) {
    uint64_t h = 1469598103934665603ULL;
    uint64_t t;
    static_assert(sizeof(t) == sizeof(tau), "");
    std::memcpy(&t, &tau, sizeof(t));
    for (uint64_t v : {t, uint64_t(k), uint64_t(n_cands), uint64_t(n_slots)})
        h = (h ^ v) * 1099511628211ULL;
    return h;
}

void publishElite(const std::string& dir, int island, uint64_t sig, const Solution& s) {
    char path[4096], tmp[4096];
    std::snprintf(path, sizeof(path), "%s/isl_%d.sol", dir.c_str(), island);
    std::snprintf(tmp, sizeof(tmp), "%s/isl_%d.sol.tmp", dir.c_str(), island);
    FILE* fh = std::fopen(tmp, "w");
    if (!fh) return;
    std::fprintf(fh, "%llu %u %.17g %zu\n", (unsigned long long)sig, s.score, s.phi,
                 s.cand_idx.size());
    for (size_t i = 0; i < s.cand_idx.size(); ++i)
        std::fprintf(fh, "%s%u", i ? " " : "", s.cand_idx[i]);
    std::fprintf(fh, "\n");
    std::fclose(fh);
    std::rename(tmp, path);
}

std::vector<Solution> readForeignElites(const std::string& dir, int island, uint64_t sig,
                                        uint32_t n_cands, uint32_t k) {
    std::vector<Solution> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    char own[64];
    std::snprintf(own, sizeof(own), "isl_%d.sol", island);
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 9 || name.compare(0, 4, "isl_") != 0) continue;
        if (name.compare(name.size() - 4, 4, ".sol") != 0) continue;
        if (name == own) continue;

        FILE* fh = std::fopen((dir + "/" + name).c_str(), "r");
        if (!fh) continue;
        Solution s;
        unsigned long long fsig = 0;
        unsigned score = 0;
        double phi = 0.0;
        size_t n = 0;
        if (std::fscanf(fh, "%llu %u %lf %zu", &fsig, &score, &phi, &n) == 4 && fsig == sig &&
            n > 0 && n <= size_t(k)) {
            s.score = score;
            s.phi = phi;
            s.cand_idx.reserve(n);
            bool ok = true;
            for (size_t i = 0; i < n; ++i) {
                unsigned v;
                if (std::fscanf(fh, "%u", &v) != 1 || v >= n_cands) {
                    ok = false;
                    break;
                }
                s.cand_idx.push_back(v);
            }
            if (ok) out.push_back(std::move(s));
        }
        std::fclose(fh);
    }
    closedir(d);
    return out;
}

}

void SearchState::init(uint32_t n_buildings, uint32_t n_slots, uint32_t n_cands) {
    cov.assign(n_buildings, 0);
    cnt.assign(n_slots, 0);
    bits.assign((n_slots + 63) / 64, 0ULL);
    chosen.assign(n_cands, 0);
    touch_stamp.assign(n_buildings, 0);
    touch_epoch = 0;
    sel.clear();
    score = 0;
    phi = 0.0;
}

void SearchState::clear() {
    std::fill(cov.begin(), cov.end(), 0u);
    std::fill(cnt.begin(), cnt.end(), uint16_t(0));
    std::fill(bits.begin(), bits.end(), 0ULL);
    std::fill(chosen.begin(), chosen.end(), uint8_t(0));
    sel.clear();
    score = 0;
    phi = 0.0;
}

Optimizer::Optimizer(const Domain& dom, const SlotLayout& layout, const VisCache& cache)
    : dom_(dom), layout_(layout), cache_(cache) {
    n_buildings_ = dom.numBuildings();
    n_cands_ = uint32_t(cache.numCandidates());
    req_.assign(n_buildings_, 0);
    w_.assign(n_buildings_, 1.0);
    buildIndices();
    buildOwnIndex();

    cand_pos_.resize(n_cands_);
    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        cand_pos_[ci] = candidateLocalPos(dom_, cache_.cands[ci]);
}

void Optimizer::buildIndices() {
    cb_start_.assign(n_cands_ + 1, 0);
    std::vector<std::vector<uint32_t>> per_cand(n_cands_);

#pragma omp parallel for schedule(static)
    for (long long ci = 0; ci < (long long)n_cands_; ++ci) {
        std::vector<uint32_t> tmp;
        uint32_t n;
        const Run* runs = cache_.runsOf(size_t(ci), n);
        for (uint32_t i = 0; i < n; ++i) tmp.push_back(layout_.buildingOfSlot(runs[i].start));
        std::sort(tmp.begin(), tmp.end());
        tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
        per_cand[ci] = tmp;
    }

    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        cb_start_[ci + 1] = cb_start_[ci] + uint32_t(per_cand[ci].size());
    cb_items_.resize(cb_start_[n_cands_]);
    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        std::copy(per_cand[ci].begin(), per_cand[ci].end(), cb_items_.begin() + cb_start_[ci]);

    std::vector<uint32_t> counts(n_buildings_ + 1, 0);
    for (uint32_t b : cb_items_) ++counts[b + 1];
    for (uint32_t b = 0; b < n_buildings_; ++b) counts[b + 1] += counts[b];
    bc_start_ = counts;
    bc_items_.resize(counts[n_buildings_]);
    bc_cnt_.assign(counts[n_buildings_], 0);
    std::vector<uint32_t> cursor(bc_start_.begin(), bc_start_.end() - 1);
    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        for (uint32_t j = cb_start_[ci]; j < cb_start_[ci + 1]; ++j)
            bc_items_[cursor[cb_items_[j]]++] = ci;

    std::vector<uint32_t> where(bc_start_.begin(), bc_start_.end() - 1);
    for (uint32_t ci = 0; ci < n_cands_; ++ci) {
        uint32_t n;
        const Run* runs = cache_.runsOf(ci, n);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t b = layout_.buildingOfSlot(runs[i].start);
            uint32_t& j = where[b];
            while (j < bc_start_[b + 1] && bc_items_[j] != ci) ++j;
            if (j < bc_start_[b + 1]) bc_cnt_[j] += runs[i].len;
        }
    }
}

double Optimizer::applyDifficulty(const OptimizeParams& p) {
    w_.assign(n_buildings_, 1.0);
    difficulty_.assign(n_buildings_, 0);
    w_flat_ = true;
    if (p.difficulty_beta <= 0.0 && p.difficulty_cap == 0) return 0.0;

    const uint32_t width = std::max(1u, p.difficulty_width);

#pragma omp parallel
    {
        std::vector<std::pair<uint32_t, uint32_t>> ranked;
        std::vector<uint64_t> got;
#pragma omp for schedule(dynamic, 64)
        for (long long bb = 0; bb < (long long)n_buildings_; ++bb) {
            const uint32_t b = uint32_t(bb);
            if (req_[b] == 0) continue;
            const uint32_t lo = bc_start_[b], hi = bc_start_[b + 1];
            if (lo == hi) continue;

            ranked.clear();
            for (uint32_t j = lo; j < hi; ++j) ranked.emplace_back(~bc_cnt_[j], bc_items_[j]);
            const uint32_t take = std::min<uint32_t>(width, uint32_t(ranked.size()));
            std::partial_sort(ranked.begin(), ranked.begin() + take, ranked.end());
            ranked.resize(take);

            const uint32_t base = layout_.slot_offset[b];
            const uint32_t n = layout_.count(b);
            got.assign((n + 63) / 64, 0ULL);
            uint32_t have = 0, used = 0;

            while (have < req_[b] && used < take) {
                uint32_t best_add = 0, best_at = UINT32_MAX;
                for (uint32_t r = 0; r < take; ++r) {
                    const uint32_t ci = ranked[r].second;
                    if (ci == UINT32_MAX) continue;
                    uint32_t nr;
                    const Run* runs = cache_.runsOf(ci, nr);
                    uint32_t add = 0;
                    for (uint32_t i = 0; i < nr; ++i) {
                        if (runs[i].start < base || runs[i].start >= base + n) continue;
                        for (uint32_t s = runs[i].start; s < runs[i].start + runs[i].len; ++s) {
                            const uint32_t o = s - base;
                            if (!(got[o >> 6] >> (o & 63) & 1ULL)) ++add;
                        }
                    }
                    if (add > best_add) {
                        best_add = add;
                        best_at = r;
                    }
                }
                if (best_at == UINT32_MAX) break;
                uint32_t nr;
                const Run* runs = cache_.runsOf(ranked[best_at].second, nr);
                for (uint32_t i = 0; i < nr; ++i) {
                    if (runs[i].start < base || runs[i].start >= base + n) continue;
                    for (uint32_t s = runs[i].start; s < runs[i].start + runs[i].len; ++s) {
                        const uint32_t o = s - base;
                        got[o >> 6] |= 1ULL << (o & 63);
                    }
                }
                have += best_add;
                ranked[best_at].second = UINT32_MAX;
                ++used;
            }
            difficulty_[b] = uint16_t(have >= req_[b] ? used : used + 1);
        }
    }

    double sum = 0.0;
    uint32_t counted = 0;
    for (uint32_t b = 0; b < n_buildings_; ++b) {
        if (req_[b] == 0 || difficulty_[b] == 0) continue;
        sum += difficulty_[b];
        ++counted;
    }

    if (p.difficulty_cap > 0) {
        for (uint32_t b = 0; b < n_buildings_; ++b)
            if (req_[b] != 0 && difficulty_[b] > p.difficulty_cap) req_[b] = 0;
        active_.clear();
        for (uint32_t b = 0; b < n_buildings_; ++b)
            if (req_[b] != 0) active_.push_back(b);
    }

    if (p.difficulty_beta > 0.0) {
        double wsum = 0.0;
        uint32_t wn = 0;
        for (uint32_t b = 0; b < n_buildings_; ++b) {
            if (req_[b] == 0) continue;
            const double m = difficulty_[b] > 0 ? double(difficulty_[b]) : 1.0;
            w_[b] = std::pow(1.0 / m, p.difficulty_beta);
            wsum += w_[b];
            ++wn;
        }
        if (wn > 0 && wsum > 0.0) {
            const double norm = double(wn) / wsum;
            for (uint32_t b = 0; b < n_buildings_; ++b)
                if (req_[b] != 0) w_[b] *= norm;
        }
        w_flat_ = false;
    }

    return counted ? sum / double(counted) : 0.0;
}

void Optimizer::buildOwnIndex() {
    std::vector<uint32_t> counts(n_buildings_ + 1, 0);
    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        ++counts[dom_.edgeBuilding(cache_.cands[ci].edge) + 1];
    for (uint32_t b = 0; b < n_buildings_; ++b) counts[b + 1] += counts[b];
    own_start_ = counts;
    own_items_.resize(counts[n_buildings_]);
    std::vector<uint32_t> cursor(own_start_.begin(), own_start_.end() - 1);
    for (uint32_t ci = 0; ci < n_cands_; ++ci)
        own_items_[cursor[dom_.edgeBuilding(cache_.cands[ci].edge)]++] = ci;
}

void Optimizer::setPhiShape(double exponent) {
    phi_linear_ = std::fabs(exponent - 1.0) < 1e-12;
    if (phi_linear_) {
        phi_table_.clear();
        return;
    }
    phi_table_.resize(kPhiTableSize);
    for (uint32_t i = 0; i < kPhiTableSize; ++i)
        phi_table_[i] = std::pow(double(i) / double(kPhiTableSize - 1), exponent);
}

uint32_t Optimizer::setThreshold(double tau, double margin) {
    std::vector<uint64_t> all((layout_.total_slots + 63) / 64, 0ULL);
    for (uint32_t ci = 0; ci < n_cands_; ++ci) {
        uint32_t n;
        const Run* runs = cache_.runsOf(ci, n);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t s = runs[i].start, e = s + runs[i].len - 1;
            const uint32_t w0 = s >> 6, w1 = e >> 6;
            if (w0 == w1) {
                all[w0] |= maskFrom(s & 63) & maskTo(e & 63);
            } else {
                all[w0] |= maskFrom(s & 63);
                for (uint32_t w = w0 + 1; w < w1; ++w) all[w] = ~0ULL;
                all[w1] |= maskTo(e & 63);
            }
        }
    }

    uint32_t excluded = 0;
    active_.clear();
    for (uint32_t b = 0; b < n_buildings_; ++b) {
        const uint32_t start = layout_.slot_offset[b];
        const uint32_t n = layout_.count(b);
        uint32_t reach = 0;
        for (uint32_t s = start; s < start + n; ++s)
            if (all[s >> 6] >> (s & 63) & 1ULL) ++reach;
        const uint32_t need = layout_.required(b, tau);
        if (reach < need) {
            req_[b] = 0;
            ++excluded;
        } else if (margin <= 0.0) {
            req_[b] = need;
        } else {
            uint32_t want = layout_.required(b, tau * (1.0 + margin));
            if (want < need) want = need;
            if (want > reach) want = reach;
            req_[b] = want;
        }
        if (req_[b] != 0) active_.push_back(b);
    }
    return excluded;
}

void Optimizer::buildBundles(const OptimizeParams& p) {
    bundle_start_.assign(n_buildings_ + 1, 0);
    bundles_.clear();
    if (p.bundles_per_building == 0) return;

    std::vector<std::vector<Bundle>> per_b(n_buildings_);

#pragma omp parallel for schedule(dynamic, 64)
    for (long long bb = 0; bb < (long long)n_buildings_; ++bb) {
        const uint32_t b = uint32_t(bb);
        const uint32_t req = req_[b];
        if (req == 0) continue;
        const uint32_t lo = own_start_[b], hi = own_start_[b + 1];
        if (hi - lo < 2) continue;

        std::vector<uint32_t> own;
        const uint32_t n_own = hi - lo;
        if (n_own <= p.bundle_own_cap) {
            own.assign(own_items_.begin() + lo, own_items_.begin() + hi);
        } else {
            for (uint32_t i = 0; i < p.bundle_own_cap; ++i)
                own.push_back(own_items_[lo + uint32_t(uint64_t(i) * n_own / p.bundle_own_cap)]);
        }

        const uint32_t base = layout_.slot_offset[b];
        const uint32_t n_slots = layout_.count(b);
        const uint32_t words = (n_slots + 63) / 64;

        std::vector<uint64_t> masks(size_t(own.size()) * words, 0ULL);
        std::vector<uint32_t> alone(own.size(), 0);
        for (size_t i = 0; i < own.size(); ++i) {
            uint64_t* m = masks.data() + i * words;
            uint32_t n;
            const Run* runs = cache_.runsOf(own[i], n);
            for (uint32_t j = 0; j < n; ++j) {
                if (layout_.buildingOfSlot(runs[j].start) != b) continue;
                const uint32_t s = runs[j].start - base;
                const uint32_t e = s + runs[j].len - 1;
                const uint32_t w0 = s >> 6, w1 = e >> 6;
                if (w0 == w1) {
                    m[w0] |= maskFrom(s & 63) & maskTo(e & 63);
                } else {
                    m[w0] |= maskFrom(s & 63);
                    for (uint32_t w = w0 + 1; w < w1; ++w) m[w] = ~0ULL;
                    m[w1] |= maskTo(e & 63);
                }
            }
            uint32_t c = 0;
            for (uint32_t w = 0; w < words; ++w) c += uint32_t(__builtin_popcountll(m[w]));
            alone[i] = c;
        }

        struct Cand2 {
            uint32_t i, j, cover;
        };
        std::vector<Cand2> found;
        for (size_t i = 0; i < own.size(); ++i) {
            if (alone[i] >= req) continue;
            const uint64_t* mi = masks.data() + i * words;
            for (size_t j = i + 1; j < own.size(); ++j) {
                if (alone[j] >= req) continue;
                if (alone[i] + alone[j] < req) continue;
                const uint64_t* mj = masks.data() + j * words;
                uint32_t c = 0;
                for (uint32_t w = 0; w < words; ++w)
                    c += uint32_t(__builtin_popcountll(mi[w] | mj[w]));
                if (c >= req) found.push_back({uint32_t(i), uint32_t(j), c});
            }
        }
        if (found.empty()) continue;

        std::sort(found.begin(), found.end(),
                  [](const Cand2& a, const Cand2& c) { return a.cover > c.cover; });

        std::vector<uint8_t> used(own.size(), 0);
        std::vector<Bundle>& dst = per_b[b];
        for (const Cand2& c : found) {
            if (dst.size() >= p.bundles_per_building) break;
            if (used[c.i] && used[c.j]) continue;
            used[c.i] = used[c.j] = 1;
            dst.push_back({own[c.i], own[c.j], b});
        }
    }

    for (uint32_t b = 0; b < n_buildings_; ++b)
        bundle_start_[b + 1] = bundle_start_[b] + uint32_t(per_b[b].size());
    bundles_.resize(bundle_start_[n_buildings_]);
    for (uint32_t b = 0; b < n_buildings_; ++b)
        std::copy(per_b[b].begin(), per_b[b].end(), bundles_.begin() + bundle_start_[b]);
}

uint32_t Optimizer::countNewSlots(const SearchState& st, uint32_t start, uint32_t len) const {
    const uint32_t end = start + len - 1;
    const uint32_t w0 = start >> 6, w1 = end >> 6;
    if (w0 == w1)
        return uint32_t(
            __builtin_popcountll(~st.bits[w0] & maskFrom(start & 63) & maskTo(end & 63)));
    uint32_t total = uint32_t(__builtin_popcountll(~st.bits[w0] & maskFrom(start & 63)));
    for (uint32_t w = w0 + 1; w < w1; ++w) total += uint32_t(__builtin_popcountll(~st.bits[w]));
    total += uint32_t(__builtin_popcountll(~st.bits[w1] & maskTo(end & 63)));
    return total;
}

void Optimizer::gain(SearchState& st, uint32_t ci, double* dphi, int* dscore) const {
    uint32_t n;
    const Run* runs = cache_.runsOf(ci, n);
    ++st.touch_epoch;
    st.touch_b.clear();
    st.touch_d.clear();

    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t b = layout_.buildingOfSlot(runs[i].start);
        if (req_[b] == 0) continue;
        const uint32_t add = countNewSlots(st, runs[i].start, runs[i].len);
        if (add == 0) continue;
        if (st.touch_stamp[b] != st.touch_epoch) {
            st.touch_stamp[b] = st.touch_epoch;
            st.touch_b.push_back(b);
            st.touch_d.push_back(add);
        } else {
            for (size_t j = st.touch_b.size(); j-- > 0;)
                if (st.touch_b[j] == b) {
                    st.touch_d[j] += add;
                    break;
                }
        }
    }

    double dp = 0.0;
    int ds = 0;
    for (size_t j = 0; j < st.touch_b.size(); ++j) {
        const uint32_t b = st.touch_b[j];
        const uint32_t r = req_[b];
        dp += phiOf(b, st.cov[b] + st.touch_d[j], r) - phiOf(b, st.cov[b], r);
        if (st.cov[b] < r && st.cov[b] + st.touch_d[j] >= r) ++ds;
    }
    *dphi = dp;
    *dscore = ds;
}

void Optimizer::gainPair(SearchState& st, uint32_t c0, uint32_t c1, double* dphi,
                         int* dscore) const {
    const double phi0 = st.phi;
    const uint32_t score0 = st.score;
    addCandidate(st, c0);
    double dphi1;
    int ds1;
    gain(st, c1, &dphi1, &ds1);
    const double dphi_total = (st.phi - phi0) + dphi1;
    const int ds_total = int(st.score - score0) + ds1;
    removeCandidate(st, c0);
    *dphi = dphi_total;
    *dscore = ds_total;
}

void Optimizer::addCandidate(SearchState& st, uint32_t ci) const {
    uint32_t n;
    const Run* runs = cache_.runsOf(ci, n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t s = runs[i].start;
        const uint32_t b = layout_.buildingOfSlot(s);
        uint32_t fresh = 0;
        for (uint32_t t = s; t < s + runs[i].len; ++t)
            if (st.cnt[t]++ == 0) {
                st.bits[t >> 6] |= 1ULL << (t & 63);
                ++fresh;
            }
        if (fresh == 0 || req_[b] == 0) continue;
        const uint32_t r = req_[b];
        const double before = phiOf(b, st.cov[b], r);
        const bool was_done = st.cov[b] >= r;
        st.cov[b] += fresh;
        st.phi += phiOf(b, st.cov[b], r) - before;
        if (!was_done && st.cov[b] >= r) ++st.score;
    }
}

void Optimizer::removeCandidate(SearchState& st, uint32_t ci) const {
    uint32_t n;
    const Run* runs = cache_.runsOf(ci, n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t s = runs[i].start;
        const uint32_t b = layout_.buildingOfSlot(s);
        uint32_t lost = 0;
        for (uint32_t t = s; t < s + runs[i].len; ++t)
            if (--st.cnt[t] == 0) {
                st.bits[t >> 6] &= ~(1ULL << (t & 63));
                ++lost;
            }
        if (lost == 0 || req_[b] == 0) continue;
        const uint32_t r = req_[b];
        const double before = phiOf(b, st.cov[b], r);
        const bool was_done = st.cov[b] >= r;
        st.cov[b] -= lost;
        st.phi += phiOf(b, st.cov[b], r) - before;
        if (was_done && st.cov[b] < r) --st.score;
    }
}

void Optimizer::loadSelection(SearchState& st, const std::vector<uint32_t>& sel) const {
    st.clear();
    for (uint32_t ci : sel) {
        addCandidate(st, ci);
        st.chosen[ci] = 1;
        st.sel.push_back(ci);
    }
}

std::vector<double> Optimizer::slotRatios(const std::vector<uint32_t>& sel) const {
    SearchState st;
    st.init(n_buildings_, layout_.total_slots, n_cands_);
    for (uint32_t ci : sel) addCandidate(st, ci);
    std::vector<double> out(n_buildings_, 0.0);
    for (uint32_t b = 0; b < n_buildings_; ++b)
        out[b] = double(st.cov[b]) / double(layout_.count(b));
    return out;
}

void Optimizer::greedy(const OptimizeParams& p, SearchState& st, uint64_t& rng,
                       bool randomised) const {
    struct QEntry {
        double val;
        uint32_t id;
        uint32_t stamp;
        bool bundle;
        bool operator<(const QEntry& o) const { return val < o.val; }
    };
    const uint32_t kBound = 0xFFFFFFFFu;

    std::priority_queue<QEntry> pq;
    std::vector<double> single_val(n_cands_, 0.0);

    for (uint32_t ci = 0; ci < n_cands_; ++ci) {
        double dphi;
        int ds;
        gain(st, ci, &dphi, &ds);
        const double v = value(dphi, ds, p.completion_weight);
        single_val[ci] = v;
        if (dphi > 0.0 || ds > 0) pq.push({v, ci, 0, false});
    }

    for (uint32_t bi = 0; bi < uint32_t(bundles_.size()); ++bi) {
        const Bundle& bu = bundles_[bi];
        const double bound =
            (single_val[bu.c0] + single_val[bu.c1] + p.completion_weight) * 0.5;
        pq.push({bound, bi, kBound, true});
    }

    uint32_t stamp = 0;
    std::vector<QEntry> choices;

    while (st.sel.size() < p.k && !pq.empty()) {
        const uint32_t width =
            (randomised && randUnit(rng) < p.grasp_prob) ? std::max(1u, p.grasp_width) : 1u;

        choices.clear();
        int guard = int(4 * width) + 16384;

        while (choices.size() < width && !pq.empty() && guard-- > 0) {
            QEntry top = pq.top();
            pq.pop();

            if (top.bundle) {
                const Bundle& bu = bundles_[top.id];
                if (st.chosen[bu.c0] || st.chosen[bu.c1]) continue;
                if (st.sel.size() + 2 > p.k) continue;
                if (req_[bu.target] == 0 || st.cov[bu.target] >= req_[bu.target]) continue;
            } else {
                if (st.chosen[top.id]) continue;
            }

            if (top.stamp != stamp) {
                double dphi;
                int ds;
                if (top.bundle) {
                    const Bundle& bu = bundles_[top.id];
                    gainPair(st, bu.c0, bu.c1, &dphi, &ds);
                    top.val = value(dphi, ds, p.completion_weight) * 0.5;
                } else {
                    gain(st, top.id, &dphi, &ds);
                    top.val = value(dphi, ds, p.completion_weight);
                }
                top.stamp = stamp;
                if (!pq.empty() && top.val < pq.top().val) {
                    pq.push(top);
                    continue;
                }
            }
            choices.push_back(top);
        }

        if (choices.empty()) {
            while (!pq.empty()) {
                QEntry top = pq.top();
                pq.pop();
                double dphi;
                int ds;
                if (top.bundle) {
                    const Bundle& bu = bundles_[top.id];
                    if (st.chosen[bu.c0] || st.chosen[bu.c1]) continue;
                    if (st.sel.size() + 2 > p.k) continue;
                    if (req_[bu.target] == 0 || st.cov[bu.target] >= req_[bu.target]) continue;
                    gainPair(st, bu.c0, bu.c1, &dphi, &ds);
                    top.val = value(dphi, ds, p.completion_weight) * 0.5;
                } else {
                    if (st.chosen[top.id]) continue;
                    gain(st, top.id, &dphi, &ds);
                    top.val = value(dphi, ds, p.completion_weight);
                }
                top.stamp = stamp;
                choices.push_back(top);
                break;
            }
        }

        if (choices.empty()) break;
        const size_t pick = randomised ? randBelow(rng, uint32_t(choices.size())) : 0;
        for (size_t i = 0; i < choices.size(); ++i)
            if (i != pick) pq.push(choices[i]);

        const QEntry& mv = choices[pick];
        if (mv.bundle) {
            const Bundle& bu = bundles_[mv.id];
            const uint32_t pair[2] = {bu.c0, bu.c1};
            for (uint32_t c : pair) {
                addCandidate(st, c);
                st.chosen[c] = 1;
                st.sel.push_back(c);
            }
        } else {
            addCandidate(st, mv.id);
            st.chosen[mv.id] = 1;
            st.sel.push_back(mv.id);
        }
        ++stamp;
    }

    for (uint32_t ci = 0; ci < n_cands_ && st.sel.size() < p.k; ++ci) {
        if (st.chosen[ci]) continue;
        addCandidate(st, ci);
        st.chosen[ci] = 1;
        st.sel.push_back(ci);
    }
}

long long Optimizer::lnsRound(const OptimizeParams& p, SearchState& st, Solution& best,
                              uint64_t& rng, double until) const {
    if (st.sel.empty()) return 0;

    uint32_t cur_score = st.score;
    double cur_phi = st.phi;
    std::vector<uint32_t> removed, added, pool, disturbed;
    struct Ranked {
        double val;
        uint32_t ci;
    };
    std::vector<Ranked> ranked;
    std::vector<std::pair<double, uint32_t>> keyed;
    long long iters = 0;

    while (nowSec() < until) {
        ++iters;
        const int span = std::max(p.destroy_min, p.destroy_max) - p.destroy_min + 1;
        const int r = std::min<int>(p.destroy_min + int(randBelow(rng, uint32_t(span))),
                                    int(st.sel.size()));
        if (r <= 0) break;

        removed.clear();
        uint32_t target_b = UINT32_MAX;
        const uint32_t mode = randBelow(rng, p.target_destroy ? 4u : 3u);

        if (mode == 3 && !active_.empty()) {
            uint32_t best_deficit = UINT32_MAX;
            for (uint32_t s = 0; s < p.target_sample; ++s) {
                const uint32_t b = active_[randBelow(rng, uint32_t(active_.size()))];
                if (st.cov[b] >= req_[b]) continue;
                const uint32_t deficit = req_[b] - st.cov[b];
                if (deficit < best_deficit) {
                    best_deficit = deficit;
                    target_b = b;
                }
            }
        }

        if (mode == 1 && int(st.sel.size()) > r) {
            const uint32_t anchor = st.sel[randBelow(rng, uint32_t(st.sel.size()))];
            const Vec2 ap = cand_pos_[anchor];
            keyed.clear();
            for (uint32_t ci : st.sel) keyed.emplace_back((cand_pos_[ci] - ap).norm2(), ci);
            std::partial_sort(keyed.begin(), keyed.begin() + r, keyed.end());
            for (int i = 0; i < r; ++i) removed.push_back(keyed[i].second);
        } else if ((mode == 2 || target_b != UINT32_MAX) && int(st.sel.size()) > r) {
            const uint32_t n_sample = std::min<uint32_t>(24, uint32_t(st.sel.size()));
            keyed.clear();
            for (uint32_t i = 0; i < n_sample; ++i) {
                const uint32_t ci = st.sel[randBelow(rng, uint32_t(st.sel.size()))];
                bool dup = false;
                for (const auto& e : keyed)
                    if (e.second == ci) dup = true;
                if (dup) continue;
                uint32_t n;
                const Run* runs = cache_.runsOf(ci, n);
                uint32_t unique = 0;
                for (uint32_t j = 0; j < n; ++j) {
                    const uint32_t b = layout_.buildingOfSlot(runs[j].start);
                    if (req_[b] == 0) continue;
                    for (uint32_t t = runs[j].start; t < runs[j].start + runs[j].len; ++t)
                        if (st.cnt[t] == 1) ++unique;
                }
                keyed.emplace_back(double(unique), ci);
            }
            std::sort(keyed.begin(), keyed.end());
            for (const auto& e : keyed) {
                if (int(removed.size()) >= r) break;
                removed.push_back(e.second);
            }
        } else {
            for (int i = 0; i < r && removed.size() < st.sel.size(); ++i) {
                uint32_t ci;
                do {
                    ci = st.sel[randBelow(rng, uint32_t(st.sel.size()))];
                } while (std::find(removed.begin(), removed.end(), ci) != removed.end());
                removed.push_back(ci);
            }
        }

        for (uint32_t ci : removed) {
            st.sel.erase(std::find(st.sel.begin(), st.sel.end(), ci));
            removeCandidate(st, ci);
            st.chosen[ci] = 0;
        }
        const int budget = int(removed.size());

        pool.clear();
        disturbed.clear();
        ++st.touch_epoch;
        for (uint32_t ci : removed) {
            for (uint32_t j = cb_start_[ci]; j < cb_start_[ci + 1]; ++j) {
                const uint32_t b = cb_items_[j];
                if (st.touch_stamp[b] == st.touch_epoch) continue;
                st.touch_stamp[b] = st.touch_epoch;
                if (req_[b] == 0 || st.cov[b] >= req_[b]) continue;
                disturbed.push_back(b);
                for (uint32_t q = bc_start_[b]; q < bc_start_[b + 1]; ++q)
                    pool.push_back(bc_items_[q]);
            }
        }
        for (uint32_t i = 0; i < p.repair_sample; ++i) pool.push_back(randBelow(rng, n_cands_));
        std::sort(pool.begin(), pool.end());
        pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
        if (pool.size() > p.pool_cap) {
            for (uint32_t i = 0; i < p.pool_cap; ++i)
                std::swap(pool[i], pool[i + randBelow(rng, uint32_t(pool.size() - i))]);
            pool.resize(p.pool_cap);
        }

        if (target_b != UINT32_MAX && st.cov[target_b] < req_[target_b]) {
            if (st.touch_stamp[target_b] != st.touch_epoch) {
                st.touch_stamp[target_b] = st.touch_epoch;
                disturbed.push_back(target_b);
            }
            const uint32_t lo = bc_start_[target_b], hi = bc_start_[target_b + 1];
            const uint32_t avail = hi - lo;
            const uint32_t take = std::min(avail, p.target_pool);
            if (take == avail) {
                for (uint32_t q = lo; q < hi; ++q) pool.push_back(bc_items_[q]);
            } else {
                const uint32_t step = avail / take;
                const uint32_t off = randBelow(rng, step);
                for (uint32_t j = 0; j < take; ++j) pool.push_back(bc_items_[lo + j * step + off]);
            }
        }

        ranked.clear();
        for (uint32_t ci : pool) {
            if (st.chosen[ci]) continue;
            double dphi;
            int ds;
            gain(st, ci, &dphi, &ds);
            ranked.push_back({value(dphi, ds, p.completion_weight), ci});
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const Ranked& a, const Ranked& b) { return a.val > b.val; });

        added.clear();
        while (int(added.size()) < budget) {
            double best_val = -1.0;
            uint32_t best_ci = UINT32_MAX;
            for (Ranked& e : ranked) {
                if (st.chosen[e.ci]) continue;
                if (e.val <= best_val) break;
                double dphi;
                int ds;
                gain(st, e.ci, &dphi, &ds);
                e.val = value(dphi, ds, p.completion_weight);
                if (e.val > best_val) {
                    best_val = e.val;
                    best_ci = e.ci;
                }
            }

            if (int(added.size()) + 2 <= budget && !disturbed.empty()) {
                uint32_t tried = 0;
                double best_pair_val = -1.0;
                uint32_t bp0 = UINT32_MAX, bp1 = UINT32_MAX;
                for (uint32_t b : disturbed) {
                    if (tried >= 8) break;
                    if (st.cov[b] >= req_[b]) continue;
                    for (uint32_t z = bundle_start_[b]; z < bundle_start_[b + 1]; ++z) {
                        if (tried >= 8) break;
                        const Bundle& bu = bundles_[z];
                        if (st.chosen[bu.c0] || st.chosen[bu.c1]) continue;
                        ++tried;
                        double dphi;
                        int ds;
                        gainPair(st, bu.c0, bu.c1, &dphi, &ds);
                        const double v = value(dphi, ds, p.completion_weight) * 0.5;
                        if (v > best_pair_val) {
                            best_pair_val = v;
                            bp0 = bu.c0;
                            bp1 = bu.c1;
                        }
                    }
                }
                if (bp0 != UINT32_MAX && best_pair_val > best_val) {
                    const uint32_t pair[2] = {bp0, bp1};
                    for (uint32_t c : pair) {
                        addCandidate(st, c);
                        st.chosen[c] = 1;
                        st.sel.push_back(c);
                        added.push_back(c);
                    }
                    continue;
                }
            }

            if (best_ci == UINT32_MAX) break;
            addCandidate(st, best_ci);
            st.chosen[best_ci] = 1;
            st.sel.push_back(best_ci);
            added.push_back(best_ci);
            std::sort(ranked.begin(), ranked.end(),
                      [](const Ranked& a, const Ranked& b) { return a.val > b.val; });
        }

        bool keep =
            st.score > cur_score || (st.score == cur_score && st.phi >= cur_phi - 1e-12);
        if (!keep && st.score == cur_score && st.phi >= cur_phi - 1.0 && randUnit(rng) < 0.02)
            keep = true;

        if (keep) {
            cur_score = st.score;
            cur_phi = st.phi;
            if (st.score > best.score ||
                (st.score == best.score && st.phi > best.phi + 1e-12)) {
                best.cand_idx = st.sel;
                best.score = st.score;
                best.phi = st.phi;
            }
        } else {
            for (uint32_t ci : added) {
                removeCandidate(st, ci);
                st.chosen[ci] = 0;
                st.sel.erase(std::find(st.sel.begin(), st.sel.end(), ci));
            }
            for (uint32_t ci : removed) {
                addCandidate(st, ci);
                st.chosen[ci] = 1;
                st.sel.push_back(ci);
            }
            cur_score = st.score;
            cur_phi = st.phi;
        }
    }
    return iters;
}

std::vector<Solution> Optimizer::solve(const OptimizeParams& p) {
    setPhiShape(p.phi_exponent);

    const double t_diff = nowSec();
    const double mean_m = applyDifficulty(p);
    if (p.verbose && mean_m > 0.0) {
        size_t hard = 0;
        for (uint32_t b = 0; b < n_buildings_; ++b)
            if (difficulty_[b] > 3) ++hard;
        std::fprintf(stderr,
                     "  difficulty: mean m_b %.2f, %zu buildings need >3 antennas, "
                     "%zu active (%.1f s)\n",
                     mean_m, hard, active_.size(), nowSec() - t_diff);
    }

    const double t_bundles = nowSec();
    buildBundles(p);
    if (p.verbose)
        std::fprintf(stderr, "  bundles: %zu completing pairs (%.1f s)\n", bundles_.size(),
                     nowSec() - t_bundles);

    const int n_rep = std::max(1, p.replicas);
    const double t_start = nowSec();
    const double t_end = t_start + p.time_budget_sec;

    const size_t n_rep_z = size_t(n_rep);
    std::vector<SearchState> states(n_rep_z);
    std::vector<Solution> bests(n_rep_z);
    std::vector<uint64_t> rngs(n_rep_z);
    for (int r = 0; r < n_rep; ++r)
        rngs[r] = seedFrom(p.seed + uint64_t(r) * 0x9E3779B97F4A7C15ULL);

#pragma omp parallel for schedule(static, 1)
    for (int r = 0; r < n_rep; ++r) {
        states[r].init(n_buildings_, layout_.total_slots, n_cands_);
        greedy(p, states[r], rngs[r], r != 0);
        bests[r].cand_idx = states[r].sel;
        bests[r].score = states[r].score;
        bests[r].phi = states[r].phi;
    }

    std::vector<uint32_t> ex_val(n_rep_z, 0);
    std::vector<uint32_t> ex_at_score(n_rep_z, UINT32_MAX);
    std::vector<double> ex_at_phi(n_rep_z, -1.0);
    long long ex_evals = 0;

    auto leader = [&]() {
        int lead = 0;
        for (int r = 1; r < n_rep; ++r)
            if (bests[r].score > bests[lead].score ||
                (bests[r].score == bests[lead].score && bests[r].phi > bests[lead].phi))
                lead = r;
        if (!p.exact_score || p.exact_top <= 0 || n_rep < 2) return lead;

        std::vector<std::pair<uint32_t, int>> byslot;
        byslot.reserve(n_rep_z);
        for (int r = 0; r < n_rep; ++r) byslot.emplace_back(bests[r].score, r);
        const size_t top = std::min<size_t>(size_t(p.exact_top), n_rep_z);
        std::partial_sort(byslot.begin(), byslot.begin() + top, byslot.end(),
                          [](const std::pair<uint32_t, int>& a,
                             const std::pair<uint32_t, int>& b) { return a.first > b.first; });

        int best_r = -1;
        uint32_t best_ex = 0;
        for (size_t i = 0; i < top; ++i) {
            const int r = byslot[i].second;
            if (bests[r].cand_idx.empty()) continue;
            if (ex_at_score[size_t(r)] != bests[r].score ||
                ex_at_phi[size_t(r)] != bests[r].phi) {
                ex_val[size_t(r)] = p.exact_score(bests[r].cand_idx);
                ex_at_score[size_t(r)] = bests[r].score;
                ex_at_phi[size_t(r)] = bests[r].phi;
                ++ex_evals;
            }
            if (best_r < 0 || ex_val[size_t(r)] > best_ex) {
                best_ex = ex_val[size_t(r)];
                best_r = r;
            }
        }
        return best_r < 0 ? lead : best_r;
    };
    auto spread = [&](uint32_t& lo, uint32_t& hi) {
        lo = hi = bests[0].score;
        for (const Solution& s : bests) {
            lo = std::min(lo, s.score);
            hi = std::max(hi, s.score);
        }
    };

    int lead = leader();
    if (p.verbose) {
        uint32_t lo, hi;
        spread(lo, hi);
        std::fprintf(stderr,
                     "  greedy: %zu antennas, slot-score %u (replicas %u..%u), phi %.1f "
                     "(%.1f s)\n",
                     bests[lead].cand_idx.size(), bests[lead].score, lo, hi, bests[lead].phi,
                     nowSec() - t_start);
    }

    long long total_iters = 0;
    int round = 0;
    while (nowSec() < t_end) {
        const double until = std::min(t_end, nowSec() + p.sync_interval_sec);
        std::vector<long long> iters(size_t(n_rep), 0);

#pragma omp parallel for schedule(static, 1)
        for (int r = 0; r < n_rep; ++r)
            iters[r] = lnsRound(p, states[r], bests[r], rngs[r], until);
        for (long long i : iters) total_iters += i;

        lead = leader();
        const Solution gb = bests[lead];

        std::vector<std::pair<uint32_t, int>> rank;
        rank.reserve(n_rep_z);
        for (int r = 0; r < n_rep; ++r) rank.emplace_back(bests[r].score, r);
        std::sort(rank.begin(), rank.end(),
                  [](const std::pair<uint32_t, int>& a, const std::pair<uint32_t, int>& b) {
                      return a.first > b.first;
                  });
        std::vector<int> recyclable;
        for (size_t i = n_rep_z; i-- > n_rep_z / 2;)
            if (rank[i].second != lead && bests[rank[i].second].score < gb.score)
                recyclable.push_back(rank[i].second);

        std::vector<Solution> foreign;
        size_t adopted = 0;
        uint32_t best_foreign = 0;
        if (!p.share_dir.empty()) {
            const uint64_t sig =
                shareSignature(p.tau, p.k, n_cands_, layout_.total_slots);
            publishElite(p.share_dir, p.island, sig, gb);
            foreign = readForeignElites(p.share_dir, p.island, sig, n_cands_, p.k);
            std::sort(foreign.begin(), foreign.end(),
                      [](const Solution& a, const Solution& b) { return a.score > b.score; });
            if (!foreign.empty()) best_foreign = foreign[0].score;

            const size_t want = size_t(std::max(0, p.immigrants));
            if (recyclable.empty() && !foreign.empty() && foreign[0].score > gb.score) {
                int worst = rank.back().second;
                if (worst != lead) recyclable.push_back(worst);
            }
            for (size_t i = 0; i < foreign.size() && adopted < want && adopted < recyclable.size();
                 ++i) {
                const int r = recyclable[adopted];
                loadSelection(states[r], foreign[i].cand_idx);
                bests[r].cand_idx = states[r].sel;
                bests[r].score = states[r].score;
                bests[r].phi = states[r].phi;
                ++adopted;
            }
        }

        int pulled = 0;
        const int n_recycle = int(recyclable.size());
#pragma omp parallel for schedule(static, 1) reduction(+ : pulled)
        for (int i = int(adopted); i < n_recycle; ++i) {
            loadSelection(states[recyclable[size_t(i)]], gb.cand_idx);
            bests[recyclable[size_t(i)]] = gb;
            ++pulled;
        }

        ++round;
        if (p.verbose) {
            std::fprintf(stderr,
                         "  round %d: %lld iterations total, best slot-score %u, %d replicas "
                         "pulled onto it (%.0f s elapsed)\n",
                         round, total_iters, gb.score, pulled, nowSec() - t_start);
            if (!p.share_dir.empty())
                std::fprintf(stderr,
                             "    islands: %zu foreign elites seen, best %u, %zu adopted\n",
                             foreign.size(), best_foreign, adopted);
        }
        if (p.on_sync) p.on_sync(gb);
    }

    if (p.verbose) {
        uint32_t lo, hi;
        spread(lo, hi);
        std::fprintf(stderr, "  %d replicas, %lld LNS iterations, slot-score spread %u..%u\n",
                     n_rep, total_iters, lo, hi);
    }
    std::sort(bests.begin(), bests.end(), [](const Solution& a, const Solution& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.phi > b.phi;
    });
    return bests;
}

}
