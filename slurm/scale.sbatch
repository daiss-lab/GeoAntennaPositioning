#!/bin/bash
# How much score does more compute actually buy?
#
# Every Solution02/03 number was produced by 16 replicas for 600 s -- 2.7 core-hours per
# sub-problem.  On submission day the cluster can supply four to five orders of magnitude
# more than that.  Before building anything, the shape of the curve has to be known: if
# score keeps climbing with cores and seconds, the highest-value work is a search that can
# use the whole machine; if it flattens at 128 cores, the work is a better algorithm.
#
# Five configurations isolate cores from seconds:
#
#   cfg 0   16 replicas,   600 s     the historical baseline
#   cfg 1  128 replicas,   600 s     8x the cores, same wall clock
#   cfg 2  128 replicas,  2400 s
#   cfg 3  128 replicas,  9600 s     16x the wall clock of the baseline
#   cfg 4   16 replicas,  9600 s     time without cores, to separate the two axes
#
#   sbatch slurm/scale.sbatch
#
#SBATCH --job-name=gis4-scale
#SBATCH --partition=batch
#SBATCH --array=0-44%16
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=192G
#SBATCH --time=05:00:00
#SBATCH --output=logs/scale-%A_%a.out
#SBATCH --error=logs/scale-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
OUTDIR="${OUTDIR:-results/scale}"
SEED_BASE="${SEED_BASE:-11}"
BIN="${BIN:-./bin/gis_cup}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

CFG_REP=(16 128 128 128 16)
CFG_TIME=(600 600 2400 9600 9600)

I="${SLURM_ARRAY_TASK_ID:-0}"
CFG=$(( I / 9 ))
CELL=$(( I % 9 ))

TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"
REP="${CFG_REP[$CFG]}"
BUDGET="${CFG_TIME[$CFG]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
OUT="${OUTDIR}/cfg${CFG}_tau${TAU}_k${K}.txt"

echo "host=$(hostname) cfg=${CFG} cell=${CELL} tau=${TAU} k=${K} replicas=${REP} budget=${BUDGET}s"
date -u +"start %Y-%m-%dT%H:%M:%SZ"

"$BIN" solve \
    --data "$DATA" \
    --cache "$CACHE" \
    --tau "$TAU" \
    --k "$K" \
    --time "$BUDGET" \
    --sync 90 \
    --replicas "$REP" \
    --seed $(( SEED_BASE + CELL * 7919 )) \
    --out "$OUT"

date -u +"solved %Y-%m-%dT%H:%M:%SZ"

# Score the emitted file the way a third party would, so no solver marks its own work.
"$BIN" score --data "$DATA" --solution "$OUT"
date -u +"done %Y-%m-%dT%H:%M:%SZ"
