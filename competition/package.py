#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import sys
import zipfile
from datetime import datetime, timezone

SOURCE_DIRS = ["src", "python", "slurm", "competition", "tests"]
SOURCE_FILES = ["Makefile", "DOCUMENTATION.md"]
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".py", ".sh", ".sbatch", ".md", ".ts", ".txt")

def fail(message):
    print("FATAL: %s" % message)
    return False

def load_cells(path):
    if path is None:
        return None
    cells = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                parts = line.split()
                cells.append((parts[0], parts[1]))
    return cells

def check_solution(path, cells):
    ok = True
    with open(path) as fh:
        raw = fh.read()

    if raw.startswith("﻿"):
        ok = fail("%s starts with a byte-order mark" % path)

    lines = raw.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    if not lines:
        return fail("%s is empty" % path), 0
    if len(lines) % 3 != 0:
        return fail("%s has %d lines, not a multiple of 3" % (path, len(lines))), 0

    blocks = len(lines) // 3
    if cells is not None and blocks != len(cells):
        ok = fail("%s has %d blocks, the parameters file asks for %d"
                  % (path, blocks, len(cells)))

    print("%-10s %-7s %-10s %-9s %s" % ("tau", "k", "antennas", "claims", "check"))
    for index in range(blocks):
        header, coords_line, claims_line = lines[3 * index:3 * index + 3]

        text = header.strip()
        if not (text.startswith("(") and text.endswith(")") and text.count(",") == 1):
            ok = fail("block %d: header %r is not '(tau, k)'" % (index + 1, header))
            continue
        tau_text, k_text = [t.strip() for t in text[1:-1].split(",")]
        try:
            tau, k = float(tau_text), int(k_text)
        except ValueError:
            ok = fail("block %d: cannot read tau or k from %r" % (index + 1, header))
            continue
        if not (0.0 < tau <= 1.0):
            ok = fail("block %d: tau=%s is outside (0, 1]" % (index + 1, tau_text))

        entries = [e for e in coords_line.split(")") if e.strip().strip(",")]
        n_bad = 0
        n_round = 0
        for entry in entries:
            body = entry.strip().lstrip(",").strip().lstrip("(")
            parts = body.split(",")
            if len(parts) != 2:
                n_bad += 1
                continue
            try:
                x, y = float(parts[0]), float(parts[1])
            except ValueError:
                n_bad += 1
                continue
            if repr(float(parts[0])) != repr(x) or repr(float(parts[1])) != repr(y):
                n_round += 1
        if n_bad:
            ok = fail("block %d: %d malformed coordinate entries" % (index + 1, n_bad))
        if n_round:
            ok = fail("block %d: %d coordinates do not round-trip" % (index + 1, n_round))
        if len(entries) != k:
            ok = fail("block %d: %d antennas for k=%d" % (index + 1, len(entries), k))

        claims = [c.strip() for c in claims_line.split(",") if c.strip()]
        if not claims:
            ok = fail("block %d: claims nothing, so it scores zero" % (index + 1))
        if len(set(claims)) != len(claims):
            ok = fail("block %d: duplicate claimed ids" % (index + 1))

        if cells is not None and index < len(cells):
            want_tau, want_k = cells[index]
            if abs(float(want_tau) - tau) > 1e-12 or int(want_k) != k:
                ok = fail("block %d: parameters ask for (%s, %s) here"
                          % (index + 1, want_tau, want_k))

        print("%-10s %-7d %-10d %-9d %s"
              % (tau_text, k, len(entries), len(claims), "ok" if ok else "see above"))

    return ok, blocks

