#!/bin/bash
#SBATCH --job-name=giscup-recheck
#SBATCH --partition=batch
#SBATCH --array=0-2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=logs/recheck-%A_%a.out
#SBATCH --error=logs/recheck-%A_%a.err
set -uo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
FILES=(../Solution01/results/tau0.25_k50.txt ../Solution01/results/tau0.5_k1000.txt ../Solution01/results/tau0.75_k1000.txt)
F="${FILES[${SLURM_ARRAY_TASK_ID:-0}]}"

echo "=== $F : fixed verifier (uncapped, antennas pushed off their host wall) ==="
./.venv/bin/python python/verify.py --geojson ../dataset/GIS-cup-sample-dataset.geojson \
    --solution "$F" --step 0.25 --sample-buildings 100 --seed 1
echo "verify.py exit status: $?"

echo
echo "=== $F : C++ score (independent of the sampler above) ==="
./bin/gis_cup score --data ../Solution01/data/sample.bin --solution "$F"
