#!/bin/bash

#SBATCH --job-name=gis5-restart
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=24:00:00
#SBATCH --output=logs/p5-rst-%A_%a.out
#SBATCH --error=logs/p5-rst-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${CACHE:?}" "${BIN:?}" "${RUN:?}" "${SOLVE_END:?}"
RESTART="${RESTART:-1800}"

TARGETS=(
    "0.68 9"   "0.68 9"   "0.68 9"   "0.68 9"   "0.68 9"   "0.68 9"
    "0.68 49"  "0.68 49"  "0.68 49"  "0.68 49"  "0.68 49"
    "0.49 49"  "0.49 49"  "0.49 49"  "0.49 49"  "0.49 49"
    "0.32 49"  "0.32 49"  "0.32 49"  "0.32 49"
    "0.49 9"   "0.49 9"
    "0.32 9"   "0.32 9"
)

I="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$I" -ge "${#TARGETS[@]}" ]; then
    echo "task $I is past the ${#TARGETS[@]} targets; nothing to do"
    exit 0
fi
read -r TAU K <<< "${TARGETS[$I]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

OUT="${RUN}/s1${I}_tau${TAU}_k${K}.txt"
TMP="${RUN}/.tmp_s1${I}_tau${TAU}_k${K}.txt"
mkdir -p "$RUN"

claims() { awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$1" 2>/dev/null; }

echo "host=$(hostname) tau=${TAU} k=${K} restarts of ${RESTART}s until $(date -d @${SOLVE_END})"

BEST=-1
N=0
while [ "$(date +%s)" -lt "$SOLVE_END" ]; do
    LEFT=$(( SOLVE_END - $(date +%s) ))
    [ "$LEFT" -lt 300 ] && break
    BUDGET=$(( LEFT < RESTART ? LEFT : RESTART ))
    SEED=$(( 7919 + I * 104729 + N * 7907 ))

    "$BIN" solve --data "$DATA" --cache "$CACHE" --tau "$TAU" --k "$K" \
        --slot-len 0.05 \
        --time "$BUDGET" --deadline "$SOLVE_END" \
        --replicas 32 --seed "$SEED" --sync 120 \
        --shortlist 24 --exact-lead 0 \
        --slot-margin 0 --target-destroy 0 --difficulty-beta 0 \
        --out "$TMP" || { echo "restart $N (seed $SEED) failed; continuing"; N=$(( N + 1 )); continue; }

    C=$(claims "$TMP")
    C="${C:--1}"
    if [ "$C" -gt "$BEST" ]; then
        mv -f "$TMP" "$OUT"
        BEST="$C"
        echo "restart $N (seed $SEED): ${C} buildings -- new best for this task"
    else
        echo "restart $N (seed $SEED): ${C} buildings (task best ${BEST})"
    fi
    N=$(( N + 1 ))
done

rm -f "$TMP"
echo "tau=${TAU} k=${K}: ${N} restarts, best ${BEST}"
[ -e "$OUT" ] && "$BIN" score --data "$DATA" --solution "$OUT"
exit 0
