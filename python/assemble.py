#!/usr/bin/env python3
"""
assemble.py -- collect the nine per-sub-problem answers into one submission file.

The rules ask for three lines per sub-problem: the (tau, k) pair, the antenna
coordinates, and the ids of the buildings claimed as serviced.  Each solve run already
emits exactly that, so this concatenates them in a fixed order and re-checks the
invariants that would invalidate a submission -- right number of antennas, coordinates
that survive a text round trip, no duplicate or unsorted building ids.

The organisers publish the exact required format with the evaluation dataset.  Only
`format_block` below should need adjusting if it differs from the reading here.

Usage:
    assemble.py --dir results --out results/submission.txt
    assemble.py --dir results --out results/submission.txt --cells data/cells.txt

`--cells` is the list competition/parse_params.py derived from the organisers' parameters
file.  Without it the sample dataset's nine combinations are assumed, which is what this
script did unconditionally until the competition parameters became a file we have to read
rather than a constant we can hard-code.

The tau written into each header is the text from that cell list, verbatim.  Reformatting
it would be harmless arithmetically -- `0.3` and `0.29999999999999999` parse to the same
double -- but the submission should read the way the parameters file reads.
"""

import argparse
import os
import sys

DEFAULT_TAUS = ["0.25", "0.5", "0.75"]
DEFAULT_KS = ["50", "500", "1000"]


def load_cells(path):
    if path is None:
        return [(t, k) for t in DEFAULT_TAUS for k in DEFAULT_KS]
    cells = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                raise SystemExit("%s: expected '<tau> <k>', got %r" % (path, line))
            cells.append((parts[0], parts[1]))
    if not cells:
        raise SystemExit("%s: no cells" % path)
    return cells


def parse(path):
    with open(path) as fh:
        lines = [ln.rstrip("\n") for ln in fh if ln.strip()]
    if len(lines) < 2:
        raise SystemExit(f"{path}: expected at least 2 lines, got {len(lines)}")
    header = lines[0].strip("() \t")
    tau_s, k_s = header.split(",")
    coords = []
    for chunk in lines[1].split(")"):
        chunk = chunk.strip().lstrip(",").strip().lstrip("(")
        if not chunk:
            continue
        xs, ys = chunk.split(",")
        coords.append((float(xs), float(ys)))
    ids = []
    if len(lines) >= 3 and lines[2].strip():
        # Kept as text: the evaluator canonicalises an id as String(id).trim() and the
        # competition dataset is free to use ids that are not integers at all.
        ids = [v.strip() for v in lines[2].split(",") if v.strip()]
    return float(tau_s), int(k_s), coords, ids


def id_sort_key(value):
    """Numeric order when the ids are numbers, lexicographic when they are not."""
    try:
        return (0, float(value), "")
    except ValueError:
        return (1, 0.0, value)


def format_block(tau_text, k, coords, ids):
    pair = f"({tau_text}, {k})"
    pts = ",".join(f"({x:.17g},{y:.17g})" for x, y in coords)
    served = ",".join(ids)
    return f"{pair}\n{pts}\n{served}\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="results")
    ap.add_argument("--out", default="results/submission.txt")
    ap.add_argument("--cells", help="cell list from competition/parse_params.py")
    args = ap.parse_args()

    cells = load_cells(args.cells)

    blocks, problems = [], 0
    print(f"{'tau':>8} {'k':>6} {'antennas':>9} {'serviced':>9}  file")
    for tau_text, k_text in cells:
        tau, k = float(tau_text), int(k_text)
        path = os.path.join(args.dir, f"tau{tau_text}_k{k_text}.txt")
        if not os.path.exists(path):
            print(f"  MISSING: {path}")
            continue
        t, kk, coords, ids = parse(path)
        problems += 1

        if abs(t - tau) > 1e-12 or kk != k:
            print(f"  WARNING {path}: header says ({t}, {kk}), expected ({tau}, {k})")
        if len(coords) != k:
            print(f"  WARNING {path}: {len(coords)} antennas, expected {k}")
        if len(set(ids)) != len(ids):
            print(f"  WARNING {path}: duplicate building ids")
        # A 17-significant-digit round trip must be exact, or the evaluator will score
        # points fractionally different from the ones we searched over.
        for x, y in coords:
            if float(f"{x:.17g}") != x or float(f"{y:.17g}") != y:
                print(f"  WARNING {path}: coordinate does not round-trip: {x},{y}")
                break

        print(f"{tau_text:>8} {k_text:>6} {len(coords):>9} {len(ids):>9}  {path}")
        # The header carries the parameters file's own text, not a reprint of the float
        # the per-cell file happened to write.
        blocks.append(format_block(tau_text, k, coords, sorted(ids, key=id_sort_key)))

    with open(args.out, "w") as fh:
        fh.write("".join(blocks))
    print(f"\nwrote {args.out}  ({problems}/{len(cells)} sub-problems)")
    return 0 if problems == len(cells) else 1


if __name__ == "__main__":
    sys.exit(main())
