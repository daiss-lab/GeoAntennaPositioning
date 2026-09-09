#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${SEEDS:?}" "${SOLVE_END:?}"

. slurm/cells.sh

I="${SLURM_ARRAY_TASK_ID:-0}"
C=$(( I % NCELLS ))
S=$(( I / NCELLS ))
if [ "$C" -ge "$NCELLS" ] || [ "$S" -ge "$SEEDS" ]; then
    echo "array index $I is past the $NCELLS cells in ${CELLS_SOURCE}; nothing to do"
    exit 0
fi
TAU="${TAUS[$C]}"; K="${KS[$C]}"
SEED=$(( 11 + S * 104729 ))

mkdir -p "$RUN"
OUT="${RUN}/s${S}_tau${TAU}_k${K}.txt"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) tau=${TAU} k=${K} seed=${SEED} deadline=$(date -d @${SOLVE_END})"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$SOLVE_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 --exact-lead 0 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
