#!/bin/bash
#SBATCH --job-name=giscup-smoke
#SBATCH --partition=debug
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=64G
#SBATCH --time=00:25:00
#SBATCH --output=logs/smoke-%j.out
#SBATCH --error=logs/smoke-%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
export OMP_NUM_THREADS=16 OMP_PROC_BIND=spread OMP_PLACES=cores
mkdir -p results/smoke
for SPEC in "0.25 50" "0.5 500" "0.75 500"; do
  set -- $SPEC
  echo "======== tau=$1 k=$2 ========"
  ./bin/gis_cup solve --data data/sample.bin --cache data/sample_arcs.bin \
      --tau "$1" --k "$2" --time 60 --replicas 16 --sync 30 \
      --out "results/smoke/tau$1_k$2.txt"
  ./bin/gis_cup score --data data/sample.bin --solution "results/smoke/tau$1_k$2.txt"
done
