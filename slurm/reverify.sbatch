#!/bin/bash
#SBATCH --job-name=giscup-rever
#SBATCH --partition=batch
#SBATCH --array=0-2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=logs/reverify-%A_%a.out
#SBATCH --error=logs/reverify-%A_%a.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
FILES=(../Solution01/results/tau0.25_k50.txt ../Solution01/results/tau0.5_k1000.txt ../Solution01/results/tau0.75_k1000.txt)
F="${FILES[${SLURM_ARRAY_TASK_ID:-0}]}"
echo "=== $F : old behaviour (antennas capped at 1200 m) ==="
./.venv/bin/python python/verify.py --geojson ../dataset/GIS-cup-sample-dataset.geojson \
    --solution "$F" --step 0.25 --sample-buildings 100 --seed 1 --antenna-radius 1200 || true
echo
echo "=== $F : fixed behaviour (all antennas) ==="
./.venv/bin/python python/verify.py --geojson ../dataset/GIS-cup-sample-dataset.geojson \
    --solution "$F" --step 0.25 --sample-buildings 100 --seed 1
