#!/bin/bash

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

F="${1:?usage: claimband.sh <F> [--score <dataset.geojson>]}"
shift
SCORE=""
if [ "${1:-}" = "--score" ]; then
    SCORE="${2:?--score needs the dataset path}"
fi

PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA="${DATA:-data/eval.bin}"
CELLS_FILE="${CELLS_FILE:-data/cells.txt}"
SRC="${SRC:-results/final}"
WORK="results/band$F"

[ -d "$SRC" ] || { echo "no $SRC -- has finalize run?"; exit 1; }

rm -rf "$WORK" "$WORK-labelled"
mkdir -p "$WORK"

for f in "$SRC"/*.txt; do
    [ -e "$f" ] || continue
    "$BIN" score --data "$DATA" --solution "$f" --claim-band "$F" \
        --rewrite "$WORK/$(basename "$f")" > "$WORK/$(basename "$f" .txt).score" 2>&1
done

IDS="${DATA}.ids"
DIR="$WORK"
if [ -r "$IDS" ]; then
    "$PY" python/relabel.py --ids "$IDS" --in "$WORK" --out "$WORK-labelled"
    DIR="$WORK-labelled"
fi
OUT="results/submission-band$F.txt"
"$PY" python/assemble.py --dir "$DIR" --out "$OUT" \
    ${CELLS_FILE:+--cells "$CELLS_FILE"}

echo
echo "---- claims added by F=$F, per cell ----"
printf '%-22s %10s %10s %10s\n' cell serviced claimed added
base_total=0; band_total=0
for f in "$SRC"/*.txt; do
    [ -e "$f" ] || continue
    b=$(basename "$f" .txt)
    n0=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$f")
    n1=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$WORK/$b.txt")
    printf '%-22s %10s %10s %10s\n' "$b" "$n0" "$n1" "$(( n1 - n0 ))"
    base_total=$(( base_total + n0 )); band_total=$(( band_total + n1 ))
done
printf '%-22s %10s %10s %10s\n' TOTAL "$base_total" "$band_total" "$(( band_total - base_total ))"
echo
echo "wrote $OUT"

if [ -n "$SCORE" ]; then
    echo
    echo "---- submitting the organisers' evaluator on $OUT ----"
    "$PY" python/split_submission.py "$OUT" "results/cells-band$F"
    N=$(ls "results/cells-band$F"/cell*.txt | wc -l)
    sbatch --array=0-$(( N - 1 ))%"$N" \
        --export=ALL,GEOJSON="$SCORE",CELLS="results/cells-band$F",OUT="results/official-band$F" \
        slurm/official_eval.sbatch
    echo "results will be in results/official-band$F/"
fi
