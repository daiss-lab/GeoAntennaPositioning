#!/bin/bash
#SBATCH --job-name=gis4-near
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=00:40:00
#SBATCH --output=logs/nearmiss-%j.out
#SBATCH --error=logs/nearmiss-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
SRC="${SRC:-../Solution03/results/refine_hard}"
OUTDIR=results/cov
mkdir -p "$OUTDIR"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) source=${SRC}"
for f in "$SRC"/*.txt; do
    base=$(basename "$f" .txt)
    echo "---- $base"
    "$BIN" score --data data/sample.bin --solution "$f" \
        --dump-cov "$OUTDIR/${base}.cov" 2>&1 | grep -E "re-scored|claim line"
done
ls -la "$OUTDIR"
