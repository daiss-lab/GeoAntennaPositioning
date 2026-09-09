#!/bin/bash
# official_eval.sbatch -- score a submission with the ORGANISERS' evaluator, one cell per task.
#
#   python python/split_submission.py results/submission.txt results/cells
#   sbatch --array=0-8%9 --export=ALL,GEOJSON=...,CELLS=results/cells,OUT=results/official \
#          slurm/official_eval.sbatch
#
# This is the only scorer whose number is the number the organisers will read.  Our own
# `score` command is an independent implementation of the same rules and agreeing with it
# is evidence, not proof; this is the ground truth.  It is slow -- single-threaded
# JavaScript running ArcGIS relate operators -- so each of the nine cells gets its own task.
#
#SBATCH --job-name=gis-oeval
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=32G
#SBATCH --time=12:00:00
#SBATCH --output=logs/oeval-%A_%a.out
#SBATCH --error=logs/oeval-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"

GEOJSON="${GEOJSON:?set GEOJSON=path/to/dataset.geojson}"
CELLS="${CELLS:-results/cells}"
OUT="${OUT:-results/official}"
EVALDIR="${EVALDIR:-evaluator/official}"
NODEBIN="${NODEBIN:-evaluator/node/bin}"

mkdir -p "$OUT"

mapfile -t FILES < <(ls "$CELLS"/cell*.txt | sort)
CELL="${FILES[${SLURM_ARRAY_TASK_ID:-0}]}"
BASE=$(basename "$CELL" .txt)

echo "task ${SLURM_ARRAY_TASK_ID:-0}: $CELL"
echo "host $(hostname), started $(date -Is)"

export PATH="$PWD/$NODEBIN:$PATH"
# The evaluator holds every building's prepared geometry plus an ArcGIS polygon cache;
# 12,860 buildings needs more than V8's default old-space.
export NODE_OPTIONS="--max-old-space-size=24576"

cd "$EVALDIR"
node node_modules/.bin/vite-node official-score.ts -- \
    "$OLDPWD/$GEOJSON" "$OLDPWD/$CELL" "$OLDPWD/$OUT/$BASE.json" \
    2>&1 | tee "$OLDPWD/$OUT/$BASE.log"

echo "finished $(date -Is)"
