#!/bin/bash
# Three questions in one small array, on two cells, with the arm varying fastest so a
# partly-finished run is still readable:
#
#   A  1 job,  32 replicas,  600 s              reference
#   B  1 job,  32 replicas, 2400 s              4x the wall clock, same cores
#   C  4 jobs, 32 replicas,  600 s, sharing     4x the cores as one search
#   D  4 jobs, 32 replicas,  600 s, no sharing  4x the cores as four searches
#
#   B vs A  does more time still buy score, or has the search converged?
#   C vs A  does the island model convert cores into score?
#   C vs D  does migration beat simply taking the best of the same four runs?
#
# C against D is the comparison that matters, and it is deliberately at equal cost.  Four
# independent runs and best-of-four is what anyone gets for free by launching four jobs;
# the island model only earns its complexity if it beats that.
#
#   sbatch --array=0-19%20 slurm/islands_ab.sbatch
#
#SBATCH --job-name=gis4-isab
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=01:30:00
#SBATCH --output=logs/isab-%A_%a.out
#SBATCH --error=logs/isab-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
OUTDIR="${OUTDIR:-results/isab}"
REP=32

# Two cells: one where the tau = 0.5 near-miss population is large, one where it is not.
CELL_TAU=(0.5 0.75)
CELL_K=(1000 500)

# Ten slots per cell: A, B, C0..C3, D0..D3.
I="${SLURM_ARRAY_TASK_ID:-0}"
SLOT=$(( I % 10 ))
CI=$(( I / 10 ))
TAU="${CELL_TAU[$CI]}"
K="${CELL_K[$CI]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
SHARE_DIR="${OUTDIR}/share_tau${TAU}_k${K}"

ARGS=(--data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K"
      --sync 60 --replicas "$REP" --target-destroy 0)

case "$SLOT" in
    0) ARM=A; BUDGET=600;  SEED=11; OUT="${OUTDIR}/A_tau${TAU}_k${K}.txt" ;;
    1) ARM=B; BUDGET=2400; SEED=11; OUT="${OUTDIR}/B_tau${TAU}_k${K}.txt" ;;
    2|3|4|5)
       ARM=C; BUDGET=600; ISL=$(( SLOT - 2 )); SEED=$(( 11 + ISL * 104729 ))
       OUT="${OUTDIR}/C${ISL}_tau${TAU}_k${K}.txt"
       mkdir -p "$SHARE_DIR"
       ARGS+=(--share-dir "$SHARE_DIR" --island "$ISL" --immigrants 2) ;;
    6|7|8|9)
       ARM=D; BUDGET=600; ISL=$(( SLOT - 6 )); SEED=$(( 11 + ISL * 104729 ))
       OUT="${OUTDIR}/D${ISL}_tau${TAU}_k${K}.txt" ;;
esac

echo "host=$(hostname) arm=${ARM} tau=${TAU} k=${K} budget=${BUDGET}s seed=${SEED}"

"$BIN" solve "${ARGS[@]}" --time "$BUDGET" --seed "$SEED" --out "$OUT"
"$BIN" score --data "$DATA" --solution "$OUT"
