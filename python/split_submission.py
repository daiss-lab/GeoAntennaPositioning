#!/usr/bin/env python3

import os
import sys

def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: split_submission.py <submission.txt> <outdir>\n")
        return 2

    src, outdir = sys.argv[1], sys.argv[2]
    with open(src) as fh:
        lines = fh.read().replace("\r\n", "\n").replace("\r", "\n").split("\n")
    while lines and lines[-1] == "":
        lines.pop()

    if len(lines) % 3 != 0:
        sys.stderr.write("error: %s has %d lines, not a multiple of 3\n" % (src, len(lines)))
        return 1

    if not os.path.isdir(outdir):
        os.makedirs(outdir)

    written = []
    for index in range(0, len(lines), 3):
        header = lines[index].strip()
        tau, k = header.lstrip("(").rstrip(")").split(",")
        tau, k = tau.strip(), k.strip()
        name = "cell%02d_tau%s_k%s.txt" % (index // 3, tau, k)
        path = os.path.join(outdir, name)
        with open(path, "w") as fh:
            fh.write("\n".join(lines[index:index + 3]) + "\n")
        written.append((path, tau, k, len(lines[index + 1].split("),")),
                        len(lines[index + 2].split(","))))

    for path, tau, k, n_ant, n_claim in written:
        print("%s  tau=%s k=%s antennas=%d claims=%d" % (path, tau, k, n_ant, n_claim))
    return 0

if __name__ == "__main__":
    sys.exit(main())
