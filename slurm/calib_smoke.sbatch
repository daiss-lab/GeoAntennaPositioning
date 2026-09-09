#!/bin/bash
# Does the measured margin land where the fixed-margin sweep says it should?
#
# What this has to show is that `--slot-margin -1` comes out near zero at tau = 0.25 and
# tau = 0.75, where the slot model and the exact evaluator already agree, and comes out
# clearly positive at tau = 0.5, where they do not.  A calibration that reports the same
# number everywhere is measuring nothing.
#
#   sbatch slurm/calib_smoke.sbatch
#
#SBATCH --job-name=gis4-csmoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=96G
#SBATCH --time=00:28:00
#SBATCH --output=logs/csmoke-%j.out
#SBATCH --error=logs/csmoke-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
OUTDIR=results/csmoke
mkdir -p "$OUTDIR"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

echo "host=$(hostname)"
for SPEC in "0.25 500" "0.5 500" "0.5 1000" "0.75 500"; do
    set -- $SPEC
    TAU=$1; K=$2
    echo "================ tau=$TAU k=$K, margin measured ================"
    "$BIN" solve --data data/sample.bin --cache data/range_2400.bin \
        --tau "$TAU" --k "$K" --time 120 --sync 60 --replicas 32 --seed 11 \
        --slot-margin -1 --out "$OUTDIR/auto_tau${TAU}_k${K}.txt" 2>&1 |
        grep -E "calibration:|slot margin|aims at|RESULT"
done
