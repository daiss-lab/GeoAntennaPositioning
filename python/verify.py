#!/usr/bin/env python3
"""
verify.py -- independent check of a solution file.

This deliberately shares no code with the C++ solver.  It re-reads the GeoJSON and the
emitted answer, then recomputes coverage by *sampling* points along building perimeters
and testing each against Shapely, whereas the solver computes exact visible intervals
analytically.  Two different methods agreeing is real evidence; a solver checking its own
arithmetic is not.

Visibility follows Definition 1 exactly.  The DE-9IM relate matrix entry [0] is the
dimension of (interior of line) intersect (interior of polygon); 'F' means they do not
meet, which is precisely "the segment does not pass through the building's interior".
A sight-line running along a wall therefore does not block, matching Figure 1.

Two subtleties dominate the accuracy of this script.  Both have produced convincing false
alarms, so both are spelled out here.

1.  A sample point computed on a wall lands a few ULPs off the mathematical boundary, and
    roughly half the time that is a few ULPs *inside* the footprint -- whereupon every
    sight-line to it is judged to enter the building's interior and the point reads as
    invisible.  Sampling naively produces a striped visible/invisible pattern with the
    period of the sample step and understates coverage by a factor of two or more.  Each
    sample is therefore pushed a small distance along the outward normal, which is far
    below any real geometric feature but decisively outside the polygon.

2.  *Every* antenna must be considered, not just nearby ones.  An earlier version capped
    the search at 1200 m for speed and reported three of the nine sub-problems as FAIL.
    Building 11328 in (tau=0.25, k=50) reads 0.0407 with the cap and 0.3541 without it,
    because an antenna 1800 m away across open ground supplies 31% of its perimeter.
    Long sight-lines are real and the solver is entitled to them, so the default here is
    now uncapped.  Speed comes instead from visiting antennas nearest-first and stopping
    at the first one that sees the sample, which is what makes the uncapped search
    affordable.

3.  The *antenna* end of the sight-line is a boundary point too, and needs the same
    treatment as point 1.  An antenna is emitted as a decimal coordinate on a wall, and
    rounding puts it a few ULPs to one side; when that side is inward, Shapely's relate
    reports interior contact with the antenna's own host building for *every* line drawn
    from it, and the antenna goes completely blind.  This is not subtle in its effect:
    antenna 990 of (tau=0.5, k=1000) sits 2.8e-11 m inside building 175 and thereby lost
    all 9.52 m of coverage it supplies to building 167, which was the whole of that
    sub-problem's apparent over-claim.  Antennas are therefore pushed outward off their
    host wall before any line is drawn, exactly as samples are.

Usage:
    verify.py --geojson D.geojson --solution results/tau0.5_k500.txt [options]
"""

import argparse
import itertools
import json
import math
import random
import sys

from shapely.geometry import LineString, Point, Polygon
from shapely.strtree import STRtree


class PolyIndex:
    """An STRtree that yields integer indices whichever Shapely is installed.

    Shapely 2.0's `STRtree.query` returns indices into the input sequence; 1.8's returns
    the geometries themselves.  This file was written against the 2.0 behaviour and the
    venv has 1.8.5, so every query raised `TypeError: list indices must be integers` and
    the verifier died before checking anything.  It died quietly: finalize5.sbatch turns a
    non-zero exit into "verifier disagreed", which reads like a *result* rather than a
    crash, so the independent check has been absent from every run that reported it.
    """

    def __init__(self, polys):
        self.polys = polys
        self.tree = STRtree(polys)
        self._by_identity = {id(p): i for i, p in enumerate(polys)}
        probe = list(self.tree.query(polys[0]))
        self.returns_indices = bool(probe) and not hasattr(probe[0], "geom_type")

    def query(self, geom):
        hits = self.tree.query(geom)
        if self.returns_indices:
            return [int(h) for h in hits]
        return [self._by_identity[id(h)] for h in hits]


