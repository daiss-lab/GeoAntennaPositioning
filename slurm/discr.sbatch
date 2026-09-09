#!/bin/bash
#SBATCH --job-name=gis5-discr
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=01:00:00
#SBATCH --output=logs/discr-%A_%a.out
#SBATCH --error=logs/discr-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
OUT=results/discr
mkdir -p "$OUT"

ARMS=(sl010 sl005 sl002 sp2)
SLOTLEN=(0.1  0.05  0.02  0.1)
TAUS=(0.5  0.5  0.75 0.25)
KS=(  500  1000 500  500)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 4 ))
C=$(( I / 4 ))

ARM="${ARMS[$A]}"
SL="${SLOTLEN[$A]}"
TAU="${TAUS[$C]}"
K="${KS[$C]}"
NAME="${ARM}_tau${TAU}_k${K}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${ARM} slot-len=${SL} tau=${TAU} k=${K}"

"$BIN" solve --data "$DATA" --cache "data/${ARM}.bin" --slot-len "$SL" \
    --tau "$TAU" --k "$K" \
    --time 900 --replicas 32 --seed 11 --sync 60 \
    --target-destroy 0 \
    --out "${OUT}/${NAME}.txt"

"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