def copy_source(root, dest):
    os.makedirs(dest, exist_ok=True)
    copied = 0
    for name in SOURCE_FILES:
        src = os.path.join(root, name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(dest, name))
            copied += 1
    for name in SOURCE_DIRS:
        src = os.path.join(root, name)
        if not os.path.isdir(src):
            continue
        for base, _dirs, files in os.walk(src):
            rel = os.path.relpath(base, root)
            for filename in files:
                if not filename.endswith(SOURCE_SUFFIXES):
                    continue
                target_dir = os.path.join(dest, rel)
                os.makedirs(target_dir, exist_ok=True)
                shutil.copy2(os.path.join(base, filename),
                             os.path.join(target_dir, filename))
                copied += 1
    return copied

README = """# ACM SIGSPATIAL GIS Cup 2026 -- antenna placement

This archive contains our answers to the nine sub-problems and the source code that
produced them.

    solution.txt      the answers: three lines per sub-problem, in the order the
                      competition parameters file lists them --
                        1. the (tau, k) pair
                        2. the k antenna coordinates, as (x,y) separated by commas
                        3. the ids of the buildings claimed as serviced
    source/           everything needed to build and re-run the solver
    MANIFEST.txt      what is in this archive, and what produced it

Coordinates are written with 17 significant digits, so every one of them round-trips
through IEEE 754 binary64 exactly as required.

## What the program does

The solver places antennas on building boundaries to maximise the number of buildings
whose visible perimeter fraction reaches `tau`.  In outline:

1. **Candidates.** Every polygon vertex, plus points every 4 m along each edge.
2. **Visibility.** An angular sweep per candidate gives the intervals of every building's
   perimeter it can see; results are cached once and shared by all sub-problems.
3. **Construction.** Randomised lazy greedy on a clipped surrogate objective that credits
   partial coverage, with bundled two-antenna moves so that a building no single antenna
   can finish is still reachable.
4. **Improvement.** Large-neighbourhood search: tear out a few antennas, rebuild greedily,
   keep the result if it is not worse.  Several replicas run in parallel and periodically
   adopt the best solution found.
5. **Polish.** Coordinate ascent over the continuum of legal positions, moving each antenna
   to the best tangency event on its edge or to a nearby vertex, scored exactly.

Everything reported is produced by re-reading the emitted answer file and scoring it from
scratch, never by trusting the search's own bookkeeping.

`source/DOCUMENTATION.md` describes the method, the measurements behind each default, and
the things that were tried and rejected.

## Building

Requires a C++17 compiler with OpenMP.  No third-party C++ dependencies.

    cd source
    make                # optimised, OpenMP, -march=native  -> bin/gis_cup
    make portable       # same without -march=native, if the run host differs
    make test           # geometry self-tests

The Python helpers need Python 3 with NumPy and Shapely; they are used only to convert the
input, to check answers independently, and to assemble the submission.  The solver itself
needs neither.

## Running

    # 1. GeoJSON -> the solver's binary format
    python3 python/prepare.py DATASET.geojson data/eval.bin

    # 2. visibility precompute, once, shared by every sub-problem
    ./bin/gis_cup precompute --data data/eval.bin --out data/eval_arcs.bin \\
        --slot-len 0.05 --spacing 4.0 --max-range 2400

    # 3. solve one sub-problem (repeat per (tau, k) pair)
    ./bin/gis_cup solve --data data/eval.bin --cache data/eval_arcs.bin --slot-len 0.05 \\
        --tau TAU --k K --time SECONDS --replicas 32 --out results/tauTAU_kK.txt

    # 4. polish it
    ./bin/gis_cup refine --data data/eval.bin --solution results/tauTAU_kK.txt \\
        --out results/polished.txt

    # 5. re-score an emitted answer the way a third party would
    ./bin/gis_cup score --data data/eval.bin --solution results/polished.txt

`--slot-len` must match between `precompute` and `solve`; `solve` refuses to run against a
cache built for a different discretisation rather than producing silent nonsense.

### The whole submission, one command

    competition/GO.sh DATASET.geojson competition-parameters.txt HOURS

This is what produced this archive.  It reads the parameters file, submits every stage to
SLURM against absolute deadlines derived from `HOURS`, and writes the finished archive.
It needs a SLURM cluster; the stage-by-stage commands above do not.

## Hardware this was run on

A shared HPC cluster; each solve is one node, 32 cores.  The solver is deterministic given
`--seed`, `--replicas` and a fixed time budget, but because the search is deadline-driven
a re-run on different hardware explores a different amount and can land on a different
answer of similar quality.
"""

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--solution", required=True, help="the assembled submission file")
    ap.add_argument("--out", default="Final_Submission", help="output directory name")
    ap.add_argument("--cells", help="cell list from parse_params.py, to cross-check order")
    ap.add_argument("--dataset", help="dataset the answers were solved on, for the manifest")
    ap.add_argument("--root", default=".", help="repository root to take source from")
    ap.add_argument("--require-valid", metavar="REPORT.JSON",
                    help="refuse to package unless this official-validate report says ok")
    ap.add_argument("--format", choices=["zip", "tgz"], default="zip")
    ap.add_argument("--force", action="store_true", help="overwrite an existing output")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.exists(args.solution):
        raise SystemExit("no solution file at %s" % args.solution)

    cells = load_cells(args.cells)
    print("== checking %s ==" % args.solution)
    ok, blocks = check_solution(args.solution, cells)

    if args.require_valid:
        if not os.path.exists(args.require_valid):
            ok = fail("no validator report at %s -- run official-validate.ts first"
                      % args.require_valid)
        else:
            with open(args.require_valid) as fh:
                report = json.load(fh)
            if not report.get("ok"):
                ok = fail("the organisers' parser rejected this submission: %s"
                          % "; ".join(report.get("fatal", []))[:400])
            else:
                print("validator report %s: ok" % args.require_valid)

    if not ok:
        print("\nNOT PACKAGED.  Fix the problems above and run again.")
        return 1

    out_dir = os.path.abspath(args.out)
    if os.path.exists(out_dir):
        if not args.force:
            raise SystemExit("%s already exists; pass --force to replace it" % out_dir)
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    shutil.copy2(args.solution, os.path.join(out_dir, "solution.txt"))
    n_source = copy_source(root, os.path.join(out_dir, "source"))
    with open(os.path.join(out_dir, "README.md"), "w") as fh:
        fh.write(README)

    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    manifest = [
        "GIS Cup 2026 submission",
        "built:           %s" % stamp,
        "solution:        solution.txt (%d sub-problems)" % blocks,
        "dataset:         %s" % (os.path.basename(args.dataset) if args.dataset else "-"),
        "parameters:      %s" % (args.cells or "-"),
        "source files:    %d under source/" % n_source,
        "",
        "sub-problems, in order:",
    ]
    if cells:
        for index, (tau, k) in enumerate(cells, 1):
            manifest.append("  %d. tau=%s k=%s" % (index, tau, k))
    with open(os.path.join(out_dir, "MANIFEST.txt"), "w") as fh:
        fh.write("\n".join(manifest) + "\n")

    base = os.path.basename(out_dir)
    if args.format == "zip":
        archive = out_dir + ".zip"
        if os.path.exists(archive):
            os.remove(archive)
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
            for dirpath, _dirs, files in os.walk(out_dir):
                for filename in sorted(files):
                    full = os.path.join(dirpath, filename)
                    zf.write(full, os.path.join(base, os.path.relpath(full, out_dir)))
    else:
        archive = shutil.make_archive(out_dir, "gztar",
                                      os.path.dirname(out_dir), base)

    size_mb = os.path.getsize(archive) / (1024.0 * 1024.0)
    print("\n== packaged ==")
    print("  directory: %s" % out_dir)
    print("  archive:   %s  (%.1f MB)" % (archive, size_mb))
    print("  contents:  solution.txt, README.md, MANIFEST.txt, source/ (%d files)" % n_source)
    print("\nUpload %s to https://easychair.org/conferences/?conf=giscup2026" % archive)
    return 0

if __name__ == "__main__":
    sys.exit(main())
