#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${BIN:?}" "${RUN:?}" "${POLISH_END:?}"

. slurm/cells.sh

C="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$C" -ge "$NCELLS" ]; then
    echo "array index $C is past the $NCELLS cells in ${CELLS_SOURCE}; nothing to do"
    exit 0
fi
TAU="${TAUS[$C]}"; K="${KS[$C]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

BEST=""; BEST_N=-1
for f in "${RUN}"/s*_tau${TAU}_k${K}.txt; do
    [ -e "$f" ] || continue
    N=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$f")
    [ -z "$N" ] && continue
    if [ "$N" -gt "$BEST_N" ]; then BEST_N="$N"; BEST="$f"; fi
done

if [ -z "$BEST" ]; then
    echo "no solve output for tau=${TAU} k=${K}; nothing to polish"
    exit 0
fi
echo "host=$(hostname) tau=${TAU} k=${K} best=${BEST} (${BEST_N} buildings)"

OUT="${RUN}/polished_tau${TAU}_k${K}.txt"
"$BIN" refine --data "$DATA" --solution "$BEST" \
    --rounds 6 --trials 48 --event-verts 28 --event-radius 220 \
    --relocate-edges 48 --relocate-radius 120 \
    --deadline "$POLISH_END" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"

NEW=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$OUT" 2>/dev/null || echo -1)
if [ "${NEW:--1}" -lt "$BEST_N" ]; then
    echo "polish did not improve (${NEW} < ${BEST_N}); keeping the unpolished answer"
    cp "$BEST" "$OUT"
fi
