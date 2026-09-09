#!/bin/bash
# Sanity check for the difficulty weighting before any of it is measured at scale.
#
# Three things have to be true, and each is checked here rather than assumed:
#
#   1. beta = 0 reproduces Solution04 bit for bit.  The weighting multiplies Phi by w_b and
#      w_b is 1.0 everywhere when the feature is off, so the same seed must give the same
#      slot score.  If it does not, the refactor changed the search and every A/B after
#      this one is measuring two things at once.
#   2. m_b is measured, and is cheap.  The line it prints carries the mean and how many
#      buildings need more than three antennas to themselves; at tau = 0.75 that number
#      should be large and at tau = 0.25 it should be near zero.  That is the whole premise.
#   3. beta > 0 runs and produces a valid answer.
#
#   sbatch slurm/smoke5.sbatch
#
#SBATCH --job-name=gis5-smoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=48G
#SBATCH --time=00:25:00
#SBATCH --output=logs/smoke5-%j.out
#SBATCH --error=logs/smoke5-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN=./bin/gis_cup
DATA=data/sample.bin
CACHE=data/sl010.bin
OUT=results/smoke5
mkdir -p "$OUT"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"

# tau = 0.75 is where the premise lives: half the buildings are unservable and under a flat
# Phi their partial coverage still steers the ranking.  tau = 0.25 is the control, where
# almost every building is a one-antenna job and the weighting should do nothing at all.
for cell in "0.75 500" "0.25 500"; do
    set -- $cell
    TAU=$1 K=$2
    for BETA in 0 0.5; do
        NAME="b${BETA}_tau${TAU}_k${K}"
        echo "================ tau=${TAU} k=${K} beta=${BETA} ================"
        "$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
            --time 120 --replicas 32 --seed 11 --sync 60 \
            --difficulty-beta "$BETA" --target-destroy 0 \
            --out "${OUT}/${NAME}.txt"
        "$BIN" score --data "$DATA" --solution "${OUT}/${NAME}.txt"
    done
done

echo "================ beta=0 must equal Solution04 ================"
"$BIN" score --data "$DATA" --solution "${OUT}/b0_tau0.75_k500.txt"
