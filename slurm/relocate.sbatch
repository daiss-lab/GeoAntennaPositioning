#!/bin/bash
#SBATCH --job-name=gis5-reloc
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=02:00:00
#SBATCH --output=logs/reloc-%A_%a.out
#SBATCH --error=logs/reloc-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
SRC=../Solution04/results/fact
OUT=results/reloc
mkdir -p "$OUT"

TAUS=(0.25 0.25 0.25 0.5 0.5  0.5 0.75 0.75 0.75)
KS=(  50   500  1000 50  500  1000 50  500  1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 2 ))
C=$(( I / 2 ))
TAU="${TAUS[$C]}"; K="${KS[$C]}"

case "$A" in
    0) REL=0  ;;
    1) REL=48 ;;
esac

IN="${SRC}/t0_m0_s11_tau${TAU}_k${K}.txt"
NAME="r${REL}_tau${TAU}_k${K}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${A} relocate=${REL} tau=${TAU} k=${K} in=${IN}"

echo "---- input, re-scored here so both arms are measured against one baseline ----"
"$BIN" score --data "$DATA" --solution "$IN"

"$BIN" refine --data "$DATA" --solution "$IN" \
    --rounds 6 --trials 48 --event-verts 28 --event-radius 220 \
    --relocate-edges "$REL" --relocate-radius 120 \
    --time 5400 \
    --out "${OUT}/${NAME}.txt"

echo "---- refined ----"
"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
