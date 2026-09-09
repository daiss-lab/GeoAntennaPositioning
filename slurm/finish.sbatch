#!/bin/bash
#SBATCH --job-name=giscup-finish
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=128G
#SBATCH --time=08:00:00
#SBATCH --output=logs/finish-%j.out
#SBATCH --error=logs/finish-%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

DATA="${DATA:-data/sample.bin}"
GEOJSON="${GEOJSON:-../dataset/GIS-cup-sample-dataset.geojson}"
OUTDIR="${OUTDIR:-results}"
NBLD="${NBLD:-150}"
STEP="${STEP:-0.25}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-32}"

TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

fail=0
for I in "${!TAUS[@]}"; do
    TAU="${TAUS[$I]}"
    K="${KS[$I]}"
    F="${OUTDIR}/tau${TAU}_k${K}.txt"
    echo "################ tau=$TAU k=$K ################"
    if [ ! -s "$F" ]; then
        echo "  MISSING: $F"
        fail=1
        continue
    fi
    ./bin/gis_cup score --data "$DATA" --solution "$F" --rewrite "$F"
done

echo "################ independent verification ################"
for I in "${!TAUS[@]}"; do
    TAU="${TAUS[$I]}"
    K="${KS[$I]}"
    F="${OUTDIR}/tau${TAU}_k${K}.txt"
    [ -s "$F" ] || continue
    echo "--- tau=$TAU k=$K ---"
    ./.venv/bin/python python/verify.py --geojson "$GEOJSON" --solution "$F" \
        --step "$STEP" --sample-buildings "$NBLD" || fail=1
done

echo "################ assemble ################"
./.venv/bin/python python/assemble.py --dir "$OUTDIR" --out "${OUTDIR}/submission.txt" || fail=1

exit $fail
