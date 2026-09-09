// score_cmd.hpp -- re-score an already emitted solution file.
//
// `solve` evaluates its own placement and writes the claim line in one process, which
// means nothing ever re-reads what was actually written.  That is the wrong shape for a
// competition: the organisers score the *file*, from the coordinates in it, with no
// access to the solver's internal state.  This subcommand closes that loop -- it parses
// the emitted file exactly as a third party would, snaps each coordinate back onto the
// boundary, recomputes coverage with the exact evaluator, and reports what the file is
// really worth.
//
// It is also how the claim line gets fixed if it is ever wrong: `--rewrite` writes the
// file back with the recomputed serviced set.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.hpp"

namespace gis {

// An answer file as a stranger reading it would see: the header, the coordinates as
// written, and whatever the claim line says.  Shared with `refine`, which has to read an
// answer back before it can improve it.
struct ParsedSolution {
    double tau = 0.0;
    uint32_t k = 0;
    std::vector<Vec2> antennas;  // world coordinates, exactly as written
    std::vector<long long> claimed;
};

ParsedSolution parseSolutionFile(const std::string& path);

struct ScoreOptions {
    std::string data;
    std::string solution;
    std::string rewrite;  // optional path to write the re-scored solution to
    double tau = -1.0;    // < 0 means "take it from the solution file"
    double eval_max_radius = 0.0;
    double knife_eps = 1e-9;
    double claim_band = 0.0;
    bool claim_all = false;
    // Visible fragments shorter than this are discarded.  The default of 1e-6 m comes from
    // VisibilityParams and it is not free: a building whose coverage should land exactly on
    // tau instead lands a micrometre under it.  0 keeps every fragment.
    double min_arc_len = 1e-6;
    int bins = 1024;
    double cell = 25.0;
    bool strict = false;
    bool include_interior_rings = false;
    std::string dump_cov;
    long long explain_building_id = -1;  // narrate every arc landing on this building
};

int runScore(const ScoreOptions& o);

}  // namespace gis
