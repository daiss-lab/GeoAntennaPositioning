#!/bin/bash
# GO.sh -- competition day, one command, dataset in and uploadable archive out.
#
#   ./competition/GO.sh <dataset.geojson> <competition-parameters.txt> [hours] [seeds]
#
# Example, on the day:
#
#   ./competition/GO.sh dataset/GIS-cup-competition-dataset.geojson \
#                       dataset/competition-parameters.txt 20
#
# The dataset is published 2026-08-15 16:00 UTC and the answer is due 2026-08-16 16:00 UTC.
# `hours` is how much of that window to spend before the archive is built; it defaults to
# 20, leaving four hours of margin for the upload and for anything that has to be redone.
#
# What this does, in order:
#
#   0.  checks the things that cannot be fixed later -- binary present, venv present,
#       dataset readable, parameters readable
#   1.  reads competition-parameters.txt into a (tau, k) cell list
#   2.  submits prepare -> precompute -> solve -> polish -> finalize to SLURM, with every
#       stage on an absolute deadline derived from `hours`
#   3.  leaves competition/FINISH.sh to be run once those finish: it validates the
#       assembled submission against the organisers' own parser and builds the archive
#
# Step 3 is deliberately not chained onto step 2.  The archive is the one artefact a human
# should look at before it is uploaded, and a packaging step that runs unattended at 04:00
# and then sits there is worth less than one run deliberately.  FINISH.sh is idempotent, so
# it can be run as often as you like while the answers improve.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

GEOJSON="${1:?usage: GO.sh <dataset.geojson> <parameters.txt> [hours] [seeds]}"
PARAMS="${2:?usage: GO.sh <dataset.geojson> <parameters.txt> [hours] [seeds]}"
HOURS="${3:-20}"
SEEDS="${4:-4}"

PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"

echo "=============================================================="
echo " GIS Cup 2026 -- submission run"
echo " dataset:    $GEOJSON"
echo " parameters: $PARAMS"
echo " window:     ${HOURS} h, ${SEEDS} seed(s) per cell"
echo " started:    $(date -Is)"
echo "=============================================================="
echo

# ---- 0. the preconditions, all of them, before anything is submitted -------------------
fatal=0
note() { echo "  !! $*"; fatal=$(( fatal + 1 )); }

[ -r "$GEOJSON" ] || note "cannot read dataset $GEOJSON"
[ -r "$PARAMS" ]  || note "cannot read parameters $PARAMS"
[ -x "$PY" ]      || note "no python at $PY (set PY=...)"
if [ ! -x "$BIN" ]; then
    echo "  no binary at $BIN; building"
    make -s || note "make failed"
fi
[ -x "$BIN" ] || note "still no binary at $BIN"

# -march=native means the binary is built for whichever host built it.  A mismatch on a
# compute node is an illegal instruction, not a wrong answer, so it is worth one second to
# find out now rather than in the first array task.
#
# What is being tested is "does this binary execute on this CPU", not "does it like these
# arguments".  `--help` prints the usage and exits 2, so testing for exit 0 fails a
# perfectly good binary -- which it did, on the morning of the competition.  A CPU mismatch
# is SIGILL, and the shell reports a signal as 128+N, so only that range is fatal here.
#
# The `|| rc=$?` is load-bearing under `set -e`: a bare nonzero command would take the
# whole script down before it reported anything.
if [ -x "$BIN" ]; then
    rc=0
    "$BIN" --help >/dev/null 2>&1 || rc=$?
    [ "$rc" -lt 128 ] || note "$BIN will not run here (signal $(( rc - 128 )); try: make portable)"
fi

"$PY" -c "import shapely, numpy" 2>/dev/null || \
    echo "  (warning) shapely/numpy missing: the independent verifier will be skipped"

if [ "$fatal" -gt 0 ]; then
    echo
    echo "$fatal precondition(s) failed.  Nothing has been submitted."
    exit 1
fi
echo "  preconditions ok"
echo

# ---- 1. the parameters -----------------------------------------------------------------
mkdir -p data results logs
CELLS_FILE="$ROOT/data/cells.txt"
echo "---- reading the competition parameters ----"
"$PY" competition/parse_params.py "$PARAMS" --out "$CELLS_FILE" || {
    status=$?
    if [ "$status" -eq 3 ]; then
        echo
        echo "The parameters file did not yield the nine cells the rules describe."
        echo "Read the dump above.  If the reading is right, re-run with --expect 0;"
        echo "if it is wrong, re-run parse_params.py with --taus/--ks and then"
        echo "start the pipeline directly:"
        echo "    CELLS_FILE=$CELLS_FILE PY=$PY ./slurm/pipeline5.sh $GEOJSON $HOURS $SEEDS"
        exit 1
    fi
    exit "$status"
}
export CELLS_FILE
echo

# Keep the exact inputs next to the outputs.  On a 24-hour clock the question "which
# dataset was this solved against" must never need archaeology.
cp -f "$GEOJSON" "$ROOT/data/competition-dataset.geojson" 2>/dev/null || true
cp -f "$PARAMS" "$ROOT/data/competition-parameters.txt" 2>/dev/null || true

# ---- 2. the pipeline -------------------------------------------------------------------
echo "---- submitting the pipeline ----"
PY="$PY" BIN="$BIN" CELLS_FILE="$CELLS_FILE" ./slurm/pipeline5.sh "$GEOJSON" "$HOURS" "$SEEDS"

cat <<EOF

=============================================================="
 submitted.

 watch:      squeue -u \$USER
 answers:    results/final/ and results/submission.txt
 then run:   ./competition/FINISH.sh $GEOJSON

 FINISH.sh validates the submission with the organisers' own parser and writes
 Final_Submission.zip.  Run it once the finalize job has completed -- and it is
 safe to run earlier, on whatever has been checkpointed so far, to prove the
 whole path works while there is still time to fix it.
=============================================================="
EOF
