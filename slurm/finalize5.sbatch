#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${BIN:?}" "${RUN:?}" "${GEOJSON:?}"
PY="${PY:-./.venv/bin/python}"

if [ ! -x "$PY" ]; then
    echo "finalize5: no python at ${PY}; relabel and assemble cannot run." >&2
    echo "finalize5: pass PY=<interpreter with shapely>, as competition/GO.sh does." >&2
    exit 1
fi

. slurm/cells.sh

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p results/final
TOTAL=0
MISSING=0

for C in $(seq 0 $(( NCELLS - 1 ))); do
    TAU="${TAUS[$C]}"; K="${KS[$C]}"
    BEST=""; BEST_N=-1
    for f in "${RUN}"/s*_tau${TAU}_k${K}.txt "${RUN}"/polished_tau${TAU}_k${K}.txt; do
        [ -e "$f" ] || continue
        N=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$f")
        [ -z "$N" ] && continue
        if [ "$N" -gt "$BEST_N" ]; then BEST_N="$N"; BEST="$f"; fi
    done

    if [ -z "$BEST" ]; then
        echo "!! tau=${TAU} k=${K}: NO ANSWER"
        MISSING=$(( MISSING + 1 ))
        continue
    fi

    cp "$BEST" "results/final/tau${TAU}_k${K}.txt"
    echo "---- tau=${TAU} k=${K}: ${BEST} ----"
    "$BIN" score --data "$DATA" --solution "results/final/tau${TAU}_k${K}.txt"
    TOTAL=$(( TOTAL + BEST_N ))
done

echo
echo "================ total across ${NCELLS} sub-problems: ${TOTAL} ================"
[ "$MISSING" -gt 0 ] && echo "!! ${MISSING} sub-problems have no answer"

VERIFY_TIMEOUT="${VERIFY_TIMEOUT:-900}"
if [ -x "$PY" ]; then
    for C in $(seq $(( NCELLS > 2 ? NCELLS - 2 : 0 )) $(( NCELLS - 1 ))); do
        TAU="${TAUS[$C]}"; K="${KS[$C]}"
        F="results/final/tau${TAU}_k${K}.txt"
        [ -e "$F" ] || continue
        echo "---- independent verify tau=${TAU} k=${K} (budget ${VERIFY_TIMEOUT}s) ----"
        RC=0
        timeout "$VERIFY_TIMEOUT" "$PY" python/verify.py --geojson "$GEOJSON" \
            --solution "$F" || RC=$?
        case "$RC" in
            0)   ;;
            124) echo "(verifier hit its ${VERIFY_TIMEOUT}s budget for tau=${TAU} k=${K}; not a disagreement)" ;;
            *)   echo "!! verifier disagreed for tau=${TAU} k=${K}" ;;
        esac
    done
else
    echo "no venv at $PY; skipping the independent verifier"
fi

IDS="${DATA}.ids"
FINAL_DIR=results/final
if [ -r "$IDS" ]; then
    "$PY" python/relabel.py --ids "$IDS" --in results/final --out results/labelled
    FINAL_DIR=results/labelled
else
    echo "!! no id sidecar at ${IDS}; assuming the solver's ids are the dataset's"
fi

"$PY" python/assemble.py --dir "$FINAL_DIR" --out results/submission.txt \
    ${CELLS_FILE:+--cells "$CELLS_FILE"}
echo "wrote results/submission.txt"
wc -l results/submission.txt
