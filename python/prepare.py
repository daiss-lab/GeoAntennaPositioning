#!/usr/bin/env python3
"""
prepare.py -- convert a GIS Cup building-footprint GeoJSON into the compact binary
format consumed by the C++ solver.

Usage:
    python3 prepare.py <input.geojson> <output.bin> [--report report.txt]

Binary layout (little endian, IEEE-754 doubles):

    char[8]  magic  = "GISCUP01"
    u32      n_buildings
    u32      n_vertices          (total, closing duplicate removed)
    u32      flags               (bit 0: any building had interior rings)
    u32      reserved            (0)
    i64      building_id[n_buildings]
    u32      vert_offset[n_buildings + 1]
    f64      extra_perimeter[n_buildings]     length of discarded interior rings
    f64      xy[2 * n_vertices]               interleaved x,y -- CCW, not closed

Normalisation performed here:
  * only the first (exterior) ring of each polygon is kept
  * rings are rewound to counter-clockwise so the interior is always on the left
  * the repeated closing vertex is dropped
  * consecutive duplicate vertices are collapsed

Interior rings are *not* geometry the solver blocks against -- the contest states
footprints are simple polygons without holes -- but their length is recorded so the
solver can optionally match an evaluator that uses Shapely's `Polygon.length`
(which sums exterior *and* interior rings).

Building ids
------------
The binary carries an i64 id per building because the solver wants a cheap integer, but
the organisers' loader canonicalises an id as `String(properties.id).trim()` and accepts
*any* string or number (`dataset-loader.ts:303`).  The evaluation dataset is therefore not
obliged to use the small integers the sample happens to use.

So the id written into the binary is an internal handle, and the id the submission must
quote is written alongside it into a sidecar `<output>.ids` file, one
`<internal>\t<canonical>` pair per line in binary order.  `relabel.py` maps a solution
file's claim line back through it.  When the dataset does use integer ids the internal
handle is that integer and the mapping is the identity, so the emitted text is unchanged.
"""

import argparse
import json
import math
import struct
import sys

MAGIC = b"GISCUP01"


def canonical_id(value, feature_index):
    """Return the id exactly as the organisers' loader would canonicalise it.

    `canonicalizeBuildingId` (dataset-loader.ts:303) accepts a string or a number and
    returns `String(value).trim()`.  The difference that matters is numeric formatting:
    JavaScript prints an integral double with no fractional part, so a JSON `12.0` is
    "12" there and must not become "12.0" here, or every claim we make would name a
    building the evaluator has never heard of.
    """
    if value is None or isinstance(value, bool):
        raise ValueError(
            "feature %d: properties.id must be a string or a number" % feature_index)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("feature %d: properties.id must be finite" % feature_index)
        if value == int(value) and abs(value) < 1e21:
            return str(int(value))
        return repr(value)
    if isinstance(value, int):
        return str(value)
    text = str(value).strip()
    if not text:
        raise ValueError("feature %d: properties.id must not be empty" % feature_index)
    return text


def internal_handles(canon_ids):
    """Pick the i64 handles the binary carries, and say whether they are the real ids.

    Integer ids are used verbatim so that on a dataset like the sample -- where ids are
    small integers -- every file this pipeline writes is byte-identical to what it wrote
    before the sidecar existed.  Anything else falls back to positions, for the whole
    dataset at once rather than per feature, so the two schemes can never be mixed and
    collide.
    """
    handles = []
    for text in canon_ids:
        try:
            value = int(text)
        except ValueError:
            return list(range(len(canon_ids))), False
        if not (-(2 ** 63) <= value < 2 ** 63):
            return list(range(len(canon_ids))), False
        handles.append(value)
    if len(set(handles)) != len(handles):
        return list(range(len(canon_ids))), False
    return handles, True


def signed_area(ring):
    """Twice the signed area; positive means counter-clockwise."""
    total = 0.0
    for i in range(len(ring) - 1):
        x1, y1 = ring[i]
        x2, y2 = ring[i + 1]
        total += x1 * y2 - x2 * y1
    return total


def ring_length(ring):
    return sum(
        math.hypot(ring[i + 1][0] - ring[i][0], ring[i + 1][1] - ring[i][1])
        for i in range(len(ring) - 1)
    )


def clean_ring(ring):
    """Drop the closing duplicate and any consecutive repeats. Returns open ring."""
    if len(ring) >= 2 and ring[0] == ring[-1]:
        ring = ring[:-1]
    out = []
    for p in ring:
        if not out or (p[0] != out[-1][0] or p[1] != out[-1][1]):
            out.append((float(p[0]), float(p[1])))
    # the first and last may still coincide after collapsing
    while len(out) >= 2 and out[0] == out[-1]:
        out.pop()
    return out


