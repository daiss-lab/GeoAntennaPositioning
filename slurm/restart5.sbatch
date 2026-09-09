#!/bin/bash
# restart5.sbatch -- sequential restarts for the cells that converge long before the window
# closes.  Submitted by hand on competition day; see the reasoning below.
#
# pipeline5 gives every (cell, seed) one search running to an absolute deadline, which is
# right for the cells that are still climbing at the end of the window.  On the competition
# parameters it is wrong for six of the nine: k = 9 and k = 49 are small enough that a
# 32-replica search stalls after about half an hour, and the measured seed spread says the
# stall is a local optimum rather than the answer --
#
#     tau=0.68 k=9 : 227 239 228 233   (5.3% between the best and worst seed)
#     tau=0.68 k=49: 1126 1121 1139 1132
#     tau=0.49 k=49: 2475 2491 2510 2477
#     tau=0.32 k=9 : 1345 1345 1345 1345   (converged, and every seed agrees)
#
# A cell whose seeds disagree by 5% is short of restarts, not short of time.  So each task
# here runs a fresh search every RESTART seconds with a new seed until the same absolute
# deadline, and keeps its own best.  Twenty-six restarts in the window instead of one.
#
# Files are written as s1<task>_tau<tau>_k<k>.txt, which the s* glob in polish5.sbatch and
# finalize5.sbatch already picks up, so nothing downstream needs to know this ran.  Each
# restart writes to a scratch file and only replaces the task's answer if it is strictly
# better, judged by the emitted claim line -- the same quantity finalize ranks on, never a
# solver's opinion of its own run.

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

# Weighted by the seed spread measured above: the cells whose seeds disagree most get the
# most independent restarts, and the two that every seed already agrees on get the fewest.
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
    # Don't start a restart that cannot finish: a truncated search is worse than the time
    # handed back, and the last partial one would just be thrown away anyway.
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
