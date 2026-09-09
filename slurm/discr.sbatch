#!/bin/bash
# The discretisation study Solution04 built the caches for and never ran.
#
# Two knobs, both baked into the visibility cache at precompute time:
#
#   sl010  0.10 m slots, 4 m candidate spacing   the incumbent
#   sl005  0.05 m slots, 4 m spacing             finer coverage measurement
#   sl002  0.02 m slots, 4 m spacing             finer still; 42.9M slots
#   sp2    0.10 m slots, 2 m spacing             429,590 candidates instead of 217,457
#
# Why this is worth a node-hour rather than a footnote.  Solution04 §2 measured the slot
# model overstating coverage by up to 12.8%, entirely at tau = 0.5, and traced it to
# near-rectangles whose two visible walls come to 0.49999998 of the perimeter -- the
# midpoint rule rounds that up to "finished" and the search stops working on the building
# while the exact evaluator still rejects it.  That is a *discretisation* error, and the
# fix Solution04 reached for was a margin on the requirement, which its own sweep then
# measured as harmful at every setting.  A finer slot is the direct fix for the same
# defect: at 0.02 m a rectangle's two walls resolve to 514 of 1029 slots against a
# requirement of 515, and the search correctly sees an unfinished building.
#
# The caches already exist (job 994032, 15-26 s each), so this costs only the solves.
#
#   sbatch --array=0-15%16 slurm/discr.sbatch
#
#SBATCH --job-name=gis5-discr
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=01:00:00
#SBATCH --output=logs/discr-%A_%a.out
#SBATCH --error=logs/discr-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
OUT=results/discr
mkdir -p "$OUT"

ARMS=(sl010 sl005 sl002 sp2)
SLOTLEN=(0.1  0.05  0.02  0.1)
TAUS=(0.5  0.5  0.75 0.25)
KS=(  500  1000 500  500)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 4 ))
C=$(( I / 4 ))

ARM="${ARMS[$A]}"
SL="${SLOTLEN[$A]}"
TAU="${TAUS[$C]}"
K="${KS[$C]}"
NAME="${ARM}_tau${TAU}_k${K}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${ARM} slot-len=${SL} tau=${TAU} k=${K}"

# --slot-len has to match what the cache was built with; solve rebuilds the layout and
# refuses to run against a cache from a different discretisation, which is the check that
# makes this array safe to fan out.
"$BIN" solve --data "$DATA" --cache "data/${ARM}.bin" --slot-len "$SL" \
    --tau "$TAU" --k "$K" \
    --time 900 --replicas 32 --seed 11 --sync 60 \
    --target-destroy 0 \
    --out "${OUT}/${NAME}.txt"

# The comparison only means anything through the exact scorer: a finer slot changes the
# solver's *internal* score by construction, so the arms are not comparable on it.
"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
