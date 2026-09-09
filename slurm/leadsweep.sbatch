#!/bin/bash
#SBATCH --job-name=gis5-lead
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=01:00:00
#SBATCH --output=logs/lead-%A_%a.out
#SBATCH --error=logs/lead-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
CACHE=data/sl010.bin
OUT=results/lead
mkdir -p "$OUT"

SHORT=(8 24 24 24)
LEAD=( 0  0  6 12)
TAUS=(0.5  0.5  0.75 0.75 0.75 0.25)
KS=(  500  1000 50   500  1000 500)
SEEDS=(11 22)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 4 ));  R=$(( I / 4 ))
C=$(( R % 6 ));  S=$(( R / 6 ))

NAME="a${A}_s${SEEDS[$S]}_tau${TAUS[$C]}_k${KS[$C]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${A} shortlist=${SHORT[$A]} exact-lead=${LEAD[$A]} \
tau=${TAUS[$C]} k=${KS[$C]} seed=${SEEDS[$S]}"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "${TAUS[$C]}" --k "${KS[$C]}" \
    --time 900 --replicas 32 --seed "${SEEDS[$S]}" --sync 60 \
    --shortlist "${SHORT[$A]}" --exact-lead "${LEAD[$A]}" \
    --target-destroy 0 --difficulty-beta 0 \
    --out "${OUT}/${NAME}.txt"

"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
