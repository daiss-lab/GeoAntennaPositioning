#!/bin/bash
# Caches for the discretisation study.  The visibility cache stores each candidate's
# coverage as slot runs, so the slot length is baked into it -- testing a finer
# discretisation means rebuilding, not just passing a flag to solve.
#
#   sl010  0.10 m slots, 4 m candidate spacing   the incumbent, rebuilt here so every
#                                                arm of the study comes from one binary
#   sl005  0.05 m slots
#   sl002  0.02 m slots                          42.9M slots; the largest building needs
#                                                53,303, still inside the 65,536 cap
#   sp2    0.10 m slots, 2 m candidate spacing   twice the candidates
#
# All at max-range 2400 m, which explanation.md §2 settled.
#
#   sbatch slurm/precompute_variants.sbatch
#
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

run() {  # name slot_len spacing
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
