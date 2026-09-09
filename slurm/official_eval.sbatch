#!/bin/bash
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
export NODE_OPTIONS="--max-old-space-size=24576"

cd "$EVALDIR"
node node_modules/.bin/vite-node official-score.ts -- \
    "$OLDPWD/$GEOJSON" "$OLDPWD/$CELL" "$OLDPWD/$OUT/$BASE.json" \
    2>&1 | tee "$OLDPWD/$OUT/$BASE.log"

echo "finished $(date -Is)"
