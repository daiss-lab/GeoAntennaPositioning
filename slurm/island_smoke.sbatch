#!/bin/bash
#SBATCH --job-name=gis4-ismoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=00:25:00
#SBATCH --output=logs/ismoke-%j.out
#SBATCH --error=logs/ismoke-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
OUTDIR=results/ismoke
SHARE_DIR="$OUTDIR/share"
rm -rf "$OUTDIR"
mkdir -p "$SHARE_DIR"

export OMP_NUM_THREADS=10
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"
for ISL in 0 1 2; do
    "$BIN" solve --data data/sample.bin --cache data/range_2400.bin \
        --tau 0.75 --k 500 --time 300 --sync 30 --replicas 10 \
        --seed $(( 7 + ISL * 104729 )) \
        --share-dir "$SHARE_DIR" --island "$ISL" --immigrants 2 \
        --out "$OUTDIR/isl${ISL}.txt" > "$OUTDIR/isl${ISL}.stdout" 2> "$OUTDIR/isl${ISL}.log" &
done
wait

echo "================ migration lines ================"
grep -H "islands:" "$OUTDIR"/isl*.log | tail -30
echo "================ per-island exact score ================"
for ISL in 0 1 2; do
    echo "---- island $ISL"
    "$BIN" score --data data/sample.bin --solution "$OUTDIR/isl${ISL}.txt" 2>&1 | grep -E "re-scored|claim line"
done
echo "================ share directory ================"
ls -la "$SHARE_DIR"
head -c 200 "$SHARE_DIR"/isl_0.sol; echo
