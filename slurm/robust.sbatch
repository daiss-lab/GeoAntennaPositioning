#!/bin/bash
#SBATCH --job-name=gis4-robust
#SBATCH --partition=batch
#SBATCH --array=0-41%14
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=128G
#SBATCH --time=01:30:00
#SBATCH --output=logs/robust-%A_%a.out
#SBATCH --error=logs/robust-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
OUTDIR="${OUTDIR:-results/robust}"
BUDGET="${BUDGET:-600}"
REP="${REP:-64}"

TAUS=(0.15 0.25 0.35 0.5 0.6 0.75 0.9)
KS=(25 50 200 500 1000 2000)

I="${SLURM_ARRAY_TASK_ID:-0}"
TI=$(( I / 6 ))
KI=$(( I % 6 ))
TAU="${TAUS[$TI]}"
K="${KS[$KI]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
OUT="${OUTDIR}/tau${TAU}_k${K}.txt"

echo "host=$(hostname) tau=${TAU} k=${K} budget=${BUDGET}s"
date -u +"start %Y-%m-%dT%H:%M:%SZ"

"$BIN" solve \
    --data "$DATA" --cache "$CACHE" \
    --tau "$TAU" --k "$K" \
    --time "$BUDGET" --sync 90 --replicas "$REP" --seed 11 \
    --slot-margin -1 \
    --out "$OUT"

date -u +"solved %Y-%m-%dT%H:%M:%SZ"
"$BIN" score --data "$DATA" --solution "$OUT"
date -u +"done %Y-%m-%dT%H:%M:%SZ"
