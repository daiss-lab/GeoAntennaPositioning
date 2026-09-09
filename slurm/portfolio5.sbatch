#!/bin/bash

#SBATCH --job-name=gis5-port
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=12:00:00
#SBATCH --output=logs/p5-port-%A_%a.out
#SBATCH --error=logs/p5-port-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${SOLVE_END:?}"

TARGETS=(
    "0.32 484 exact"  "0.32 484 exact"  "0.32 484 phi"  "0.32 484 base"
    "0.49 484 exact"  "0.49 484 exact"  "0.49 484 phi"  "0.49 484 base"
    "0.68 484 exact"  "0.68 484 exact"  "0.68 484 phi"  "0.68 484 base"
)

I="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$I" -ge "${#TARGETS[@]}" ]; then
    echo "task $I is past the ${#TARGETS[@]} targets; nothing to do"
    exit 0
fi
read -r TAU K ARM <<< "${TARGETS[$I]}"

EXTRA=()
case "$ARM" in
    exact) EXTRA=(--exact-lead 8) ;;
    phi)   EXTRA=(--phi-exp 1.25) ;;
    base)  EXTRA=() ;;
esac

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

OUT="${RUN}/s2${I}_tau${TAU}_k${K}.txt"
SEED=$(( 31337 + I * 65537 ))

echo "host=$(hostname) tau=${TAU} k=${K} arm=${ARM} seed=${SEED} until $(date -d @${SOLVE_END})"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
    --slot-len 0.05 \
    --deadline "$SOLVE_END" --time 86400 \
    --replicas 32 --seed "$SEED" --sync 120 \
    --shortlist 24 \
    --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
    "${EXTRA[@]}" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