def iter_polygons(geom):
    """Yield each polygon's ring list, accepting Polygon and MultiPolygon."""
    t = geom["type"]
    if t == "Polygon":
        yield geom["coordinates"]
    elif t == "MultiPolygon":
        for poly in geom["coordinates"]:
            yield poly
    else:
        raise ValueError("unsupported geometry type: %s" % t)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--report", help="write a human-readable summary here")
    ap.add_argument("--id-field", default="id",
                    help="feature property holding the building id (default: id)")
    args = ap.parse_args()

    with open(args.input) as fh:
        data = json.load(fh)

    features = data["features"]
    canon_ids = []
    offsets = [0]
    coords = []
    extra_perims = []

    notes = []
    n_multiring = 0
    n_multipolygon = 0
    n_rewound = 0
    n_collapsed = 0

    for feat in features:
        geom = feat["geometry"]
        props = feat.get("properties") or {}
        if args.id_field in props:
            bid = canonical_id(props[args.id_field], len(canon_ids))
        else:
            # The evaluator refuses a feature with no properties.id, so this is a dataset
            # we could not have scored anyway; number it positionally and carry on rather
            # than losing the whole run to one malformed feature.
            bid = str(len(canon_ids) + 1)

        polys = list(iter_polygons(geom))
        if len(polys) > 1:
            n_multipolygon += 1
        # A MultiPolygon feature keeps only its largest part: the contest guarantees
        # one simple polygon per building, so extra parts are data artefacts.
        best = max(polys, key=lambda rings: abs(signed_area(rings[0])))

        rings = best
        if len(rings) > 1:
            n_multiring += 1
            notes.append(
                "building id=%s had %d rings; kept exterior only "
                "(discarded length %.6f m)"
                % (bid, len(rings), sum(ring_length(r) for r in rings[1:]))
            )
        extra = sum(ring_length(r) for r in rings[1:])

        raw = rings[0]
        before = len(raw)
        ring = clean_ring(raw)
        if len(ring) != before - 1:
            n_collapsed += 1
        if len(ring) < 3:
            notes.append("building id=%s degenerate (<3 distinct vertices); skipped" % bid)
            continue

        closed = ring + [ring[0]]
        if signed_area(closed) < 0.0:
            ring.reverse()
            n_rewound += 1

        canon_ids.append(bid)
        for x, y in ring:
            coords.append(x)
            coords.append(y)
        offsets.append(len(coords) // 2)
        extra_perims.append(extra)

    ids, ids_are_real = internal_handles(canon_ids)
    if len(set(canon_ids)) != len(canon_ids):
        # The evaluator throws DUPLICATE_ID and scores nothing, so this is fatal there.
        notes.append("!! %d duplicate building ids after canonicalisation"
                     % (len(canon_ids) - len(set(canon_ids))))

    sidecar = args.output + ".ids"
    with open(sidecar, "w") as fh:
        for handle, canon in zip(ids, canon_ids):
            fh.write("%d\t%s\n" % (handle, canon))

    n_b = len(ids)
    n_v = len(coords) // 2
    flags = 1 if n_multiring else 0

    with open(args.output, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<IIII", n_b, n_v, flags, 0))
        fh.write(struct.pack("<%dq" % n_b, *ids))
        fh.write(struct.pack("<%dI" % (n_b + 1), *offsets))
        fh.write(struct.pack("<%dd" % n_b, *extra_perims))
        fh.write(struct.pack("<%dd" % (2 * n_v), *coords))

    perim_total = 0.0
    for b in range(n_b):
        lo, hi = offsets[b], offsets[b + 1]
        n = hi - lo
        for i in range(n):
            j = (i + 1) % n
            x1, y1 = coords[2 * (lo + i)], coords[2 * (lo + i) + 1]
            x2, y2 = coords[2 * (lo + j)], coords[2 * (lo + j) + 1]
            perim_total += math.hypot(x2 - x1, y2 - y1)

    lines = [
        "input:              %s" % args.input,
        "output:             %s" % args.output,
        "buildings:          %d" % n_b,
        "vertices / edges:   %d" % n_v,
        "total perimeter:    %.3f m" % perim_total,
        "rewound to CCW:     %d" % n_rewound,
        "rings collapsed:    %d" % n_collapsed,
        "multi-ring features:%d" % n_multiring,
        "multi-polygon feats:%d" % n_multipolygon,
        "id sidecar:         %s" % sidecar,
        "id scheme:          %s" % ("dataset integers used verbatim" if ids_are_real
                                    else "positional handles; relabel.py is REQUIRED"),
        "first / last id:    %s / %s" % (canon_ids[0] if canon_ids else "-",
                                         canon_ids[-1] if canon_ids else "-"),
    ]
    if notes:
        lines.append("")
        lines.append("notes:")
        lines.extend("  " + n for n in notes)
    report = "\n".join(lines)
    print(report)
    if args.report:
        with open(args.report, "w") as fh:
            fh.write(report + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
