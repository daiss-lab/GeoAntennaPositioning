#!/bin/bash
# Does letting an antenna leave the edge `solve` put it on buy anything?
#
# Solution03 built a within-edge continuous ascent, measured it at +43 buildings (+0.08%),
# and ended by naming its own binding constraint: the ascent reaches a zero-move round after
# two sweeps, five times the within-edge sampling density buys only ten more buildings, and
# "the remaining headroom is therefore almost entirely across-edge, which is exactly what
# this module cannot touch".  Solution04 read that, agreed, and spent its budget elsewhere.
#
# This is that experiment.  Each antenna is additionally offered the start vertices of the
# nearest `relocate-edges` edges within `relocate-radius` metres of where it stands, so it
# can hop to a different edge -- or a different building -- instead of only sliding.
#
# Why this design gives a clean answer with no noise at all: refinement is deterministic and
# its acceptance rule initialises best_dscore to 0, so a move that loses buildings can never
# be taken.  Both arms start from the *same* answer file.  Any difference is therefore the
# operator, not a seed, and it can only be non-negative.
#
#   arm 0   relocate-edges 0    Solution03's within-edge ascent, unchanged
#   arm 1   relocate-edges 48   plus the across-edge move
#
# Refining the incumbent is the honest test: refining a weak answer would measure the
# baseline's slack rather than the operator.  The input is Solution04's best complete
# nine-cell arm (fact/t0_m0_s11, 53,078 by the independent scorer).
#
#   sbatch --array=0-17%18 slurm/relocate.sbatch
#
#SBATCH --job-name=gis5-reloc
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=02:00:00
#SBATCH --output=logs/reloc-%A_%a.out
#SBATCH --error=logs/reloc-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
SRC=../Solution04/results/fact
OUT=results/reloc
mkdir -p "$OUT"

TAUS=(0.25 0.25 0.25 0.5 0.5  0.5 0.75 0.75 0.75)
KS=(  50   500  1000 50  500  1000 50  500  1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 2 ))   # arm varies fastest
C=$(( I / 2 ))
TAU="${TAUS[$C]}"; K="${KS[$C]}"

case "$A" in
    0) REL=0  ;;
    1) REL=48 ;;
esac

IN="${SRC}/t0_m0_s11_tau${TAU}_k${K}.txt"
NAME="r${REL}_tau${TAU}_k${K}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${A} relocate=${REL} tau=${TAU} k=${K} in=${IN}"

echo "---- input, re-scored here so both arms are measured against one baseline ----"
"$BIN" score --data "$DATA" --solution "$IN"

"$BIN" refine --data "$DATA" --solution "$IN" \
    --rounds 6 --trials 48 --event-verts 28 --event-radius 220 \
    --relocate-edges "$REL" --relocate-radius 120 \
    --time 5400 \
    --out "${OUT}/${NAME}.txt"

echo "---- refined ----"
"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
