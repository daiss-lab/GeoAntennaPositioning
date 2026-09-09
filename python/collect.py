#!/usr/bin/env python3
import argparse
import collections
import os
import re
import sys

def score_of(path):
    try:
        with open(path) as fh:
            lines = fh.read().splitlines()
    except OSError:
        return None
    if len(lines) < 3:
        return None
    claim = lines[2].strip()
    if not claim:
        return 0
    return len(claim.split(','))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='+')
    ap.add_argument('--pattern', required=True,
                    help='regex with named groups matched against each filename stem')
    ap.add_argument('--rows', default='', help='comma-separated group names keying rows')
    ap.add_argument('--cols', default='tau,k', help='comma-separated group names keying columns')
    ap.add_argument('--pair', default='',
                    help='group name to average over (e.g. seed); also reports per-value deltas')
    args = ap.parse_args()

    rx = re.compile(args.pattern)
    rows = [c for c in args.rows.split(',') if c]
    cols = [c for c in args.cols.split(',') if c]

    data = {}
    pair_vals, row_vals, col_vals = set(), set(), set()
    for d in args.dirs:
        for name in sorted(os.listdir(d)):
            if not name.endswith('.txt'):
                continue
            m = rx.search(name[:-4])
            if not m:
                continue
            s = score_of(os.path.join(d, name))
            if s is None:
                print('  (unreadable, skipped) %s/%s' % (d, name), file=sys.stderr)
                continue
            g = m.groupdict()
            rk = tuple(g.get(c, '') for c in rows)
            ck = tuple(g.get(c, '') for c in cols)
            pk = g.get(args.pair, '') if args.pair else ''
            data[(rk, ck, pk)] = s
            row_vals.add(rk)
            col_vals.add(ck)
            pair_vals.add(pk)

    if not data:
        print('no results matched', file=sys.stderr)
        return 1

    def numkey(t):
        out = []
        for v in t:
            try:
                out.append((0, float(v)))
            except ValueError:
                out.append((1, v))
        return out

    row_list = sorted(row_vals, key=numkey)
    col_list = sorted(col_vals, key=numkey)
    pair_list = sorted(pair_vals, key=lambda v: numkey((v,)))

    head = ['/'.join(rows) or 'config'] + ['/'.join(c) for c in col_list] + ['TOTAL', 'n']
    widths = [max(len(head[0]), *(len('/'.join(r)) for r in row_list))] + \
             [max(9, len(h)) for h in head[1:]]
    print('  '.join(h.ljust(w) for h, w in zip(head, widths)))
    print('  '.join('-' * w for w in widths))

    totals = {}
    for rk in row_list:
        cells, tot, n = [], 0, 0
        for ck in col_list:
            vs = [data[(rk, ck, pk)] for pk in pair_list if (rk, ck, pk) in data]
            if not vs:
                cells.append('.')
                continue
            mean = sum(vs) / len(vs)
            cells.append('%.1f' % mean if len(vs) > 1 else '%d' % vs[0])
            tot += mean
            n += len(vs)
        totals[rk] = tot
        line = ['/'.join(rk)] + cells + ['%.1f' % tot, '%d' % n]
        print('  '.join(v.ljust(w) for v, w in zip(line, widths)))

    if len(row_list) > 1:
        base = row_list[0]
        print('\ndelta vs %s (sum over cells, then the per-cell sign count):' % ('/'.join(base) or 'row 0'))
        for rk in row_list[1:]:
            wins = losses = ties = 0
            for ck in col_list:
                for pk in pair_list:
                    a = data.get((base, ck, pk))
                    b = data.get((rk, ck, pk))
                    if a is None or b is None:
                        continue
                    if b > a:
                        wins += 1
                    elif b < a:
                        losses += 1
                    else:
                        ties += 1
            print('  %-24s %+8.1f   paired: %d win / %d loss / %d tie' %
                  ('/'.join(rk), totals[rk] - totals[base], wins, losses, ties))
    return 0

if __name__ == '__main__':
    sys.exit(main())
