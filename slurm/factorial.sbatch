#!/bin/bash
# Two changes, measured together as a 2x3 factorial with paired seeds.
#
#   target-destroy  0|1            a fourth LNS destroy shape that picks a building sitting
#                                  just below tau and points the repair at it
#   slot-margin     0|.01|.02|.04  ask the search for tau*(1+m), to offset the slot model's
#                                  optimism
#
# The margin arm is aimed at a specific measurement.  Reading Solution02's bench logs, the
# slot score and the exact score disagree almost entirely at tau = 0.5:
#
#     tau=0.25  k=50/500/1000   slot 2452/10509/12860 -> exact 2457/10574/12853
#     tau=0.50  k=50/500/1000   slot  944/ 6418/10486 -> exact  837/ 6100/10151
#     tau=0.75  k=50/500/1000   slot  397/ 3116/  ... -> exact  397/ 3115/  ...
#
# At tau = 0.25 the proxy understates, at tau = 0.75 it is exact to a building, and at
# tau = 0.5 it overstates by 107, 318 and 335 -- up to 12.8%.  The coverage dumps agree
# from the other side: the tau = 0.5 answers leave 153, 525 and 446 buildings sitting in
# [0.98*tau, tau), a near-miss population an order of magnitude larger than any other
# cell's.  The search believes it finished those buildings and it did not.  A margin makes
# it keep pushing until they are genuinely over.
#
# Paired is the point.  Solution02 measured a 1.1% spread between seeds on one cell, which
# is larger than the target-destroy effect is likely to be, so an unpaired comparison would
# be reading noise.  Every configuration runs the same three seeds on the same nine cells,
# so the comparison is within-seed and the seed spread cancels.
#
#   sbatch slurm/factorial.sbatch
#
#SBATCH --job-name=gis4-fact
#SBATCH --partition=batch
#SBATCH --array=0-215%24
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=96G
#SBATCH --time=01:30:00
#SBATCH --output=logs/fact-%A_%a.out
#SBATCH --error=logs/fact-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
CACHE="${CACHE:-data/range_2400.bin}"
BIN="${BIN:-./bin/gis_cup}"
OUTDIR="${OUTDIR:-results/fact}"
BUDGET="${BUDGET:-900}"
REP="${REP:-64}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)
SEEDS=(11 22 33)
# cfg = target * 4 + margin_index
CFG_TARGET=(0 0 0 0 1 1 1 1)
CFG_MARGIN=(0 0.01 0.02 0.04 0 0.01 0.02 0.04)

I="${SLURM_ARRAY_TASK_ID:-0}"
CFG=$(( I / 27 ))
REST=$(( I % 27 ))
SEEDI=$(( REST / 9 ))
CELL=$(( REST % 9 ))

TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"
SEED="${SEEDS[$SEEDI]}"
TGT="${CFG_TARGET[$CFG]}"
MGN="${CFG_MARGIN[$CFG]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

mkdir -p "$OUTDIR"
OUT="${OUTDIR}/t${TGT}_m${MGN}_s${SEED}_tau${TAU}_k${K}.txt"

echo "host=$(hostname) cfg=${CFG} target=${TGT} margin=${MGN} seed=${SEED} tau=${TAU} k=${K}"

"$BIN" solve \
    --data "$DATA" --cache "$CACHE" \
    --tau "$TAU" --k "$K" \
    --time "$BUDGET" --sync 90 --replicas "$REP" \
    --seed "$SEED" \
    --target-destroy "$TGT" \
    --slot-margin "$MGN" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"
