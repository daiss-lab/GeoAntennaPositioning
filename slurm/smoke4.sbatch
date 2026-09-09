#!/bin/bash
#SBATCH --job-name=gis4-smoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=00:25:00
#SBATCH --output=logs/smoke4-%j.out
#SBATCH --error=logs/smoke4-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"
echo "================ selftest ================"
./bin/gis_cup selftest

mkdir -p results/smoke4
echo "================ solve tau=0.5 k=500, 60 s ================"
./bin/gis_cup solve --data data/sample.bin --cache data/range_2400.bin \
    --tau 0.5 --k 500 --time 60 --replicas 32 --seed 11 \
    --out results/smoke4/tau0.5_k500.txt

echo "================ score it back ================"
./bin/gis_cup score --data data/sample.bin --solution results/smoke4/tau0.5_k500.txt

echo "================ re-score the Solution03 baseline answers ================"
for f in ../Solution03/results/refine_hard/*.txt; do
    echo "---- $(basename "$f")"
    ./bin/gis_cup score --data data/sample.bin --solution "$f"
done
