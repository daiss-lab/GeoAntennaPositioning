#!/bin/bash
# Visibility precompute: the one expensive stage, shared by all nine sub-problems.
#
#   sbatch slurm/precompute.sbatch
#   DATA=data/eval.bin CACHE=data/eval_arcs.bin sbatch slurm/precompute.sbatch
#
#SBATCH --job-name=giscup-pre
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=256G
#SBATCH --time=04:00:00
#SBATCH --output=logs/precompute-%j.out
#SBATCH --error=logs/precompute-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/sample_arcs.bin}"
SPACING="${SPACING:-4.0}"
SLOT_LEN="${SLOT_LEN:-0.1}"
MAX_RANGE="${MAX_RANGE:-2400}"
EXTRA="${EXTRA:-}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-128}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) threads=${OMP_NUM_THREADS} data=${DATA} cache=${CACHE}"
/usr/bin/time -v ./bin/gis_cup precompute \
    --data "${DATA}" \
    --out "${CACHE}" \
    --spacing "${SPACING}" \
    --slot-len "${SLOT_LEN}" \
    --max-range "${MAX_RANGE}" \
    ${EXTRA}
