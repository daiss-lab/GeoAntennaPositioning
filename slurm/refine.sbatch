#!/bin/bash
# Does continuous refinement buy anything over Solution02's 4 m candidate grid?
#
# The baseline is Solution02's bench run (job 993563, 16 replicas x 600 s): its `new_far`
# answers, which were the best of the four arms in eight of nine cells.  Refining the
# *winner* is the honest test -- refining a weak answer would measure how much slack the
# baseline left, not what the continuum is worth.
#
# Both the input and the output are re-scored here by `score`, the same third-party reader
# used throughout, so the before/after pair comes from one evaluator and neither solver
# marks its own work.  The refined answer is written to a separate file; the baseline in
# ../Solution02 is never touched.
#
#   sbatch slurm/refine.sbatch
#
#SBATCH --job-name=giscup-refine
#SBATCH --partition=batch
#SBATCH --array=0-8
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=08:00:00
#SBATCH --output=logs/refine-%A_%a.out
#SBATCH --error=logs/refine-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
BASE="${BASE:-../Solution02/results/bench}"
ARM="${ARM:-new_far}"
OUTDIR="${OUTDIR:-results/refine}"
ROUNDS="${ROUNDS:-4}"
BUDGET="${BUDGET:-14400}"
TRIALS="${TRIALS:-48}"
EVERTS="${EVERTS:-28}"
ERADIUS="${ERADIUS:-220}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
TAU="${TAUS[$I]}"
K="${KS[$I]}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
IN="${BASE}/${ARM}_tau${TAU}_k${K}.txt"
OUT="${OUTDIR}/${ARM}_tau${TAU}_k${K}.txt"

echo "host=$(hostname) tau=$TAU k=$K  rounds=$ROUNDS trials=$TRIALS everts=$EVERTS budget=${BUDGET}s  arm=$ARM"
echo "input: $IN"

echo "======== BEFORE ========"
./bin/gis_cup score --data "$DATA" --solution "$IN"

echo "======== REFINE ========"
./bin/gis_cup refine --data "$DATA" --solution "$IN" --out "$OUT" \
    --rounds "$ROUNDS" --time "$BUDGET" \
    --trials "$TRIALS" --event-verts "$EVERTS" --event-radius "$ERADIUS"

echo "======== AFTER ========"
./bin/gis_cup score --data "$DATA" --solution "$OUT"
