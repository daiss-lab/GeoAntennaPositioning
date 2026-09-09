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

constexpr double kOrientRelEps = 1e-12;
constexpr double kOrientAbsEps = 1e-9;

inline double orientTol(const Vec2& u, const Vec2& v) {
    const double lu = u.norm(), lv = v.norm();
    return kOrientRelEps * lu * lv + kOrientAbsEps * (lu + lv);
}

inline int orient(const Vec2& a, const Vec2& b, const Vec2& c) {
    const Vec2 u = b - a;
    const Vec2 v = c - a;
    const double d = cross(u, v);
    const double tol = orientTol(u, v);
    if (d > tol) return 1;
    if (d < -tol) return -1;
    return 0;
}

inline bool properCross(const Vec2& p1, const Vec2& p2, const Vec2& q1, const Vec2& q2) {
    const int o1 = orient(p1, p2, q1);
    const int o2 = orient(p1, p2, q2);
    if (o1 == 0 || o2 == 0 || o1 == o2) return false;
    const int o3 = orient(q1, q2, p1);
    const int o4 = orient(q1, q2, p2);
    if (o3 == 0 || o4 == 0 || o3 == o4) return false;
    return true;
}

inline int crossSign(const Vec2& u, const Vec2& v) {
    const double d = cross(u, v);
    const double tol = orientTol(u, v);
    if (d > tol) return 1;
    if (d < -tol) return -1;
    return 0;
}

inline double segPointDist2(const Vec2& a, const Vec2& b, const Vec2& p, double* t_out) {
    const Vec2 ab = b - a;
    const double len2 = ab.norm2();
    double t = 0.0;
    if (len2 > 0.0) t = std::max(0.0, std::min(1.0, dot(p - a, ab) / len2));
    if (t_out) *t_out = t;
    const Vec2 q = a + ab * t;
    return (p - q).norm2();
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline double angleOf(const Vec2& d) { return std::atan2(d.y, d.x); }

inline double normAngle(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}

inline double ccwSpan(double from, double to) {
    double d = to - from;
    if (d < 0.0) d += kTwoPi;
    return d;
}

}
