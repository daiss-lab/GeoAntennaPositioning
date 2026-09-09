#!/bin/bash
# Confidence check on the targeted destroy before a 162-task array depends on it: that it
# runs, that the answer it writes still re-scores cleanly from the file, and that it is not
# obviously worse than the three-mode search on the cell where it should matter most
# (tau = 0.75, where every point needs at least two antennas and the score is flattest).
#
#   sbatch slurm/target_smoke.sbatch
#
#SBATCH --job-name=gis4-tsmoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=00:28:00
#SBATCH --output=logs/tsmoke-%j.out
#SBATCH --error=logs/tsmoke-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
OUTDIR=results/tsmoke
mkdir -p "$OUTDIR"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"
for CELL in "0.75 500" "0.5 1000"; do
    set -- $CELL
    TAU=$1; K=$2
    for TGT in 0 1; do
        OUT="$OUTDIR/t${TGT}_tau${TAU}_k${K}.txt"
        echo "================ tau=$TAU k=$K target-destroy=$TGT ================"
        "$BIN" solve --data data/sample.bin --cache data/range_2400.bin \
            --tau "$TAU" --k "$K" --time 240 --sync 60 --replicas 32 --seed 11 \
            --target-destroy "$TGT" --out "$OUT" 2>&1 | grep -E "RESULT|round [0-9]+:|placement"
        "$BIN" score --data data/sample.bin --solution "$OUT" 2>&1 | grep -E "re-scored|claim line|worst"
    done
done
