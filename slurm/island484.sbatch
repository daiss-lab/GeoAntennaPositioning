#!/bin/bash

#SBATCH --job-name=gis5-island
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=06:00:00
#SBATCH --output=logs/p5-isl-%A_%a.out
#SBATCH --error=logs/p5-isl-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${ISLAND_END:?}"

ISLANDS="${ISLANDS:-8}"
CELLS=("0.32 484" "0.49 484" "0.68 484")

I="${SLURM_ARRAY_TASK_ID:-0}"
NTASK=$(( ${#CELLS[@]} * ISLANDS ))
if [ "$I" -ge "$NTASK" ]; then
    echo "task $I is past the ${NTASK} (cell, island) pairs; nothing to do"
    exit 0
fi

C=$(( I / ISLANDS ))
ISL=$(( I % ISLANDS ))
read -r TAU K <<< "${CELLS[$C]}"

SHARE="${SHARE:-0}"
SHARE_ARGS=()
if [ "$SHARE" = "1" ]; then
    SHARE_DIR="${RUN}/share/tau${TAU}_k${K}"
    mkdir -p "$SHARE_DIR"
    SHARE_ARGS=(--share-dir "$SHARE_DIR" --island "$ISL" --immigrants 2)
fi
mkdir -p "$RUN"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

OUT="${RUN}/s5${I}_tau${TAU}_k${K}.txt"
SEED=$(( 104729 + I * 7919 ))

echo "host=$(hostname) tau=${TAU} k=${K} seed-slot=${ISL}/${ISLANDS} share=${SHARE}"
echo "deadline $(date -u -d @${ISLAND_END}) UTC"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$ISLAND_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 --exact-lead 0 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    ${SHARE_ARGS[@]+"${SHARE_ARGS[@]}"} \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
