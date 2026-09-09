#!/bin/bash
#SBATCH --job-name=giscup-range
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=384G
#SBATCH --time=08:00:00
#SBATCH --output=logs/range-%j.out
#SBATCH --error=logs/range-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
RANGES="${RANGES:-600 1200 2400 100000}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-128}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) threads=${OMP_NUM_THREADS} data=${DATA}"
for R in $RANGES; do
    echo "================ max-range ${R} ================"
    ./bin/gis_cup precompute --data "$DATA" --out "data/range_${R}.bin" --max-range "$R"
    ls -la "data/range_${R}.bin"
done
