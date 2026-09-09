#!/bin/bash
# Where is the reachable headroom?
#
# An upper bound on the optimum would answer this, but every cheap bound available here is
# loose enough to be uninformative -- the submodular guarantee is on Phi, and Phi counts
# partial progress, so it sits far above the score it is supposed to bound.
#
# This measures the same thing from below and exactly.  For each of the nine incumbent
# answers, dump every building's true coverage ratio and count how many sit just short of
# tau.  A building at 0.97*tau is one small move from a point; a building at 0.3*tau is
# not.  The size of that near-miss population is the honest ceiling on what any amount of
# extra local search can buy, and its shape says which cells deserve the compute.
#
#   sbatch slurm/nearmiss.sbatch
#
#SBATCH --job-name=gis4-near
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=00:40:00
#SBATCH --output=logs/nearmiss-%j.out
#SBATCH --error=logs/nearmiss-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
SRC="${SRC:-../Solution03/results/refine_hard}"
OUTDIR=results/cov
mkdir -p "$OUTDIR"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) source=${SRC}"
for f in "$SRC"/*.txt; do
    base=$(basename "$f" .txt)
    echo "---- $base"
    "$BIN" score --data data/sample.bin --solution "$f" \
        --dump-cov "$OUTDIR/${base}.cov" 2>&1 | grep -E "re-scored|claim line"
done
ls -la "$OUTDIR"
