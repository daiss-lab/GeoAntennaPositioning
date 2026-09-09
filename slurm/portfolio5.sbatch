#!/bin/bash
# portfolio5.sbatch -- extra arms on the three k=484 cells, each with a DIFFERENT search
# setting rather than merely a different seed.  Submitted by hand for the short-window run.
#
# WHY A PORTFOLIO AND NOT MORE SEEDS
#
# finalize takes the maximum over every answer file on disk, so an extra arm can never make
# the submission worse -- the only cost is the cores it occupies.  That makes parameter
# settings which are *plausible but unmeasured* exactly the right thing to spend spare
# capacity on: measuring them properly costs a controlled A/B we do not have time for, but
# running them alongside the baseline costs nothing but cores and keeps the upside.
#
# Two arms, both aimed at the k=484 cells because those are the only ones still climbing:
#
#   exact-lead 8 -- at each replica sync, re-score the top 8 replicas with the EXACT
#     evaluator and promote the leader from that, instead of trusting the slot surrogate.
#     DOCUMENTATION reports that across 291 historical shortlists the best slot score was
#     the best exact score only 53% of the time, and the true winner sat as deep as rank 7
#     of 8.  The search has therefore been picking its leader close to a coin flip, and this
#     is the same correction --shortlist already applies at the very end, applied throughout
#     the run where it can still steer the search.  wave5.sbatch deliberately leaves this at
#     0 because an unmeasured change does not belong in the run that produces the
#     submission -- which is precisely why it belongs in an arm that runs BESIDE it.
#
#   phi-exp 1.25 -- the surrogate credits partial coverage linearly; an exponent above 1
#     favours buildings that are nearly finished over buildings barely started.  At tau=0.68
#     a large share of buildings sit just short of the threshold, so the cell where this
#     should matter most is exactly the one with the most headroom left.  Above 1 the
#     objective stops being submodular and the greedy bound no longer applies, which is a
#     reason to hedge it rather than adopt it.
#
# Files are s2<task>_tau<tau>_k<k>.txt, which the existing s* glob already collects.

#SBATCH --job-name=gis5-port
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=12:00:00
#SBATCH --output=logs/p5-port-%A_%a.out
#SBATCH --error=logs/p5-port-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${SOLVE_END:?}"

# task -> "tau k arm"
TARGETS=(
    "0.32 484 exact"  "0.32 484 exact"  "0.32 484 phi"  "0.32 484 base"
    "0.49 484 exact"  "0.49 484 exact"  "0.49 484 phi"  "0.49 484 base"
    "0.68 484 exact"  "0.68 484 exact"  "0.68 484 phi"  "0.68 484 base"
)

I="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$I" -ge "${#TARGETS[@]}" ]; then
    echo "task $I is past the ${#TARGETS[@]} targets; nothing to do"
    exit 0
fi
read -r TAU K ARM <<< "${TARGETS[$I]}"

EXTRA=()
case "$ARM" in
    exact) EXTRA=(--exact-lead 8) ;;
    phi)   EXTRA=(--phi-exp 1.25) ;;
    base)  EXTRA=() ;;
esac

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

OUT="${RUN}/s2${I}_tau${TAU}_k${K}.txt"
SEED=$(( 31337 + I * 65537 ))

echo "host=$(hostname) tau=${TAU} k=${K} arm=${ARM} seed=${SEED} until $(date -d @${SOLVE_END})"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$SOLVE_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    "${EXTRA[@]}" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
