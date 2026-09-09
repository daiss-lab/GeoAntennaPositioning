#!/bin/bash
#SBATCH --job-name=giscup-abl
#SBATCH --partition=batch
#SBATCH --array=0-3
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=12:00:00
#SBATCH --output=logs/ablate-%A_%a.out
#SBATCH --error=logs/ablate-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/ablate}"
TIME_BUDGET="${TIME_BUDGET:-600}"
REPLICAS="${REPLICAS:-${SLURM_CPUS_PER_TASK:-16}}"

TAUS=(0.75 0.75 0.5 0.25)
KS=(50 500 500 500)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
K="${KS[$I]}"
SEED=$((1 + I * 7919))

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
mkdir -p "$OUTDIR"

echo "host=$(hostname) tau=$TAU k=$K budget=${TIME_BUDGET}s"

run() {
    local tag="$1"; shift
    echo "======== ${tag} ========"
    ./bin/gis_cup solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
        --time "$TIME_BUDGET" --replicas "$REPLICAS" --seed "$SEED" \
        --out "${OUTDIR}/${tag}_tau${TAU}_k${K}.txt" "$@"
}

run full
run no_bundles   --bundles 0
run no_grasp     --grasp-prob 0
run no_sync      --sync 1000000
run phi_squared  --phi-exp 2.0

echo "======== summary (exact scores) ========"
for TAG in full no_bundles no_grasp no_sync phi_squared; do
    printf '%-12s ' "$TAG"
    ./bin/gis_cup score --data "$DATA" \
        --solution "${OUTDIR}/${TAG}_tau${TAU}_k${K}.txt" | grep -E "^re-scored"
done
