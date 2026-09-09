#!/usr/bin/env python3
"""
relabel.py -- rewrite a solution file's claim line into the dataset's own building ids.

    python3 relabel.py --ids data/eval.bin.ids --in results/final --out results/labelled
    python3 relabel.py --ids data/eval.bin.ids --in one.txt --out one_labelled.txt

The solver carries an i64 handle per building because that is what the binary format
holds.  The organisers' loader canonicalises an id as `String(properties.id).trim()` and
accepts any string or number, so on a dataset whose ids are not small integers the handle
is a position, not the id, and a claim line quoting handles would name buildings the
evaluator has never heard of -- every claim rejected as unknown, every cell scoring zero.

`prepare.py` writes the mapping to `<binary>.ids` as `<handle>\t<canonical>` per line.
This applies it.  When the dataset does use integer ids the mapping is the identity and
the output is byte-identical to the input, which is the case the sample dataset exercises
and the reason this step is safe to run unconditionally.

Only line 3 of each block is touched.  The (tau, k) header and the antenna coordinates are
copied through verbatim -- notably without reformatting the coordinates, because a reparse
and reprint is a chance to lose a digit of the 17 the submission needs.
"""

import argparse
import os
import sys


def load_mapping(path):
    mapping = {}
    with open(path) as fh:
        for line_no, line in enumerate(fh, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            handle, _, canonical = line.partition("\t")
            if not _:
                raise SystemExit("%s:%d: expected <handle>TAB<id>" % (path, line_no))
            mapping[handle.strip()] = canonical
    if not mapping:
        raise SystemExit("%s: empty id map" % path)
    return mapping


def relabel_text(text, mapping, source):
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    if len(lines) % 3 != 0:
        raise SystemExit("%s: %d lines, not a multiple of 3" % (source, len(lines)))

    identity = True
    unknown = 0
    for index in range(2, len(lines), 3):
        claims = [t.strip() for t in lines[index].split(",") if t.strip()]
        out = []
        for claim in claims:
            if claim in mapping:
                canonical = mapping[claim]
                if canonical != claim:
                    identity = False
                out.append(canonical)
            else:
                # Not silently dropped: a handle with no mapping means the claim line and
                # the id map came from different datasets, which is a bug worth stopping
                # for rather than a submission quietly missing buildings.
                unknown += 1
                out.append(claim)
        lines[index] = ",".join(out)

    if unknown:
        raise SystemExit("%s: %d claimed ids are not in the id map -- the solution and the "
                         "map are from different datasets" % (source, unknown))
    return "\n".join(lines) + "\n", identity


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", required=True, help="the <binary>.ids sidecar from prepare.py")
    ap.add_argument("--in", dest="source", required=True, help="solution file or directory")
    ap.add_argument("--out", dest="dest", required=True, help="output file or directory")
    args = ap.parse_args()

    mapping = load_mapping(args.ids)

    if os.path.isdir(args.source):
        if not os.path.isdir(args.dest):
            os.makedirs(args.dest)
        names = sorted(n for n in os.listdir(args.source) if n.endswith(".txt"))
        if not names:
            raise SystemExit("%s: no .txt solution files" % args.source)
        changed = 0
        for name in names:
            src = os.path.join(args.source, name)
            with open(src) as fh:
                text = fh.read()
            out, identity = relabel_text(text, mapping, src)
            with open(os.path.join(args.dest, name), "w") as fh:
                fh.write(out)
            if not identity:
                changed += 1
        print("relabelled %d file(s) into %s (%d had ids rewritten)"
              % (len(names), args.dest, changed))
    else:
        with open(args.source) as fh:
            text = fh.read()
        out, identity = relabel_text(text, mapping, args.source)
        parent = os.path.dirname(args.dest)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(args.dest, "w") as fh:
            fh.write(out)
        print("relabelled %s -> %s%s"
              % (args.source, args.dest, "" if not identity else " (identity mapping)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
