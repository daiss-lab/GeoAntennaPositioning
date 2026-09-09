#!/bin/bash
# One long solve per (cell, seed), driven by an absolute deadline.  Submitted by
# pipeline5.sh, which exports DATA, CACHE, BIN, RUN, SEEDS and SOLVE_END.
#
# The array index decomposes CELL-fastest, so a partly-finished array still covers all nine
# cells rather than finishing the first cells and never starting the last -- a submission
# with every cell answered once beats one with three cells answered four times.
#
# This was the stated intent from the start but not what the code did: with S = I % SEEDS
# the first four tasks were four seeds of cell 0, and an array that only got a third of its
# tasks scheduled would have answered three cells and left six blank.  On an uncontended
# cluster every task starts at once and the mapping cannot be observed; on a contended one
# it decides what the submission contains.  Tasks 0..NCELLS-1 are now seed 0 of every cell,
# the next NCELLS are seed 1, and so on.  Nothing downstream depends on the mapping --
# polish and finalize glob s*_tau${TAU}_k${K}.txt and never reconstruct an index.

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${SEEDS:?}" "${SOLVE_END:?}"

. slurm/cells.sh

I="${SLURM_ARRAY_TASK_ID:-0}"
C=$(( I % NCELLS ))      # cell varies fastest: every cell gets a seed before any gets two
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

# --deadline rather than --time: a task that waited in the queue shortens itself.
# --shortlist 24 rather than 8: across 291 historical shortlists the best slot score was the
# best exact score only 53% of the time and the true winner sat as deep as rank 7 of 8.
# This runs after the search and takes the max over strictly more candidates, so it cannot
# pick worse; it costs about 0.4 s per extra placement.
# --exact-lead stays 0 until slurm/leadsweep.sbatch reports.  It is the same observation
# applied inside the search, which is the more valuable version and the riskier one, and an
# unmeasured change does not belong in the run that produces the submission.
# --slot-len 0.05 must match what precompute built.  The discretisation study (job 995474)
# measured 0.05 m beating the incumbent 0.1 m by +61 over four cells, and the gain is
# concentrated where the mechanism predicts: +49 and +33 on the two tau = 0.5 cells, which
# are the ones holding the near-rectangles whose two visible walls come to 0.49999998 of the
# perimeter and which the 0.1 m midpoint rule rounds up to "finished".  0.02 m was worse
# than 0.05 m (-35): five times the slots buys accuracy nobody needs and costs iterations.
# The three features Solution04 added and its own sweeps measured as neutral-to-harmful are
# off: no slot margin, no targeted destroy, no island sharing.
"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$SOLVE_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 --exact-lead 0 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
