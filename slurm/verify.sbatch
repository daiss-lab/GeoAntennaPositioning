#!/bin/bash
# Independent verification of a solution file, by sampling rather than by the solver's
# own analytic method.
#
#   SOLUTION=results/tau0.5_k500.txt sbatch slurm/verify.sbatch
#
#SBATCH --job-name=giscup-ver
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=32G
#SBATCH --time=04:00:00
#SBATCH --output=logs/verify-%j.out
#SBATCH --error=logs/verify-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

GEOJSON="${GEOJSON:-../dataset/GIS-cup-sample-dataset.geojson}"
SOLUTION="${SOLUTION:?set SOLUTION=results/....txt}"
STEP="${STEP:-0.25}"
NBLD="${NBLD:-400}"

./.venv/bin/python python/verify.py \
    --geojson "${GEOJSON}" \
    --solution "${SOLUTION}" \
    --step "${STEP}" \
    --sample-buildings "${NBLD}"
