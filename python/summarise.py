#!/usr/bin/env python3
import argparse
import collections
import glob
import os
import re
import subprocess
import sys

def score(path):
    try:
        lines = open(path).read().strip().split("\n")
    except OSError:
        return None
    if len(lines) < 3:
        return None
    body = lines[2].strip()
    return len([x for x in body.split(",") if x.strip()]) if body else 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("--pattern", default=r"(?P<arm>.+?)_",
                    help="regex with an 'arm' group, and optionally a 'seed' group")
    ap.add_argument("--job", help="SLURM array job id; without it, this refuses to "
                                  "report, because `solve` rewrites its answer file at "
                                  "every synchronisation point and a file belonging to a "
                                  "running task is a checkpoint, not a result")
    ap.add_argument("--allow-running", action="store_true",
                    help="report anyway, clearly labelled; for watching progress only")
    args = ap.parse_args()

    if args.job:
        out = subprocess.run(["sacct", "-j", args.job, "--format=JobID%20,State", "-n"],
                             stdout=subprocess.PIPE, universal_newlines=True).stdout
        states = collections.Counter(p[1] for p in (l.split() for l in out.splitlines())
                                     if len(p) >= 2 and "." not in p[0])
        unfinished = sum(n for st, n in states.items() if st != "COMPLETED")
        print(f"job {args.job}: {dict(states)}")
        if unfinished and not args.allow_running:
            sys.exit(f"{unfinished} task(s) not COMPLETED; pass --allow-running to see "
                     f"partial numbers, but do not draw conclusions from them")
    elif not args.allow_running:
        sys.exit("pass --job <arrayid> so completion can be checked, or --allow-running "
                 "to report unconditionally")

    cell_re = re.compile(r"tau(?P<tau>[\d.]+)_k(?P<k>\d+)")
    arm_re = re.compile(args.pattern)

    table = collections.defaultdict(dict)
    for path in sorted(glob.glob(os.path.join(args.directory, "*.txt"))):
        base = os.path.basename(path)
        c, a = cell_re.search(base), arm_re.match(base)
        if not c or not a:
            continue
        cell = (float(c.group("tau")), int(c.group("k")))
        gd = a.groupdict()
        table[(gd["arm"], gd.get("seed", "-"))][cell] = score(path)

    if not table:
        sys.exit(f"no scored answer files matched in {args.directory}")

    cells = sorted({c for row in table.values() for c in row})
    head = "arm/seed".ljust(14) + "".join(f"{f'{t}/{k}':>12s}" for t, k in cells)
    print(head + "        total")
    print("-" * (len(head) + 13))
    for key in sorted(table):
        row = table[key]
        body = "".join(f"{row.get(c, 0):12d}" for c in cells)
        complete = len(row) == len(cells)
        total = f"{sum(row.values()):13d}" if complete else "     partial"
        print(f"{key[0] + '/' + key[1]:14s}{body}{total}")

    arms = sorted({k[0] for k in table})
    if len(arms) > 1:
        base = arms[0]
        print(f"\npaired delta vs arm {base!r}, per (cell, seed):")
        for arm in arms[1:]:
            deltas = []
            for (a, s), row in table.items():
                if a != arm:
                    continue
                ref = table.get((base, s))
                if not ref:
                    continue
                deltas += [row[c] - ref[c] for c in row if c in ref]
            if deltas:
                wins = sum(1 for d in deltas if d > 0)
                loss = sum(1 for d in deltas if d < 0)
                print(f"  {arm:>6s}: sum {sum(deltas):+5d}  mean {sum(deltas)/len(deltas):+6.1f}  "
                      f"{wins}W/{loss}L/{len(deltas)-wins-loss}T over {len(deltas)} pairs")

if __name__ == "__main__":
    main()
