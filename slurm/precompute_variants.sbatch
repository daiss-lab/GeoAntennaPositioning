#!/bin/bash
#SBATCH --job-name=gis4-pre
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=384G
#SBATCH --time=04:00:00
#SBATCH --output=logs/prevar-%j.out
#SBATCH --error=logs/prevar-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
DATA="${DATA:-data/sample.bin}"

export OMP_NUM_THREADS=128
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"

run() {
    local name="$1" sl="$2" sp="$3"
    echo "================ ${name}: slot-len=${sl} spacing=${sp} ================"
    /usr/bin/time -f "  wall %e s, peak rss %M KB" \
        "$BIN" precompute --data "$DATA" --out "data/${name}.bin" \
            --slot-len "$sl" --spacing "$sp" --max-range 2400
    ls -la "data/${name}.bin"
}

run sl010 0.1  4.0
run sl005 0.05 4.0
run sl002 0.02 4.0
run sp2   0.1  2.0
