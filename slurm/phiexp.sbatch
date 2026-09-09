#!/bin/bash
#SBATCH --job-name=giscup-phi
#SBATCH --partition=batch
#SBATCH --array=0-8
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=12:00:00
#SBATCH --output=logs/phiexp-%A_%a.out
#SBATCH --error=logs/phiexp-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/phiexp}"
TIME_BUDGET="${TIME_BUDGET:-600}"
REPLICAS="${REPLICAS:-${SLURM_CPUS_PER_TASK:-16}}"
K="${K:-500}"
EXPS="${EXPS:-1.0 1.5 2.0 3.0}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
SEEDS=(11 22 33 11 22 33 11 22 33)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
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

echo "======== summary tau=${TAU} seed=${SEED} ========"
for G in $EXPS; do
    printf 'phi-exp %-5s ' "$G"
    ./bin/gis_cup score --data "$DATA" \
        --solution "${OUTDIR}/g${G}_tau${TAU}_k${K}_s${SEED}.txt" | grep -E "^re-scored"
done
