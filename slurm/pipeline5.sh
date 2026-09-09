#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"

GEOJSON="${1:?usage: pipeline5.sh <dataset.geojson> <hours> [seeds-per-cell]}"
HOURS="${2:?usage: pipeline5.sh <dataset.geojson> <hours> [seeds-per-cell]}"
SEEDS="${3:-4}"

PY="${PY:-./.venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA=data/eval.bin
CACHE=data/eval_arcs.bin
RUN=results/run

. slurm/cells.sh
export CELLS_FILE
echo "cells: ${NCELLS} from ${CELLS_SOURCE}"

NOW=$(date +%s)
DEADLINE=$(( NOW + $(printf '%.0f' "$(echo "$HOURS * 3600" | bc -l)") ))
SOLVE_END=$(( NOW + (DEADLINE - NOW) * 80 / 100 ))
POLISH_END=$(( NOW + (DEADLINE - NOW) * 93 / 100 ))

mkdir -p "$RUN" logs data results

echo "pipeline5: deadline $(date -d @${DEADLINE}), solve to $(date -d @${SOLVE_END}), \
polish to $(date -d @${POLISH_END}), ${SEEDS} seeds per cell"

"$PY" python/prepare.py "$GEOJSON" "$DATA"
echo "prepared $DATA"

PRE_CPUS="${PRE_CPUS:-96}"
PRE_MEM="${PRE_MEM:-256G}"
J_PRE=$(sbatch --parsable \
    --job-name=gis5-pre --partition=batch --nodes=1 --ntasks=1 --cpus-per-task="$PRE_CPUS" \
    --mem="$PRE_MEM" --time=01:00:00 --output=logs/p5-pre-%j.out --error=logs/p5-pre-%j.err \
    --wrap "export OMP_NUM_THREADS=$PRE_CPUS OMP_PROC_BIND=spread OMP_PLACES=cores; \
            $BIN precompute --data $DATA --out $CACHE --slot-len 0.05 --spacing 4.0 \
                 --max-range 2400")
echo "precompute: $J_PRE"

N=$(( NCELLS * SEEDS ))
J_SOLVE=$(sbatch --parsable --dependency=afterany:"$J_PRE" \
    --array=0-$(( N - 1 ))%"$N" \
    --job-name=gis5-solve --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=24:00:00 --output=logs/p5-solve-%A_%a.out \
    --error=logs/p5-solve-%A_%a.err \
    --export=ALL,DATA="$DATA",CACHE="$CACHE",BIN="$BIN",RUN="$RUN",SEEDS="$SEEDS",\
SOLVE_END="$SOLVE_END" \
    slurm/wave5.sbatch)
echo "solve: $J_SOLVE"

J_POL=$(sbatch --parsable --dependency=afterany:"$J_SOLVE" \
    --array=0-$(( NCELLS - 1 ))%"$NCELLS" \
    --job-name=gis5-polish --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=06:00:00 --output=logs/p5-pol-%A_%a.out --error=logs/p5-pol-%A_%a.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",POLISH_END="$POLISH_END" \
    slurm/polish5.sbatch)
echo "polish: $J_POL"

J_FIN=$(sbatch --parsable --dependency=afterany:"$J_POL" \
    --job-name=gis5-final --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=02:00:00 --output=logs/p5-fin-%j.out --error=logs/p5-fin-%j.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",PY="$PY",GEOJSON="$GEOJSON" \
    slurm/finalize5.sbatch)
echo "finalize: $J_FIN"

echo
echo "submitted.  the submission will be at results/submission.txt"
echo "watch with: squeue -u \$USER"
