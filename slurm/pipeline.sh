#!/bin/bash
# The whole thing, in one command, for submission day.
#
#   ./slurm/pipeline.sh <input.geojson> [hours-until-deadline]
#
# Submits four dependent SLURM stages -- convert, precompute, solve x9, finish -- and
# returns immediately.  Everything after the first line is scheduled, so nothing needs a
# human between the dataset arriving and the submission file existing.
#
# The deadline is passed down to the solvers as an absolute timestamp rather than as a
# duration, so a stage that starts late shortens the search instead of overrunning.  Each
# solver rewrites its answer at every checkpoint, so the submission is valid from the
# first checkpoint onward and only improves after that.
set -euo pipefail

cd "$(dirname "$0")/.."

GEOJSON="${1:?usage: pipeline.sh <input.geojson> [hours-until-deadline]}"
HOURS="${2:-20}"
DEADLINE=$(( $(date +%s) + $(printf '%.0f' "$(echo "$HOURS * 3600" | bc -l)") ))

DATA="${DATA:-data/eval.bin}"
CACHE="${CACHE:-data/eval_arcs.bin}"
OUTDIR="${OUTDIR:-results}"
MAX_RANGE="${MAX_RANGE:-2400}"

mkdir -p data logs "$OUTDIR"

echo "input:    $GEOJSON"
echo "deadline: $(date -d "@$DEADLINE" 2>/dev/null || date -r "$DEADLINE") (in ${HOURS} h)"

echo "=== converting (login node; it is seconds of work) ==="
./.venv/bin/python python/prepare.py "$GEOJSON" "$DATA" --report data/prepare.txt

echo "=== building ==="
make portable

JID_PRE=$(DATA="$DATA" CACHE="$CACHE" MAX_RANGE="$MAX_RANGE" \
    sbatch --parsable slurm/precompute.sbatch)
echo "precompute: $JID_PRE"

JID_SOLVE=$(DATA="$DATA" CACHE="$CACHE" OUTDIR="$OUTDIR" DEADLINE="$DEADLINE" \
    TIME_BUDGET=86400 \
    sbatch --parsable --dependency=afterok:"$JID_PRE" slurm/solve.sbatch)
echo "solve:      $JID_SOLVE"

# afterany, not afterok: if one sub-problem's task dies, the other eight still have
# checkpointed answers and a submission with eight good blocks beats no submission.
JID_FIN=$(DATA="$DATA" GEOJSON="$GEOJSON" OUTDIR="$OUTDIR" \
    sbatch --parsable --dependency=afterany:"$JID_SOLVE" slurm/finish.sbatch)
echo "finish:     $JID_FIN"

echo
echo "watch with:  squeue -u $USER"
echo "submission will be at ${OUTDIR}/submission.txt"
