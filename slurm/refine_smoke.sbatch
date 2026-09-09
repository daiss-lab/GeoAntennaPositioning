#!/bin/bash
# One small refinement, to prove the module runs at all before spending a full array on it.
#
# tau=0.75 k=50 is the cheapest cell in the grid: fewest antennas, and the baseline answer
# from Solution02's bench run (job 993563) is the one with the most headroom left, so if
# refinement can move anything it should move something here.
#
#   sbatch slurm/refine_smoke.sbatch
#
#SBATCH --job-name=giscup-refsmoke
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=96G
#SBATCH --time=01:00:00
#SBATCH --output=logs/refsmoke-%j.out
#SBATCH --error=logs/refsmoke-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
BASE="${BASE:-../Solution02/results/bench}"
OUTDIR="${OUTDIR:-results/smoke}"
TAU=0.75
K=50

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-16}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
IN="${BASE}/new_far_tau${TAU}_k${K}.txt"
OUT="${OUTDIR}/refined_tau${TAU}_k${K}.txt"

echo "host=$(hostname) tau=$TAU k=$K  SMOKE"
echo "input: $IN"

echo "======== baseline, re-scored ========"
./bin/gis_cup score --data "$DATA" --solution "$IN"

echo "======== refine (1 round, 600 s cap) ========"
time ./bin/gis_cup refine --data "$DATA" --solution "$IN" --out "$OUT" \
    --rounds 1 --time 600

echo "======== refined, re-scored by the same evaluator ========"
./bin/gis_cup score --data "$DATA" --solution "$OUT"
