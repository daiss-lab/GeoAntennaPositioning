#!/bin/bash
# Confirms the partial-progress exponent at the two k values the first sweep skipped.
#
# slurm/phiexp.sbatch swept gamma at k = 500 only, and found gamma = 2 beating gamma = 1
# on 6 of 6 paired seeds at tau >= 0.5 (means: 6152 -> 6171 at tau 0.5, 3176 -> 3206 at
# tau 0.75) while being neutral-to-negative at tau = 0.25.  That is now the default in
# solve_cmd.cpp, so it has to hold at k = 50 and k = 1000 as well -- the mechanism is
# about completions needing more than one antenna, and how much that binds depends on how
# many antennas there are to spend.
#
# Paired by seed, so the comparison that matters is the sign per cell, not the spread
# across cells.
#
#   sbatch slurm/phiexp2.sbatch
#
#SBATCH --job-name=giscup-phi2
#SBATCH --partition=batch
#SBATCH --array=0-17
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=12:00:00
#SBATCH --output=logs/phiexp2-%A_%a.out
#SBATCH --error=logs/phiexp2-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/phiexp2}"
TIME_BUDGET="${TIME_BUDGET:-600}"
REPLICAS="${REPLICAS:-${SLURM_CPUS_PER_TASK:-16}}"
EXPS="${EXPS:-1.0 2.0}"

#            0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16   17
TAUS=(    0.25 0.25 0.25 0.25 0.25 0.25  0.5  0.5  0.5  0.5  0.5  0.5 0.75 0.75 0.75 0.75 0.75 0.75)
KS=(        50   50   50 1000 1000 1000   50   50   50 1000 1000 1000   50   50   50 1000 1000 1000)
SEEDS=(     11   22   33   11   22   33   11   22   33   11   22   33   11   22   33   11   22   33)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
K="${KS[$I]}"
SEED="${SEEDS[$I]}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
mkdir -p "$OUTDIR"

echo "host=$(hostname) tau=$TAU k=$K seed=$SEED budget=${TIME_BUDGET}s"

for G in $EXPS; do
    echo "======== phi-exp ${G} ========"
    ./bin/gis_cup solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
        --time "$TIME_BUDGET" --replicas "$REPLICAS" --seed "$SEED" --phi-exp "$G" \
        --out "${OUTDIR}/g${G}_tau${TAU}_k${K}_s${SEED}.txt"
done

echo "======== summary tau=${TAU} k=${K} seed=${SEED} ========"
for G in $EXPS; do
    printf 'phi-exp %-5s ' "$G"
    ./bin/gis_cup score --data "$DATA" \
        --solution "${OUTDIR}/g${G}_tau${TAU}_k${K}_s${SEED}.txt" | grep -E "^re-scored"
done
