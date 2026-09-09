#!/bin/bash
# Re-runs the verifier over the three Solution01 answers that reported FAIL, with both
# verifier defects fixed: the 1200 m antenna cap (see explanation.md §1) and the antenna
# endpoint landing inside its own host building (verify.py docstring point 3).
#
# reverify.sbatch established the first fix on tau=0.25 k=50 only; the other two answers
# still failed after it, and the antenna-interior defect is why.
#
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

# The C++ evaluator's own reading of the same file, for a side-by-side.  These two share
# no code, so agreement here is the evidence that matters.
echo
echo "=== $F : C++ score (independent of the sampler above) ==="
./bin/gis_cup score --data ../Solution01/data/sample.bin --solution "$F"