def canonical_id(value):
    """The id as the organisers' loader would canonicalise it: `String(value).trim()`."""
    if isinstance(value, float) and value == int(value) and abs(value) < 1e21:
        return str(int(value))
    return str(value).strip()

# Metres to push a perimeter sample outward before testing it.  Coordinates here are of
# order 5e5 m, where one ULP is about 6e-11 m, so this is ~1e3 ULPs -- unambiguous in
# floating point yet 1e7 times smaller than the shortest edge in the dataset.
BOUNDARY_OFFSET = 1e-7


def load_buildings(path):
    with open(path) as fh:
        data = json.load(fh)
    polys, ids, rings = [], [], []
    for feat in data["features"]:
        geom = feat["geometry"]
        coords = geom["coordinates"]
        if geom["type"] == "MultiPolygon":
            coords = max(coords, key=lambda r: Polygon(r[0]).area)
        ring = [tuple(p) for p in coords[0][:-1]]
        # Rewind to counter-clockwise so the interior is on the left of each edge and the
        # outward normal below is unambiguous.  This matches python/prepare.py.
        area2 = sum(ring[i][0] * ring[(i + 1) % len(ring)][1]
                    - ring[(i + 1) % len(ring)][0] * ring[i][1] for i in range(len(ring)))
        if area2 < 0:
            ring = ring[::-1]
        rings.append(ring)
        polys.append(Polygon(coords[0]))
        ids.append(canonical_id(feat["properties"]["id"]))
    return polys, ids, rings


def parse_solution(path):
    with open(path) as fh:
        lines = [ln.strip() for ln in fh if ln.strip()]
    if len(lines) < 2:
        raise SystemExit("solution file must have at least two lines")

    tau_s, k_s = lines[0].strip("() \t").split(",")
    tau, k = float(tau_s), int(k_s)

    pts = []
    for chunk in lines[1].split(")"):
        chunk = chunk.strip().lstrip(",").strip().lstrip("(")
        if not chunk:
            continue
        xs, ys = chunk.split(",")
        pts.append((float(xs), float(ys)))

    claimed = []
    if len(lines) >= 3 and lines[2]:
        claimed = [v.strip() for v in lines[2].split(",") if v.strip()]
    return tau, k, pts, claimed


