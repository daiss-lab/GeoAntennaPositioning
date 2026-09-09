// refine.hpp -- continuous refinement of a finished placement.
//
// Solution01 and Solution02 both solve a *doubly discretised* version of the problem.
// Antenna positions are restricted to a fixed candidate set (every vertex plus interior
// points every 4 m), and coverage is measured on a 0.1 m slot grid.  Both discretisations
// are choices of convenience, and the first one is the expensive one: the true feasible
// set is continuous -- every point of every boundary is a legal antenna -- and a 4 m grid
// can only ever land near the right answer.
//
// The structure that makes the continuum tractable is standard visibility geometry.  Slide
// an antenna along its host edge and the set of visible boundary intervals does not vary
// smoothly; it is piecewise constant in a combinatorial sense, changing only at
// *tangency events* -- parameters where the antenna, some occluding vertex and some target
// vertex become collinear.  Between two consecutive events the visible set has a fixed
// combinatorial description and the covered length varies continuously.  So the positions
// worth testing on an edge are its events, not a uniform grid, and a grid is both wasteful
// where the geometry is open and too coarse where it is intricate.
//
// This module takes a finished answer and runs coordinate ascent over that continuum: each
// antenna in turn is moved to the best position on its own host edge, holding the others
// fixed, with every trial scored by the exact arc evaluator rather than by the slot proxy.
// It accepts a move only when the exact number of serviced buildings strictly improves, so
// it can never make an answer worse than the one it was given.
#pragma once

#include <cstdint>
#include <string>

namespace gis {

struct RefineOptions {
    std::string data;
    std::string solution;
    std::string out;      // where to write the refined answer; empty means in place
    double tau = -1.0;    // < 0 means "take it from the solution file"

    int rounds = 4;       // sweeps over all antennas; stops early when a sweep gains nothing
    int trials = 48;      // candidate positions tested per antenna per sweep
    int event_verts = 28; // nearest vertices used to generate tangency events for an edge
    double event_radius = 220.0;  // metres; how far to look for those vertices

    // Across-edge relocation.  Sliding an antenna along the edge `solve` happened to put it
    // on cannot recover from `solve` having chosen the wrong edge in the first place, and
    // Solution03 measured that restriction, not the sampling density, as the binding
    // constraint: its ascent reached a zero-move round after two sweeps, and five times the
    // within-edge density bought only ten more buildings across the whole grid.
    //
    // So each antenna is also offered the start vertex of every edge within
    // `relocate_radius` of where it currently stands, nearest first, capped at
    // `relocate_edges`.  Vertices rather than interior points because a convex corner has
    // the widest aperture of any boundary point and sees both its walls in full, which is
    // the effect the whole approach runs on -- and because a vertex coordinate is exact to
    // write into the answer file.  0 disables, restoring Solution03's within-edge ascent.
    int relocate_edges = 48;
    double relocate_radius = 120.0;

    double time_budget_sec = 0.0;  // 0 = no limit
    double deadline_epoch = 0.0;   // absolute unix time; overrides the budget when nearer

    double eval_max_radius = 0.0;  // 0 = uncapped, matching the final evaluator
    double knife_eps = 1e-9;
    double claim_band = 0.0;
    bool claim_all = false;
    int bins = 1024;
    double cell = 25.0;
    bool strict = false;
    bool include_interior_rings = false;
};

int runRefine(const RefineOptions& o);

}  // namespace gis
