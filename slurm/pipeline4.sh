#!/bin/bash
# pipeline4.sh -- the whole submission, from a GeoJSON to a checked submission file, as
# one command and no human decisions.
#
#   ./slurm/pipeline4.sh dataset.geojson 20        # 20 hours from now
#
# The window is 24 hours between the evaluation dataset appearing and the answer being
# due, so every stage is driven by an absolute deadline rather than a duration.  A stage
# that starts late shortens itself; it never overruns.
#
# Five stages, submitted as dependent SLURM jobs, then this returns:
#
#   prepare     GeoJSON -> binary                       (here, seconds)
#   precompute  visibility cache, shared by every cell  (one node)
#   wave 1      ISLANDS jobs per cell, all nine cells, to 45% of the window
#   wave 2      the same again to 90%, picking up wave 1's elites through the share
#               directory, which persists between the waves
#   finalize    pick the best answer per cell out of everything produced, re-score it
#               independently, verify a sample, assemble
#
# Two waves rather than one long one because a job that dies takes its node with it until
# the array ends; restarting halfway puts those nodes back to work, and the share
# directory means a wave-2 island inherits everything wave 1 found instead of starting
# over.  The dependencies are `afterany`, never `afterok`: if one cell's task dies, the
# other eight still have checkpointed answers, and a submission with eight good blocks
# beats no submission.

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

# ---- stage 1: prepare, here, because it takes seconds and everything depends on it ----
echo "== prepare =="
"$PY" python/prepare.py "$GEOJSON" "$DATA"

# ---- stage 2: precompute -------------------------------------------------------------
PRE=$(DATA="$DATA" CACHE="$CACHE" sbatch --parsable \
        --export=ALL,BIN="$BIN",DATA="$DATA",CACHE="$CACHE" slurm/precompute.sbatch)
echo "== precompute: job $PRE =="

# ---- stages 3 and 4: the two island waves --------------------------------------------
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

# ---- stage 5: finalize ---------------------------------------------------------------
FIN=$(sbatch --parsable --dependency=afterany:"$WAVE2" \
        --export=ALL,BIN="$BIN",DATA="$DATA",GEOJSON="$GEOJSON",RUNDIR="$RUN" \
        slurm/finalize.sbatch)
echo "== finalize: job $FIN =="

echo
echo "submitted.  watch with:  squeue -u $USER"
echo "the submission lands in ${RUN}/submission.txt"
