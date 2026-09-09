#!/bin/bash
#SBATCH --job-name=gis4-sweep
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=00:45:00
#SBATCH --output=logs/sweep-%A_%a.out
#SBATCH --error=logs/sweep-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
FLAG="${FLAG:?set FLAG to the solve option to sweep, without the leading --}"
VALUES="${VALUES:?set VALUES to a space-separated list}"
TAG="${TAG:-$FLAG}"
SEEDS_STR="${SEEDS:-11 22}"
CELLS_STR="${CELLS:-0 1 2 3 4 5 6 7 8}"
BUDGET="${BUDGET:-600}"
REP="${REP:-${SLURM_CPUS_PER_TASK:-32}}"
EXTRA="${EXTRA:-}"
OUTDIR="${OUTDIR:-results/sweep_${TAG}}"

read -r -a VALS <<< "$VALUES"
read -r -a SEEDS <<< "$SEEDS_STR"
read -r -a CELLS <<< "$CELLS_STR"
NV=${#VALS[@]}
NS=${#SEEDS[@]}
NC=${#CELLS[@]}

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
VI=$(( I % NV ))
REST=$(( I / NV ))
CI=$(( REST % NC ))
SI=$(( REST / NC ))

if [ "$SI" -ge "$NS" ]; then
    echo "array index $I is past the end ($NV values x $NC cells x $NS seeds)" >&2
    exit 1
fi

VAL="${VALS[$VI]}"
SEED="${SEEDS[$SI]}"
CELL="${CELLS[$CI]}"
TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
SAFE=$(echo "$VAL" | tr -- '-.' 'n_')
OUT="${OUTDIR}/v${SAFE}_s${SEED}_tau${TAU}_k${K}.txt"

echo "host=$(hostname) --${FLAG}=${VAL} seed=${SEED} tau=${TAU} k=${K} rep=${REP} budget=${BUDGET}s"

"$BIN" solve \
    --data "$DATA" --cache "$CACHE" \
    --tau "$TAU" --k "$K" \
    --time "$BUDGET" --sync 90 --replicas "$REP" \
    --seed "$SEED" \
    --"${FLAG}" "$VAL" \
    ${EXTRA} \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
