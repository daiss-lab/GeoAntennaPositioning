#!/bin/bash
# FINISH.sh -- validate the assembled answers and build the archive to upload.
#
#   ./competition/FINISH.sh <dataset.geojson> [submission.txt] [output-name]
#
# Run after GO.sh's SLURM jobs have finished -- and run it early too, on partial results,
# because it is the only part of the day that has never been rehearsed against the real
# dataset and the only part that cannot be started late.  It is idempotent: every run
# overwrites the archive from whatever the answers currently are.
#
# Three things happen, and the first two can stop the third:
#
#   1. the organisers' own parser and submission validator read the file -- the same
#      parseSolutionText / validateSubproblemInput the hosted evaluator calls.  This is
#      what catches antennas off a boundary, claims naming buildings the dataset does not
#      contain, and a block count that does not match the parameters file.  It takes
#      seconds because it stops short of the visibility geometry.
#   2. an independent structural check in Python, which needs no Node runtime.
#   3. the archive is written, with the solution file and the source directory in it,
#      which is what the rules ask to be uploaded.
#
# Scoring -- what the answers are actually worth -- is a separate, much slower job:
#   $PY python/split_submission.py results/submission.txt results/cells
#   sbatch --array=0-8%9 --export=ALL,GEOJSON=<dataset>,CELLS=results/cells,\
#OUT=results/official slurm/official_eval.sbatch

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

GEOJSON="${1:?usage: FINISH.sh <dataset.geojson> [submission.txt] [output-name]}"
SUBMISSION="${2:-results/submission.txt}"
OUTNAME="${3:-Final_Submission}"

PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
CELLS_FILE="${CELLS_FILE:-data/cells.txt}"
EVALDIR="${EVALDIR:-evaluator/official}"
NODEBIN="${NODEBIN:-evaluator/node/bin}"
REPORT="$ROOT/results/validation.json"

[ -r "$GEOJSON" ]    || { echo "cannot read dataset $GEOJSON"; exit 1; }
[ -r "$SUBMISSION" ] || { echo "no submission at $SUBMISSION -- has finalize run?"; exit 1; }

CELLS_ARG=()
if [ -r "$CELLS_FILE" ]; then
    CELLS_ARG=(--cells "$CELLS_FILE")
else
    echo "(warning) no cell list at $CELLS_FILE; the parameters cross-check is skipped"
fi

echo "=============================================================="
echo " validating $SUBMISSION"
echo "=============================================================="

# ---- 1. the organisers' own parser -----------------------------------------------------
VALIDATED=0
if [ -x "$NODEBIN/node" ] && [ -f "$EVALDIR/official-validate.ts" ]; then
    echo
    echo "---- the organisers' parser and submission validator ----"
    (
        export PATH="$ROOT/$NODEBIN:$PATH"
        export NODE_OPTIONS="--max-old-space-size=24576"
        cd "$EVALDIR"
        node node_modules/.bin/vite-node official-validate.ts -- \
            "$ROOT/$GEOJSON" "$ROOT/$SUBMISSION" \
            "${CELLS_FILE:+$ROOT/$CELLS_FILE}" "$REPORT"
    ) && VALIDATED=1 || {
        echo
        echo "The organisers' parser rejected this submission.  The archive was NOT built."
        echo "Fix what it reported above and run this again."
        exit 1
    }
else
    echo "(warning) no vendored evaluator; falling back to the Python checks alone"
fi

# ---- 2 and 3. structural check, then the archive ---------------------------------------
echo
echo "---- packaging ----"
REQUIRE=()
[ "$VALIDATED" -eq 1 ] && REQUIRE=(--require-valid "$REPORT")

"$PY" competition/package.py \
    --solution "$SUBMISSION" \
    --dataset "$GEOJSON" \
    --out "$ROOT/$OUTNAME" \
    --root "$ROOT" \
    --force \
    "${CELLS_ARG[@]}" \
    "${REQUIRE[@]}"

echo
echo "=============================================================="
echo " Upload $ROOT/${OUTNAME}.zip to"
echo "   https://easychair.org/conferences/?conf=giscup2026"
echo "=============================================================="