def perimeter_samples(ring, step):
    """Points just outside each wall, with the length each one represents."""
    out = []
    n = len(ring)
    for i in range(n):
        ax, ay = ring[i]
        bx, by = ring[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        elen = math.hypot(ex, ey)
        if elen == 0.0:
            continue
        # Interior lies to the left of a counter-clockwise edge, so outward is to the right.
        nx, ny = ey / elen, -ex / elen
        m = max(1, int(math.ceil(elen / step)))
        for j in range(m):
            f = (j + 0.5) / m
            out.append((ax + f * ex + nx * BOUNDARY_OFFSET,
                        ay + f * ey + ny * BOUNDARY_OFFSET,
                        elen / m))
    return out


def nudge_antennas(antennas, polys, rings, tree):
    """Push each antenna a hair off its host wall, outward.

    See docstring point 3.  An antenna that rounds to the inward side of its own wall is
    read as interior by Shapely and every sight-line from it is judged blocked.  The
    outward normal is taken from the host edge; at a vertex the two adjacent edges are
    tried in turn and then their bisector, which covers reflex corners where one edge's
    normal still points into the footprint.
    """
    out = []
    stuck = 0
    for a in antennas:
        pt = Point(a)
        cands = [i for i in tree.query(pt.buffer(1e-6))
                 if polys[i].exterior.distance(pt) <= 1e-6]
        if not cands:
            out.append(a)
            continue
        hi = min(cands, key=lambda i: polys[i].exterior.distance(pt))
        ring = rings[hi]
        n = len(ring)

        # Outward normals of the edges this antenna could be standing on.  Ties at a
        # vertex are what make the list longer than one.
        best_d2 = min(_seg_dist2(ring[i], ring[(i + 1) % n], a) for i in range(n))
        normals = []
        for i in range(n):
            if _seg_dist2(ring[i], ring[(i + 1) % n], a) > best_d2 + 1e-12:
                continue
            (ax, ay), (bx, by) = ring[i], ring[(i + 1) % n]
            ex, ey = bx - ax, by - ay
            elen = math.hypot(ex, ey)
            if elen > 0.0:
                normals.append((ey / elen, -ex / elen))
        if len(normals) > 1:
            sx = sum(v[0] for v in normals)
            sy = sum(v[1] for v in normals)
            slen = math.hypot(sx, sy)
            if slen > 0.0:
                normals.append((sx / slen, sy / slen))

        for nx, ny in normals:
            q = (a[0] + nx * BOUNDARY_OFFSET, a[1] + ny * BOUNDARY_OFFSET)
            if not Point(q).within(polys[hi]):
                out.append(q)
                break
        else:
            stuck += 1
            out.append(a)
    if stuck:
        print(f"  WARNING: {stuck} antennas could not be pushed clear of their host")
    return out


def _seg_dist2(p, q, r):
    """Squared distance from point r to segment pq."""
    px, py = p
    qx, qy = q
    rx, ry = r
    dx, dy = qx - px, qy - py
    d2 = dx * dx + dy * dy
    if d2 == 0.0:
        return (rx - px) ** 2 + (ry - py) ** 2
    t = max(0.0, min(1.0, ((rx - px) * dx + (ry - py) * dy) / d2))
    return (rx - px - t * dx) ** 2 + (ry - py - t * dy) ** 2


def visible(p, q, tree, polys):
    line = LineString([p, q])
    for idx in tree.query(line):
        if line.relate(polys[idx])[0] != "F":
            return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geojson", required=True)
    ap.add_argument("--solution", required=True)
    ap.add_argument("--step", type=float, default=0.25,
                    help="perimeter sampling step in metres (0.25)")
    ap.add_argument("--sample-buildings", type=int, default=400,
                    help="how many buildings to check; 0 means all (400)")
    ap.add_argument("--antenna-radius", type=float, default=0.0,
                    help="ignore antennas beyond this distance; 0 means no limit "
                         "(0 -- see the module docstring, a limit here fabricates failures)")
    ap.add_argument("--tolerance", type=float, default=0.02,
                    help="allowed |sampled - claimed| coverage gap before flagging (0.02)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    polys, ids, rings = load_buildings(args.geojson)
    tau, k, antennas, claimed = parse_solution(args.solution)
    print(f"solution: tau={tau} k={k}  antennas={len(antennas)}  claimed={len(claimed)}")
    if len(antennas) != k:
        print(f"  WARNING: expected {k} antennas, found {len(antennas)}")

    tree = PolyIndex(polys)

    # --- constraint: every antenna must lie on a building boundary -------------------
    off_boundary, worst = 0, 0.0
    for a in antennas:
        pt = Point(a)
        cands = tree.query(pt.buffer(1e-6))
        d = min((polys[i].exterior.distance(pt) for i in cands), default=float("inf"))
        if d > 1e-9:
            off_boundary += 1
        if d != float("inf"):
            worst = max(worst, d)
    print(f"  antennas off the boundary: {off_boundary}  (worst distance {worst:.3e} m)")

    # The constraint above is checked on the coordinates as written; visibility is then
    # traced from a point pushed a hair outward, for the reason in docstring point 3.
    antennas = nudge_antennas(antennas, polys, rings, tree)

    order = list(range(len(polys)))
    if args.sample_buildings and args.sample_buildings < len(order):
        random.Random(args.seed).shuffle(order)
        order = order[: args.sample_buildings]
        print(f"  checking a random sample of {len(order)} buildings")
    if args.antenna_radius > 0:
        print(f"  WARNING: antennas beyond {args.antenna_radius} m ignored; "
              f"this understates coverage and can fabricate over-claims")

    claimed_set = set(claimed)
    agree = over = under = 0
    flagged = []
    # How far away the furthest antenna that actually supplied coverage was.  If this
    # approaches any radius cap in use, the cap is distorting the answer.
    furthest_useful = 0.0

    for n_done, bi in enumerate(order, 1):
        samples = perimeter_samples(rings[bi], args.step)
        total = sum(s[2] for s in samples)
        # Nearest-first, so the common case terminates after one or two visibility tests.
        cx = sum(p[0] for p in rings[bi]) / len(rings[bi])
        cy = sum(p[1] for p in rings[bi]) / len(rings[bi])
        near = sorted(range(len(antennas)),
                      key=lambda i: (antennas[i][0] - cx) ** 2 + (antennas[i][1] - cy) ** 2)
        if args.antenna_radius > 0:
            r2 = args.antenna_radius ** 2
            near = [i for i in near
                    if (antennas[i][0] - cx) ** 2 + (antennas[i][1] - cy) ** 2 <= r2]

        was_claimed = ids[bi] in claimed_set

        # Considering every antenna is what makes this correct; exact early exits are what
        # make it affordable.  Once the covered length has passed the threshold the verdict
        # can no longer change, so the remaining samples are not worth testing.  The
        # mirror-image exit -- stopping once even all the remaining length could not reach
        # the threshold -- is only taken for buildings that were *not* claimed, because it
        # leaves `ratio` a lower bound, and for a claimed building the size of the gap is
        # the whole point of the report.  So a claimed building that is genuinely far below
        # tau gets the full scan, which is exactly where the time should go.
        need = tau * total
        seen = 0.0
        left = total
        # Antennas that worked for a previous sample are tried first for the next one:
        # consecutive samples lie on the same wall and are usually seen by the same
        # antenna, so this collapses the inner loop to a single test in the common case.
        hot = []
        for px, py, w in samples:
            if seen >= need:
                break
            if not was_claimed and seen + left < need:
                break
            left -= w
            for ai in itertools.chain(hot, near):
                if visible(antennas[ai], (px, py), tree, polys):
                    seen += w
                    # hypot rather than math.dist: the venv this runs in is Python 3.6.8,
                    # where math.dist does not exist yet.
                    furthest_useful = max(
                        furthest_useful,
                        math.hypot(antennas[ai][0] - px, antennas[ai][1] - py))
                    if ai in hot:
                        hot.remove(ai)
                    hot.insert(0, ai)
                    del hot[4:]
                    break
        ratio = seen / total if total > 0 else 0.0

        is_serviced = ratio >= tau - 1e-9
        if is_serviced == was_claimed:
            agree += 1
        elif was_claimed:
            over += 1
            flagged.append((ids[bi], ratio))
        else:
            under += 1
        if n_done % 100 == 0:
            print(f"    {n_done}/{len(order)} buildings checked", flush=True)

    n = agree + over + under
    print(f"\n  agreement: {agree}/{n}")
    print(f"  claimed but sampling says not serviced: {over}")
    print(f"  serviced but not claimed:               {under}")
    print(f"  furthest antenna that supplied coverage: {furthest_useful:.0f} m")

    # Sampling at a finite step cannot resolve coverage nearer than roughly one step to
    # the threshold, so borderline disagreements are expected.  A real defect shows up as
    # claims that miss by a wide margin.
    bad = [(i, r) for i, r in flagged if r < tau - args.tolerance]
    if flagged:
        print("  over-claims (id, sampled ratio), worst first:")
        for i, r in sorted(flagged, key=lambda t: t[1])[:10]:
            print(f"    {i}: {r:.4f}  (needs {tau})")
    print(f"  of those, missing by more than {args.tolerance}: {len(bad)}")

    ok = off_boundary == 0 and not bad
    print("\nVERDICT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
