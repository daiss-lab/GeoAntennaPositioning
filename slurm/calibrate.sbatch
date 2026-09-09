#!/bin/bash
# Calibration sweep: how faithfully does the slot discretisation track the exact score?
#
# The optimiser ranks candidates using slot counts, so a slot rule that drifts from the
# exact coverage makes it optimise the wrong thing.  This compares the greedy slot-score
# against the exact score for each rule/resolution.
#
#SBATCH --job-name=giscup-cal
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=192G
#SBATCH --time=02:00:00
#SBATCH --output=logs/calibrate-%j.out
#SBATCH --error=logs/calibrate-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-64}"
DATA=data/sample.bin

for cfg in "conservative 0.1" "midpoint 0.1" "conservative 0.05"; do
    set -- $cfg
    RULE=$1; SLOT=$2
    TAG="${RULE}_${SLOT}"
    CACHE="data/cal_${TAG}.bin"
    echo "############ rule=${RULE} slot=${SLOT} ############"
    ./bin/gis_cup precompute --data "$DATA" --out "$CACHE" \
        --slot-rule "$RULE" --slot-len "$SLOT" 2>/dev/null

    for combo in "0.25 500" "0.5 50" "0.5 500" "0.75 500"; do
        set -- $combo
        TAU=$1; K=$2
        echo "--- tau=$TAU k=$K ---"
        ./bin/gis_cup solve --data "$DATA" --cache "$CACHE" \
            --tau "$TAU" --k "$K" --time 0 --slot-len "$SLOT" 2>&1 \
            | grep -E "greedy|RESULT|excluded" || true
    done
    rm -f "$CACHE"
done
