#!/bin/bash
# Greedy-only evaluation: what each solver's construction phase is worth before any LNS.
#
# `--time 0` makes the LNS loop's `while (nowSec() < t_end)` false on entry, so the run is
# greedy plus the final exact evaluation and nothing else.  `--replicas 1` pins Solution02
# to replica 0, which is its plain deterministic greedy -- the randomised starts exist to
# feed the LNS and would only add noise to a measurement of the construction itself.  Both
# solvers are therefore deterministic here and the seed is recorded only for the log.
#
# Laid out as the same 2x2 as bench.sbatch, {Solution01, Solution02} x {600 m, 2400 m
# cache}, because the working radius is a flag both accept and folding its effect into the
# solver comparison would misattribute it.
#
# Every answer is re-scored by `score`, so all 36 numbers come from one evaluator reading
# the emitted files rather than from each solver marking its own work.
#
#   sbatch slurm/greedy.sbatch
#
#SBATCH --job-name=giscup-greedy
#SBATCH --partition=batch
#SBATCH --array=0-8
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=04:00:00
#SBATCH --output=logs/greedy-%A_%a.out
#SBATCH --error=logs/greedy-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
NEAR_CACHE="${NEAR_CACHE:-data/range_600.bin}"
FAR_CACHE="${FAR_CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/greedy}"
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
echo "host=$(hostname) tau=$TAU k=$K  GREEDY ONLY (--time 0 --replicas 1)"

run() {
    local tag="$1" bin="$2" cache="$3"
    echo "======== ${tag} ========"
    "$bin" solve --data "$DATA" --cache "$cache" --tau "$TAU" --k "$K" \
        --time 0 --replicas 1 --seed "$SEED" \
        --out "${OUTDIR}/${tag}_tau${TAU}_k${K}.txt"
}

run old_near "$OLD" "$NEAR_CACHE"
run new_near "$NEW" "$NEAR_CACHE"
run old_far  "$OLD" "$FAR_CACHE"
run new_far  "$NEW" "$FAR_CACHE"

echo "======== greedy scores, re-scored by one evaluator ========"
for TAG in old_near new_near old_far new_far; do
    F="${OUTDIR}/${TAG}_tau${TAU}_k${K}.txt"
    printf '%-10s tau=%-5s k=%-5s ' "$TAG" "$TAU" "$K"
    "$NEW" score --data "$DATA" --solution "$F" | grep -E "^re-scored|not supported"
done
