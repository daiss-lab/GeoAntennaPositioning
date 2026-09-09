#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${BIN:?}" "${RUN:?}" "${GEOJSON:?}"
PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
CLAIM_BAND="${CLAIM_BAND:-0}"
OFFICIAL="${OFFICIAL:-0}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(  50   500  1000 50  500 1000 50   500  1000)

export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-32}
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p results/final results/scored
TOTAL=0
MISSING=0

for C in $(seq 0 8); do
    TAU="${TAUS[$C]}"; K="${KS[$C]}"
    BEST=""; BEST_N=-1

    for f in "${RUN}"/s*_tau${TAU}_k${K}.txt "${RUN}"/polished_tau${TAU}_k${K}.txt; do
        [ -e "$f" ] || continue
        N=$("$BIN" score --data "$DATA" --solution "$f" 2>/dev/null \
            | sed -n 's/^SCORE .* serviced=\([0-9]*\) .*/\1/p')
        [ -z "$N" ] && continue
        echo "  candidate $f -> $N"
        if [ "$N" -gt "$BEST_N" ]; then BEST_N="$N"; BEST="$f"; fi
    done

    if [ -z "$BEST" ]; then
        echo "!! tau=${TAU} k=${K}: NO ANSWER"
        MISSING=$(( MISSING + 1 ))
        continue
    fi

    echo "---- tau=${TAU} k=${K}: ${BEST} scores ${BEST_N} ----"
    "$BIN" score --data "$DATA" --solution "$BEST" \
        --claim-band "$CLAIM_BAND" \
        --rewrite "results/final/tau${TAU}_k${K}.txt" \
        | tee "results/scored/tau${TAU}_k${K}.txt"
    TOTAL=$(( TOTAL + BEST_N ))
done

echo
echo "================ total across nine sub-problems: ${TOTAL} ================"
[ "$MISSING" -gt 0 ] && echo "!! ${MISSING} sub-problems have no answer"

"$PY" python/assemble.py --dir results/final --out results/submission.txt
echo "wrote results/submission.txt"

"$PY" python/check_submission.py --geojson "$GEOJSON" --submission results/submission.txt

if [ "$OFFICIAL" = "1" ]; then
    "$PY" python/split_submission.py results/submission.txt results/cells
    echo "submit the official evaluator with:"
    echo "  sbatch --array=0-8%9 --export=ALL,GEOJSON=$GEOJSON,CELLS=results/cells,\
OUT=results/official slurm/official_eval.sbatch"
fi
