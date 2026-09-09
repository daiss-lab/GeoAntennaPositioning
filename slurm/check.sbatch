#!/bin/bash
# End-to-end check: rebuild the visibility cache, solve one sub-problem, then verify the
# answer with the independent sampling checker.
#
#SBATCH --job-name=giscup-chk
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=04:00:00
#SBATCH --output=logs/check-%j.out
#SBATCH --error=logs/check-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-32}"
TAU="${TAU:-0.5}"
K="${K:-500}"
TIME_BUDGET="${TIME_BUDGET:-90}"
NBLD="${NBLD:-200}"

echo "=== precompute ==="
./bin/gis_cup precompute --data data/sample.bin --out data/sample_arcs.bin

echo "=== solve tau=$TAU k=$K ==="
./bin/gis_cup solve --data data/sample.bin --cache data/sample_arcs.bin \
    --tau "$TAU" --k "$K" --time "$TIME_BUDGET" --replicas 16 \
    --out "results/check_t${TAU}_k${K}.txt"

echo "=== independent verification ==="
./.venv/bin/python python/verify.py \
    --geojson ../dataset/GIS-cup-sample-dataset.geojson \
    --solution "results/check_t${TAU}_k${K}.txt" \
    --step 0.25 --sample-buildings "$NBLD"
