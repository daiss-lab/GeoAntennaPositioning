#!/bin/bash
#SBATCH --job-name=gis5-smoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=00:25:00
#SBATCH --output=logs/smoke5-%j.out
#SBATCH --error=logs/smoke5-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
CACHE=data/sl010.bin
OUT=results/smoke5
mkdir -p "$OUT"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"

for cell in "0.75 500" "0.25 500"; do
    set -- $cell
    TAU=$1 K=$2
    for BETA in 0 0.5; do
        NAME="b${BETA}_tau${TAU}_k${K}"
        echo "================ tau=${TAU} k=${K} beta=${BETA} ================"
        "$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
            --time 120 --replicas 32 --seed 11 --sync 60 \
            --difficulty-beta "$BETA" --target-destroy 0 \
            --out "${OUT}/${NAME}.txt"
        "$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
    done
done

echo "================ beta=0 must equal Solution04 ================"
"$BIN" score --data "$DATA" --solution "${OUT}/b0_tau0.75_k500.txt"
