#!/usr/bin/env python3
import argparse
import os
import shutil
import sys

def parse_header(path):
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

    best = {}
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
