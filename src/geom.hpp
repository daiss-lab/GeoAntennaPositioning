// geom.hpp -- 2-D primitives and the orientation predicates the solver relies on.
//
// Every predicate here follows Definition 1 of the problem statement: visibility is
// blocked only when a segment meets the *interior* of a footprint.  Touching a wall,
// running along a wall, or grazing a vertex does NOT block.  Wherever a test is
// numerically ambiguous we therefore resolve it as "does not block", which is both the
// spec-conformant choice and the one that keeps a tangent sight-line alive.
#pragma once

#include <cmath>
#include <algorithm>

namespace gis {

struct Vec2 {
    double x = 0.0, y = 0.0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }

    double norm2() const { return x * x + y * y; }
    double norm() const { return std::sqrt(x * x + y * y); }
};

inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

// Tolerance for orientation signs.
//
// A purely relative band of eps*|u|*|v| is wrong here.  The vectors are differences of
// points that sit thousands of metres from the origin, so each carries an absolute
// rounding error of order eps*|coordinate| regardless of how short it is.  The error in
// cross(u,v) is therefore proportional to (|u| + |v|), not to |u|*|v|.  Comparing a 4 cm
// vector against a 6 m one, the relative band lands *below* the actual noise floor and
// the predicate starts inventing turns in configurations that are exactly collinear.
//
// The band mixes both terms.  kOrientAbsEps is in metres and acts as positional slack:
// a nanometre of perpendicular deviation is treated as collinear, which is far below
// anything geometrically meaningful in a dataset measured in metres, and it resolves
// ties towards "collinear" -- hence towards "does not block", matching Definition 1.
constexpr double kOrientRelEps = 1e-12;
constexpr double kOrientAbsEps = 1e-9;

inline double orientTol(const Vec2& u, const Vec2& v) {
    const double lu = u.norm(), lv = v.norm();
    return kOrientRelEps * lu * lv + kOrientAbsEps * (lu + lv);
}

// Sign of the cross product (b-a) x (c-a): +1 left turn, -1 right turn, 0 collinear.
inline int orient(const Vec2& a, const Vec2& b, const Vec2& c) {
    const Vec2 u = b - a;
    const Vec2 v = c - a;
    const double d = cross(u, v);
    const double tol = orientTol(u, v);
    if (d > tol) return 1;
    if (d < -tol) return -1;
    return 0;
}

// True only for a transversal crossing: the two open segments meet at a single point
// interior to both.  Shared endpoints, T-junctions and collinear overlap all return
// false, which is exactly the tangency rule of Definition 1.
inline bool properCross(const Vec2& p1, const Vec2& p2, const Vec2& q1, const Vec2& q2) {
    const int o1 = orient(p1, p2, q1);
    const int o2 = orient(p1, p2, q2);
    if (o1 == 0 || o2 == 0 || o1 == o2) return false;
    const int o3 = orient(q1, q2, p1);
    const int o4 = orient(q1, q2, p2);
    if (o3 == 0 || o4 == 0 || o3 == o4) return false;
    return true;
}

// Sign of cross(u, v), using the same mixed zero band as orient().
inline int crossSign(const Vec2& u, const Vec2& v) {
    const double d = cross(u, v);
    const double tol = orientTol(u, v);
    if (d > tol) return 1;
    if (d < -tol) return -1;
    return 0;
}

// Squared distance from p to segment [a,b], plus the clamped parameter along it.
inline double segPointDist2(const Vec2& a, const Vec2& b, const Vec2& p, double* t_out) {
    const Vec2 ab = b - a;
    const double len2 = ab.norm2();
    double t = 0.0;
    if (len2 > 0.0) t = std::max(0.0, std::min(1.0, dot(p - a, ab) / len2));
    if (t_out) *t_out = t;
    const Vec2 q = a + ab * t;
    return (p - q).norm2();
}

// --- angular helpers -------------------------------------------------------------
// Angles are taken with atan2 into [-pi, pi).  They are used for bucketing and for
// sweep ordering only; every *decision* about blocking goes through the exact
// orientation predicates above.

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline double angleOf(const Vec2& d) { return std::atan2(d.y, d.x); }

// Normalise into [0, 2*pi).
inline double normAngle(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}

// Counter-clockwise sweep from angle `from` to angle `to`, in [0, 2*pi).
inline double ccwSpan(double from, double to) {
    double d = to - from;
    if (d < 0.0) d += kTwoPi;
    return d;
}

}  // namespace gis
