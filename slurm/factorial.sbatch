#!/bin/bash
#SBATCH --job-name=gis4-fact
#SBATCH --partition=batch
#SBATCH --array=0-215%24
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=96G
#SBATCH --time=01:30:00
#SBATCH --output=logs/fact-%A_%a.out
#SBATCH --error=logs/fact-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
OUTDIR="${OUTDIR:-results/fact}"
BUDGET="${BUDGET:-900}"
REP="${REP:-64}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)
SEEDS=(11 22 33)
CFG_TARGET=(0 0 0 0 1 1 1 1)
CFG_MARGIN=(0 0.01 0.02 0.04 0 0.01 0.02 0.04)

I="${SLURM_ARRAY_TASK_ID:-0}"
CFG=$(( I / 27 ))
REST=$(( I % 27 ))
SEEDI=$(( REST / 9 ))
CELL=$(( REST % 9 ))

TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"
SEED="${SEEDS[$SEEDI]}"
TGT="${CFG_TARGET[$CFG]}"
MGN="${CFG_MARGIN[$CFG]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
OUT="${OUTDIR}/t${TGT}_m${MGN}_s${SEED}_tau${TAU}_k${K}.txt"

echo "host=$(hostname) cfg=${CFG} target=${TGT} margin=${MGN} seed=${SEED} tau=${TAU} k=${K}"

"$BIN" solve \
    --data "$DATA" --cache "$CACHE" \
    --tau "$TAU" --k "$K" \
    --time "$BUDGET" --sync 90 --replicas "$REP" \
    --seed "$SEED" \
    --target-destroy "$TGT" \
    --slot-margin "$MGN" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
