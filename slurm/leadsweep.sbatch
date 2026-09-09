#!/bin/bash
# The search ranks placements by the slot model.  The slot model is wrong about which
# placement is best, and it is wrong about half the time.
#
# Measured over the 291 shortlists printed in the Solution02-04 logs:
#
#   best slot score is also the best exact score   53% of the time
#   buildings gained by checking the shortlist     mean 2.3, p90 7, max 46
#   rank of the true winner within the shortlist   0:155  1:46  2:31  3:16  4:11  5:14  6:11  7:7
#
# Two separate defects hide in that, and this array separates them:
#
#   * The shortlist is truncated at 8 and the rank distribution has not decayed at 7 --
#     11, 14, 11, 7 at ranks 4-7.  Winners are being cut off by the cap.  Arm 1 raises it.
#   * Worse, the *search itself* steers by the same broken signal.  Every synchronisation
#     point copies "the leader" into the trailing replicas, chosen by slot score -- so on
#     roughly half of them the search propagates a solution that is not the best one it
#     holds and overwrites one that was.  Arms 2 and 3 pick the leader by exact score.
#
#   arm 0   shortlist 8,  exact-lead 0    Solution04 exactly
#   arm 1   shortlist 24, exact-lead 0    the cap alone
#   arm 2   shortlist 24, exact-lead 6    the cap plus honest leader selection
#   arm 3   shortlist 24, exact-lead 12   twice as much of it, to see the cost curve
#
# 6 cells x 2 seeds, paired, arm varying fastest with the index.
#
#   sbatch --array=0-47%24 slurm/leadsweep.sbatch
#
#SBATCH --job-name=gis5-lead
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=01:00:00
#SBATCH --output=logs/lead-%A_%a.out
#SBATCH --error=logs/lead-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
CACHE=data/sl010.bin
OUT=results/lead
mkdir -p "$OUT"

SHORT=(8 24 24 24)
LEAD=( 0  0  6 12)
TAUS=(0.5  0.5  0.75 0.75 0.75 0.25)
KS=(  500  1000 50   500  1000 500)
SEEDS=(11 22)

I="${SLURM_ARRAY_TASK_ID:-0}"
A=$(( I % 4 ));  R=$(( I / 4 ))
C=$(( R % 6 ));  S=$(( R / 6 ))

NAME="a${A}_s${SEEDS[$S]}_tau${TAUS[$C]}_k${KS[$C]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname) arm=${A} shortlist=${SHORT[$A]} exact-lead=${LEAD[$A]} \
tau=${TAUS[$C]} k=${KS[$C]} seed=${SEEDS[$S]}"

"$BIN" solve --data "$DATA" --cache "$CACHE" --tau "${TAUS[$C]}" --k "${KS[$C]}" \
    --time 900 --replicas 32 --seed "${SEEDS[$S]}" --sync 60 \
    --shortlist "${SHORT[$A]}" --exact-lead "${LEAD[$A]}" \
    --target-destroy 0 --difficulty-beta 0 \
    --out "${OUT}/${NAME}.txt"

"$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
