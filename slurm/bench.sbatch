#!/bin/bash
# Head-to-head, as a 2x2: {Solution01, Solution02} x {600 m cache, 2400 m cache}.
#
# Two things changed between the solutions and they need separating.  The optimiser is
# different, and so is the working radius the visibility cache is built at -- and the
# radius is a flag both solvers accept, so attributing its effect to the new code would
# be wrong.  Running all four cells at an identical time budget and replica count says
# how much each is worth on its own.
#
# Every answer is re-scored afterwards by `score`, which reads the emitted file the way a
# third party would.  The comparison is therefore one evaluator reading four files, not
# each solver marking its own work.
#
#   sbatch slurm/bench.sbatch
#   TIME_BUDGET=1800 sbatch slurm/bench.sbatch
#
#SBATCH --job-name=giscup-bench
#SBATCH --partition=batch
#SBATCH --array=0-8
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=12:00:00
#SBATCH --output=logs/bench-%A_%a.out
#SBATCH --error=logs/bench-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
NEAR_CACHE="${NEAR_CACHE:-data/range_600.bin}"
FAR_CACHE="${FAR_CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/bench}"
TIME_BUDGET="${TIME_BUDGET:-600}"
REPLICAS="${REPLICAS:-${SLURM_CPUS_PER_TASK:-16}}"
OLD="${OLD:-../Solution01/bin/gis_cup}"
NEW="${NEW:-./bin/gis_cup}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
K="${KS[$I]}"
SEED=$((1 + I * 7919))

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
echo "host=$(hostname) tau=$TAU k=$K replicas=$REPLICAS budget=${TIME_BUDGET}s"

run() {
    local tag="$1" bin="$2" cache="$3"
    echo "======== ${tag} ========"
    "$bin" solve --data "$DATA" --cache "$cache" --tau "$TAU" --k "$K" \
        --time "$TIME_BUDGET" --replicas "$REPLICAS" --seed "$SEED" \
        --out "${OUTDIR}/${tag}_tau${TAU}_k${K}.txt"
}

run old_near "$OLD" "$NEAR_CACHE"
run new_near "$NEW" "$NEAR_CACHE"
run old_far  "$OLD" "$FAR_CACHE"
run new_far  "$NEW" "$FAR_CACHE"

echo "======== re-scored by one evaluator ========"
for TAG in old_near new_near old_far new_far; do
    F="${OUTDIR}/${TAG}_tau${TAU}_k${K}.txt"
    printf '%-10s ' "$TAG"
    "$NEW" score --data "$DATA" --solution "$F" | grep -E "^re-scored|not supported"
done
