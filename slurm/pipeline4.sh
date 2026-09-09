#!/bin/bash

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"

GEOJSON="${1:?usage: pipeline4.sh <dataset.geojson> <hours> [islands-per-cell]}"
HOURS="${2:?usage: pipeline4.sh <dataset.geojson> <hours> [islands-per-cell]}"
ISLANDS="${3:-6}"

PY="${PY:-./.venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA=data/eval.bin
CACHE=data/eval_arcs.bin
RUN=results/run

NOW=$(date +%s)
DEADLINE=$(( NOW + $(printf '%.0f' "$(echo "$HOURS * 3600" | bc -l)") ))
W1=$(( NOW + (DEADLINE - NOW) * 45 / 100 ))
W2=$(( NOW + (DEADLINE - NOW) * 90 / 100 ))

echo "submission window closes $(date -d @${DEADLINE} -u +%Y-%m-%dT%H:%M:%SZ)"
echo "  wave 1 until $(date -d @${W1} -u +%H:%M:%SZ), wave 2 until $(date -d @${W2} -u +%H:%M:%SZ)"
echo "  ${ISLANDS} islands per cell over 9 cells = $(( ISLANDS * 9 )) jobs per wave"

mkdir -p data logs "$RUN"

echo "== prepare =="
"$PY" python/prepare.py "$GEOJSON" "$DATA"

PRE=$(DATA="$DATA" CACHE="$CACHE" sbatch --parsable \
        --export=ALL,BIN="$BIN",DATA="$DATA",CACHE="$CACHE" slurm/precompute.sbatch)
echo "== precompute: job $PRE =="

WAVE1=$(sbatch --parsable --dependency=afterok:"$PRE" \
        --array=0-$(( ISLANDS * 9 - 1 ))%$(( ISLANDS * 9 )) \
        --export=ALL,BIN="$BIN",DATA="$DATA",CACHE="$CACHE",ISLANDS="$ISLANDS",\
DEADLINE="$W1",OUTDIR="$RUN",SHAREROOT="$RUN/share",WAVE=1 \
        slurm/wave.sbatch)
echo "== wave 1: job $WAVE1 =="

WAVE2=$(sbatch --parsable --dependency=afterany:"$WAVE1" \
        --array=0-$(( ISLANDS * 9 - 1 ))%$(( ISLANDS * 9 )) \
        --export=ALL,BIN="$BIN",DATA="$DATA",CACHE="$CACHE",ISLANDS="$ISLANDS",\
DEADLINE="$W2",OUTDIR="$RUN",SHAREROOT="$RUN/share",WAVE=2 \
        slurm/wave.sbatch)
echo "== wave 2: job $WAVE2 =="

FIN=$(sbatch --parsable --dependency=afterany:"$WAVE2" \
        --export=ALL,BIN="$BIN",DATA="$DATA",GEOJSON="$GEOJSON",RUNDIR="$RUN" \
        slurm/finalize.sbatch)
echo "== finalize: job $FIN =="

echo
echo "submitted.  watch with:  squeue -u $USER"
echo "the submission lands in ${RUN}/submission.txt"
