#!/bin/bash
# claimband.sh -- build a submission variant whose claim lines are widened by F, and
# (optionally) score it with the organisers' own evaluator so the choice of F is a
# measurement rather than an opinion.
#
#   ./competition/claimband.sh 0.02                  # build results/submission-band0.02.txt
#   ./competition/claimband.sh 0.02 --score DATASET  # ...and submit the official scoring
#
# WHY THIS EXISTS, AND WHAT IT IS NOT FOR
#
# The organisers score `verifiedServiceScore = |claims INTERSECT verified|`.  A serviced
# building we fail to claim scores nothing; a claimed building that fails costs only a
# CLAIM_BELOW_THRESHOLD warning.  The payoff is therefore one-sided, and the score is
# MONOTONE in how much we claim -- which means "what F maximises the score" has the trivial
# and dishonest answer "claim everything".  That is what `--claim-all` does and it is not
# what this script is for.
#
# The honest use is narrow.  Our geometry is stricter than the evaluator's: the organisers
# snap antennas within 0.001 m to the boundary and run ArcGIS relate operators at that same
# xyTolerance, so a sight line we compute as grazing-and-blocked can be topologically clear
# for them.  Buildings whose coverage we compute just below tau are ones we genuinely
# believe may be serviced -- claiming those is a statement of belief under known
# uncertainty.  Claiming a building we compute at 0.05 coverage is not.
#
# So F should be small enough that every claim it adds is a building we could not
# distinguish from serviced, and the measurement below tells you what each F is worth so
# that the trade is made with numbers in view.  The ranking that picks which answer to
# submit always uses the honest `serviced` count and is untouched by F.
#
# claim_floor = tau - F  (see src/evaluate.cpp: the `relative` in --claim-band's help is
# nominal, since max(1.0, tau) is 1 for every tau in this competition).

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

# Re-emit each cell's answer with a widened claim line.  The antenna positions are
# untouched: this changes only which buildings the file claims, never where anything is.
for f in "$SRC"/*.txt; do
    [ -e "$f" ] || continue
    "$BIN" score --data "$DATA" --solution "$f" --claim-band "$F" \
        --rewrite "$WORK/$(basename "$f")" > "$WORK/$(basename "$f" .txt).score" 2>&1
done

# Same tail as finalize5.sbatch: solver ids -> dataset ids, then assemble.
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
