#!/bin/bash

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
