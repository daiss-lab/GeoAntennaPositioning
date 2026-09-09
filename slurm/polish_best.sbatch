#!/bin/bash
#SBATCH --job-name=gis5-pbest
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=03:00:00
#SBATCH --output=logs/pbest-%A_%a.out
#SBATCH --error=logs/pbest-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
SRC=results/best
OUT=results/best_polished
mkdir -p "$OUT"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(  50   500  1000 50  500 1000 50   500  1000)

C="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$C]}"; K="${KS[$C]}"
IN="${SRC}/tau${TAU}_k${K}.txt"
DST="${OUT}/tau${TAU}_k${K}.txt"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) tau=${TAU} k=${K} in=${IN}"
echo "---- before ----"
"$BIN" score --data "$DATA" --solution "$IN"

"$BIN" refine --data "$DATA" --solution "$IN" \
    --rounds 8 --trials 48 --event-verts 28 --event-radius 220 \
    --relocate-edges 48 --relocate-radius 120 \
    --time 9000 \
    --out "$DST"

echo "---- after ----"
"$BIN" score --data "$DATA" --solution "$DST"

BEFORE=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$IN")
AFTER=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$DST" 2>/dev/null || echo -1)
if [ "${AFTER:--1}" -lt "${BEFORE}" ]; then
    echo "polish did not improve (${AFTER} < ${BEFORE}); keeping the input"
    cp "$IN" "$DST"
fi
echo "cell tau=${TAU} k=${K}: ${BEFORE} -> ${AFTER}"
