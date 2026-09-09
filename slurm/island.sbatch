#!/bin/bash
#SBATCH --job-name=gis4-island
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=192G
#SBATCH --time=06:00:00
#SBATCH --output=logs/island-%A_%a.out
#SBATCH --error=logs/island-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
BUDGET="${BUDGET:-2400}"
ISLANDS="${ISLANDS:-8}"
SHARE="${SHARE:-1}"
TAG="${TAG:-sh}"
REP="${REP:-128}"
SYNC="${SYNC:-60}"
IMMIGRANTS="${IMMIGRANTS:-2}"
OUTDIR="${OUTDIR:-results/island_${TAG}}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
CELL=$(( I / ISLANDS ))
ISL=$(( I % ISLANDS ))

TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
OUT="${OUTDIR}/tau${TAU}_k${K}_isl${ISL}.txt"

SHARE_ARGS=()
if [ "$SHARE" = "1" ]; then
    SHARE_DIR="${OUTDIR}/share_tau${TAU}_k${K}"
    mkdir -p "$SHARE_DIR"
    SHARE_ARGS=(--share-dir "$SHARE_DIR" --island "$ISL" --immigrants "$IMMIGRANTS")
fi

echo "host=$(hostname) cell=${CELL} island=${ISL}/${ISLANDS} tau=${TAU} k=${K} share=${SHARE} budget=${BUDGET}s"
date -u +"start %Y-%m-%dT%H:%M:%SZ"

"$BIN" solve \
    --data "$DATA" \
    --cache "$CACHE" \
    --tau "$TAU" \
    --k "$K" \
    --time "$BUDGET" \
    --sync "$SYNC" \
    --replicas "$REP" \
    --seed $(( 101 + CELL * 7919 + ISL * 104729 )) \
    "${SHARE_ARGS[@]}" \
    --out "$OUT"

date -u +"solved %Y-%m-%dT%H:%M:%SZ"
"$BIN" score --data "$DATA" --solution "$OUT"
date -u +"done %Y-%m-%dT%H:%M:%SZ"
