#!/bin/bash
# island484.sbatch -- restart the three k=484 cells as one shared island search.
#
# WHY THIS EXISTS
#
# The k=484 cells hold 32,705 of the 42,762 buildings in the current answer -- 76% of the
# score -- and they were the only cells still climbing when the solve array was cancelled
# at 22:00 UTC on 2026-08-15 to make room for the small-cell restarts.  Their last
# checkpoints say so plainly:
#
#     0.32/484:  16174 -> 16175 -> 16176 -> 16179   (cancelled here)
#     0.49/484:  10627 -> 10628 -> 10629            (cancelled here)
#     0.68/484:  5881  -> 5881  -> 5883             (cancelled here)
#
# Nothing has touched them in the eight hours since, while twelve restart tasks on the
# small cells gained exactly zero over the same period.  So the marginal core-hour is worth
# far more here than anywhere else in the run.
#
# WHY INDEPENDENT SEEDS AND NOT THE ISLAND MODEL
#
# This job was first submitted WITH --share-dir, which contradicts the measured result in
# DOCUMENTATION.md §8: at equal cost, four shared islands lost to four independent seeds on
# both cells tested (10,307 vs 10,330 and 3,215 vs 3,229), and the shared four finished at
# 3,212 / 3,214 / 3,215 / 3,214 -- converged onto one trajectory.  Sharing every 120 s for
# four hours would collapse harder than sharing for ten minutes did, not less.
#
# The doc's winning arm -- one job given 4x the wall clock -- is not on the menu here: we
# are bound by the clock to the deadline, not by cores, and eight nodes are sitting idle.
# So the live choice is eight shared islands versus eight independent seeds over the same
# window, and that is exactly the comparison the table answers.  Independent, best-of-eight.
#
# SHARE=1 restores the island wiring if a later measurement ever inverts this.
#
# SAFETY
#
# Output is written as s5<task>_tau<tau>_k<k>.txt.  Both polish5 and finalize5 rank on the
# emitted claim line and take the MAXIMUM over every s* file, so a task that ends up worse
# than the incumbent s0/s1 is ignored rather than able to lose the cell.  Nothing here
# touches the existing answers, the queued chain, or the already-validated zip.

#SBATCH --job-name=gis5-island
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=06:00:00
#SBATCH --output=logs/p5-isl-%A_%a.out
#SBATCH --error=logs/p5-isl-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${ISLAND_END:?}"

ISLANDS="${ISLANDS:-8}"
CELLS=("0.32 484" "0.49 484" "0.68 484")

I="${SLURM_ARRAY_TASK_ID:-0}"
NTASK=$(( ${#CELLS[@]} * ISLANDS ))
if [ "$I" -ge "$NTASK" ]; then
    echo "task $I is past the ${NTASK} (cell, island) pairs; nothing to do"
    exit 0
fi

# Cell-major: islands of the same cell are consecutive, so a throttled array still fills
# one cell's share directory before starting the next.
C=$(( I / ISLANDS ))
ISL=$(( I % ISLANDS ))
read -r TAU K <<< "${CELLS[$C]}"

SHARE="${SHARE:-0}"
SHARE_ARGS=()
if [ "$SHARE" = "1" ]; then
    SHARE_DIR="${RUN}/share/tau${TAU}_k${K}"
    mkdir -p "$SHARE_DIR"
    SHARE_ARGS=(--share-dir "$SHARE_DIR" --island "$ISL" --immigrants 2)
fi
mkdir -p "$RUN"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

OUT="${RUN}/s5${I}_tau${TAU}_k${K}.txt"
SEED=$(( 104729 + I * 7919 ))

echo "host=$(hostname) tau=${TAU} k=${K} seed-slot=${ISL}/${ISLANDS} share=${SHARE}"
echo "deadline $(date -u -d @${ISLAND_END}) UTC"

# Same search parameters as wave5.sbatch, which produced the incumbents -- this changes
# only how long the search runs and that the tasks talk to each other.  --time 86400 so the
# 60 s default never becomes the binding budget; --deadline is what actually stops it.
"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$ISLAND_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 --exact-lead 0 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    ${SHARE_ARGS[@]+"${SHARE_ARGS[@]}"} \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
