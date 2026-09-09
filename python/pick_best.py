#!/usr/bin/env python3
"""pick_best.py -- choose the winning answer per sub-problem out of every run made.

Solution04 produces many answers per (tau, k): several islands, several configurations,
several seeds.  Only one of each can be submitted, and the choice must be made on the
score rather than on which run was expected to be best -- an arm that loses on average can
still hold the best single answer for one cell, and there is no reason to leave it behind.

Selection is on the claim line, which is the list of buildings the exact evaluator
confirmed at the moment the file was written.  It is a verified quantity, not the solver's
opinion: `writeSolution` only ever emits what `evaluateExact` returned.  The winners are
re-scored independently afterwards by slurm/finalize.sbatch, so a corrupt or stale file
cannot reach a submission on the strength of a long claim line alone.

Usage:
    python3 python/pick_best.py results/fact results/scale results/island_sh \\
        --out results/final [--copy]
"""
import argparse
import os
import shutil
import sys


def parse_header(path):
    """(tau, k, n_antennas, n_claimed) or None if the file is not a usable answer."""
    try:
        with open(path) as fh:
            lines = [ln.rstrip('\n') for ln in fh if ln.strip()]
    except OSError:
        return None
    if len(lines) < 2:
        return None
    try:
        tau_s, k_s = lines[0].strip('() \t').split(',')
        tau, k = float(tau_s), int(k_s)
    except ValueError:
        return None
    n_ant = lines[1].count('(')
    claimed = 0
    if len(lines) >= 3 and lines[2].strip():
        claimed = len(lines[2].split(','))
    return tau, k, n_ant, claimed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='+')
    ap.add_argument('--out', default='results/final')
    ap.add_argument('--copy', action='store_true', help='write the winners into --out')
    ap.add_argument('--top', type=int, default=3, help='runners-up to list per cell')
    args = ap.parse_args()

    best = {}      # (tau, k) -> list of (claimed, n_ant, path)
    scanned = skipped = 0
    for d in args.dirs:
        for root, _dirs, files in os.walk(d):
            for name in files:
                if not name.endswith('.txt'):
                    continue
                path = os.path.join(root, name)
                h = parse_header(path)
                scanned += 1
                if h is None:
                    skipped += 1
                    continue
                tau, k, n_ant, claimed = h
                # An answer with the wrong number of antennas is not a valid submission for
                # this sub-problem whatever it claims, so it never competes.
                if n_ant != k:
                    skipped += 1
                    continue
                best.setdefault((tau, k), []).append((claimed, n_ant, path))

    print('scanned %d files, %d unusable' % (scanned, skipped))
    if not best:
        print('nothing to pick from', file=sys.stderr)
        return 1

    if args.copy:
        os.makedirs(args.out, exist_ok=True)

    total = 0
    print('\n%-6s %-6s %9s  %s' % ('tau', 'k', 'claimed', 'winner'))
    for key in sorted(best):
        tau, k = key
        cands = sorted(best[key], reverse=True)
        claimed, _n, path = cands[0]
        total += claimed
        print('%-6g %-6d %9d  %s' % (tau, k, claimed, path))
        for c, _n2, p in cands[1:1 + args.top]:
            print('%-6s %-6s %9d  %s' % ('', '', c, p))
        if args.copy:
            shutil.copyfile(path, os.path.join(args.out, 'tau%g_k%d.txt' % (tau, k)))

    print('\n%d sub-problems, total claimed %d' % (len(best), total))
    if len(best) != 9:
        print('WARNING: expected 9 sub-problems, found %d' % len(best), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
