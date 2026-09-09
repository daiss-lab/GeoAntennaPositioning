#!/bin/bash

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

cp -f "$GEOJSON" "$ROOT/data/competition-dataset.geojson" 2>/dev/null || true
cp -f "$PARAMS" "$ROOT/data/competition-parameters.txt" 2>/dev/null || true

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
