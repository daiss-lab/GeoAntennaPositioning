#!/usr/bin/env python3
"""
parse_params.py -- read the competition's `competition-parameters.txt` into a cell list.

    python3 parse_params.py competition-parameters.txt --out data/cells.txt
    python3 parse_params.py --taus 0.25,0.5,0.75 --ks 50,500,1000 --out data/cells.txt

Output is one `<tau> <k>` per line, tau-major, which is the order every stage of the
pipeline indexes its SLURM array by.

Why this is tolerant rather than strict
---------------------------------------
The organisers publish the parameters as a file whose format we have not seen, and the
problem statement is explicit that the sample's values are only an example: "we encourage
participants to experiment with different values.  The above values are just an example."
Hard-coding 0.25/0.5/0.75 and 50/500/1000 -- which is what every stage of this pipeline
did until now -- risks solving nine sub-problems nobody asked for, which scores zero on
all nine while looking perfectly healthy the whole way through.

So four readings are tried in order of how specific they are, and whichever fires first
wins.  Every one of them prints what it concluded, because the failure this guards against
is silent.  If all four are wrong, `--taus/--ks` overrides the file entirely, and that is
the escape hatch to use on the day rather than editing this script under time pressure.

The tau *text* is carried through verbatim, not reformatted.  `0.3` printed back as
`0.29999999999999999` parses to the identical double and would score the same, but the
submission should read the way the parameters file reads.
"""

import argparse
import json
import os
import re
import sys

NUMBER = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
PAIR = re.compile(r"\(\s*(" + NUMBER.pattern + r")\s*,\s*(" + NUMBER.pattern + r")\s*\)")

TAU_WORDS = ("tau", "threshold", "coverage", "fraction", "τ")
K_WORDS = ("k", "antenna", "antennas", "count", "number")


def strip_comments(text):
    out = []
    for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        line = re.sub(r"(#|//).*$", "", line)
        out.append(line)
    return "\n".join(out)


def is_tau(text):
    try:
        value = float(text)
    except ValueError:
        return False
    return 0.0 < value <= 1.0 and ("." in text or "e" in text.lower())


def is_k(text):
    try:
        value = int(text)
    except ValueError:
        return False
    return value >= 1


def dedupe(items):
    seen, out = set(), []
    for item in items:
        key = float(item)
        if key not in seen:
            seen.add(key)
            out.append(item)
    return out


def from_json(text):
    """`{"tau": [...], "k": [...]}` or a list of pairs."""
    try:
        data = json.loads(text)
    except (ValueError, TypeError):
        return None
    if isinstance(data, dict):
        taus, ks = [], []
        for key, value in data.items():
            target = None
            low = str(key).strip().lower()
            if any(low.startswith(w) for w in TAU_WORDS):
                target = taus
            elif any(low.startswith(w) for w in K_WORDS):
                target = ks
            if target is None:
                continue
            for item in (value if isinstance(value, list) else [value]):
                target.append(repr(float(item)) if target is taus else str(int(item)))
        if taus and ks:
            return cross(taus, ks), "JSON object with tau and k lists"
    return None


def from_pairs(text):
    """Explicit `(tau, k)` pairs -- the most specific reading, so it is tried first."""
    pairs = PAIR.findall(text)
    good = [(t, k) for t, k in pairs if is_tau(t) and is_k(k)]
    if len(good) >= 2 and len(good) == len(pairs):
        return good, "explicit (tau, k) pairs"
    return None


def from_labels(text):
    """Lines that name what they hold: `tau: 0.25, 0.5, 0.75` and `k = 50 500 1000`."""
    taus, ks = [], []
    for line in text.split("\n"):
        if ":" in line:
            label, _, rest = line.partition(":")
        elif "=" in line:
            label, _, rest = line.partition("=")
        else:
            continue
        low = label.strip().lower()
        values = NUMBER.findall(rest)
        if not values:
            continue
        # `k` is checked before tau: a label of exactly "k" must not match a tau word.
        if low in ("k", "ks") or any(w in low for w in K_WORDS[1:]):
            ks.extend(v for v in values if is_k(v))
        elif any(w in low for w in TAU_WORDS):
            taus.extend(v for v in values if is_tau(v))
    if taus and ks:
        return cross(dedupe(taus), dedupe(ks)), "labelled tau and k lines"
    return None


def from_shape(text):
    """No labels: every number in the file, split by what it can legally be."""
    values = NUMBER.findall(text)
    taus = dedupe([v for v in values if is_tau(v)])
    ks = dedupe([v for v in values if is_k(v)])
    if taus and ks:
        return cross(taus, ks), "unlabelled numbers, split by range"
    return None


def cross(taus, ks):
    return [(t, k) for t in taus for k in ks]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("params", nargs="?", help="competition-parameters.txt")
    ap.add_argument("--out", required=True, help="where to write the cell list")
    ap.add_argument("--taus", help="override: comma-separated tau values")
    ap.add_argument("--ks", help="override: comma-separated k values")
    ap.add_argument("--expect", type=int, default=9,
                    help="how many cells there should be (0 to accept any)")
    args = ap.parse_args()

    if args.taus and args.ks:
        taus = [t.strip() for t in args.taus.split(",") if t.strip()]
        ks = [k.strip() for k in args.ks.split(",") if k.strip()]
        bad = [t for t in taus if not is_tau(t)] + [k for k in ks if not is_k(k)]
        if bad:
            raise SystemExit("not a usable tau or k: %s" % ", ".join(bad))
        cells, how = cross(taus, ks), "--taus/--ks on the command line"
    elif args.params:
        if not os.path.exists(args.params):
            raise SystemExit("no such parameters file: %s" % args.params)
        with open(args.params) as fh:
            raw = fh.read()
        print("---- %s ----" % args.params)
        print(raw.rstrip("\n"))
        print("---- end ----\n")
        text = strip_comments(raw)
        found = None
        for reader in (from_json, from_pairs, from_labels, from_shape):
            found = reader(text)
            if found:
                break
        if not found:
            raise SystemExit(
                "could not read any tau/k values from %s.\n"
                "Read the file above and re-run with, for example:\n"
                "    --taus 0.25,0.5,0.75 --ks 50,500,1000" % args.params)
        cells, how = found
    else:
        raise SystemExit("give a parameters file, or both --taus and --ks")

    print("read as: %s" % how)
    print("%d cell(s):" % len(cells))
    for tau, k in cells:
        print("    tau=%-8s k=%s" % (tau, k))

    if args.expect and len(cells) != args.expect:
        # Not fatal on its own -- the organisers set the count, not us -- but it is the
        # one thing worth stopping the eye on, so it is loud and it changes the exit code.
        print("\n!! expected %d cells, read %d.  Check the file above before continuing."
              % (args.expect, len(cells)))

    parent = os.path.dirname(args.out)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with open(args.out, "w") as fh:
        fh.write("# derived from %s by parse_params.py (%s)\n"
                 % (args.params or "command line", how))
        for tau, k in cells:
            fh.write("%s %s\n" % (tau, k))
    print("\nwrote %s" % args.out)

    return 0 if (not args.expect or len(cells) == args.expect) else 3


if __name__ == "__main__":
    sys.exit(main())
