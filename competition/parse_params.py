#!/usr/bin/env python3

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
    pairs = PAIR.findall(text)
    good = [(t, k) for t, k in pairs if is_tau(t) and is_k(k)]
    if len(good) >= 2 and len(good) == len(pairs):
        return good, "explicit (tau, k) pairs"
    return None

def from_labels(text):
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
        if low in ("k", "ks") or any(w in low for w in K_WORDS[1:]):
            ks.extend(v for v in values if is_k(v))
        elif any(w in low for w in TAU_WORDS):
            taus.extend(v for v in values if is_tau(v))
    if taus and ks:
        return cross(dedupe(taus), dedupe(ks)), "labelled tau and k lines"
    return None

def from_shape(text):
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
