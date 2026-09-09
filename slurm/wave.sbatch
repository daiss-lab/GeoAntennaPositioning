#!/bin/bash
# One wave of the island search: ISLANDS jobs on each of the nine sub-problems, every
# process publishing its leader into a per-cell share directory and adopting the best it
# finds there.  Submitted by pipeline4.sh, which sets the array size and the deadline.
#
# The share directory is deliberately *not* cleared between waves.  A wave-2 island reads
# it at its first synchronisation point and adopts everything wave 1 found, so restarting
# costs a greedy construction (seconds) rather than the wave.
#
#SBATCH --job-name=gis4-wave
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --mem=256G
#SBATCH --time=24:00:00
#SBATCH --output=logs/wave-%A_%a.out
#SBATCH --error=logs/wave-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

BIN="${BIN:-./bin/gis_cup}"
DATA="${DATA:-data/eval.bin}"
CACHE="${CACHE:-data/eval_arcs.bin}"
ISLANDS="${ISLANDS:-6}"
DEADLINE="${DEADLINE:-0}"
OUTDIR="${OUTDIR:-results/run}"
SHAREROOT="${SHAREROOT:-${OUTDIR}/share}"
WAVE="${WAVE:-1}"
REP="${REP:-${SLURM_CPUS_PER_TASK:-128}}"
SYNC="${SYNC:-120}"

# The nine sub-problems.  If the organisers publish different values these two arrays are
# the only place that changes -- and slurm/robust.sbatch exists to have already measured
# that the solver holds up across a much wider grid than this one.
TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
KS=(50 500 1000 50 500 1000 50 500 1000)

I="${SLURM_ARRAY_TASK_ID:-0}"
CELL=$(( I / ISLANDS ))
ISL=$(( I % ISLANDS ))
TAU="${TAUS[$CELL]}"
K="${KS[$CELL]}"

export OMP_NUM_THREADS="${REP}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

SHARE_DIR="${SHAREROOT}/tau${TAU}_k${K}"
mkdir -p "$SHARE_DIR" "$OUTDIR"
OUT="${OUTDIR}/w${WAVE}_tau${TAU}_k${K}_isl${ISL}.txt"

echo "host=$(hostname) wave=${WAVE} cell=${CELL} island=${ISL} tau=${TAU} k=${K} deadline=${DEADLINE}"
date -u +"start %Y-%m-%dT%H:%M:%SZ"

# --time is a ceiling the deadline overrides whenever it is nearer, so a wave that starts
# late shortens itself instead of running past the window.
"$BIN" solve \
    --data "$DATA" --cache "$CACHE" \
    --tau "$TAU" --k "$K" \
    --time 86400 --deadline "$DEADLINE" \
    --sync "$SYNC" --replicas "$REP" \
    --seed $(( 1 + CELL * 7919 + ISL * 104729 + WAVE * 15485863 )) \
    --slot-margin -1 \
    --share-dir "$SHARE_DIR" --island $(( WAVE * 1000 + ISL )) \
    --out "$OUT"

date -u +"solved %Y-%m-%dT%H:%M:%SZ"
"$BIN" score --data "$DATA" --solution "$OUT"
date -u +"done %Y-%m-%dT%H:%M:%SZ"
