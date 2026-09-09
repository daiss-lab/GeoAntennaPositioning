#!/bin/bash
#SBATCH --job-name=giscup-solve
#SBATCH --partition=batch
#SBATCH --array=0-8
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=24:00:00
#SBATCH --output=logs/solve-%A_%a.out
#SBATCH --error=logs/solve-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/sample_arcs.bin}"
OUTDIR="${OUTDIR:-results}"
TIME_BUDGET="${TIME_BUDGET:-600}"
REPLICAS="${REPLICAS:-${SLURM_CPUS_PER_TASK:-16}}"
SYNC="${SYNC:-90}"
DEADLINE="${DEADLINE:-0}"
EXTRA="${EXTRA:-}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
K="${KS[$I]}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
echo "host=$(hostname) task=$I tau=$TAU k=$K replicas=$REPLICAS budget=${TIME_BUDGET}s deadline=${DEADLINE}"

./bin/gis_cup solve \
    --data "$DATA" \
    --cache "$CACHE" \
    --tau "$TAU" \
    --k "$K" \
    --time "$TIME_BUDGET" \
    --deadline "$DEADLINE" \
    --sync "$SYNC" \
    --replicas "$REPLICAS" \
    --seed $((1 + I * 7919)) \
    --out "${OUTDIR}/tau${TAU}_k${K}.txt" \
    ${EXTRA}
