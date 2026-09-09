#!/bin/bash
# refine484.sbatch -- run the across-edge refinement on the three k=484 cells, now.
#
# WHY THIS EXISTS
#
# polish5 has never actually run on the competition answers.  Both earlier polish arrays
# (1025774 and its predecessor) exited in two seconds against the SAMPLE cells, because
# they were submitted without CELLS_FILE and slurm/cells.sh reads unset as "development
# run".  results/run holds no polished_* file at all, so every antenna in the current
# submission still sits exactly where solve put it, on a candidate position from the
# discretised precompute -- and refine's whole job is to move it off that grid onto the
# tangency events and neighbouring-edge vertices that the exact evaluator prefers.
#
# The queued polish (1025785) will do this at 09:00 UTC, but it will do it to whichever
# answer is best AT THAT MOMENT.  If island484 has by then overtaken the incumbent, the
# incumbent's own refinement gain is never banked anywhere.  Running it now costs three
# tasks and makes the two lines of attack independent: finalize takes the maximum over
# both, so whichever wins, the other cannot cost anything.
#
# Refine is monotone by construction -- its acceptance rule initialises best_dscore to 0,
# so a move that loses buildings is never taken -- and it re-scores what it writes with the
# exact evaluator.  It cannot return an answer worse than the one it was given.
#
# Output is s9<TAG><cell>_tau<tau>_k<k>.txt, which the s* glob in polish5 and finalize5
# already ranks, so nothing downstream needs to know this ran.

#SBATCH --job-name=gis5-ref484
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=04:00:00
#SBATCH --output=logs/p5-r484-%A_%a.out
#SBATCH --error=logs/p5-r484-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${BIN:?}" "${RUN:?}" "${REFINE_END:?}"
TAG="${TAG:-0}"

CELLS=("0.32 484" "0.49 484" "0.68 484")

C="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$C" -ge "${#CELLS[@]}" ]; then
    echo "task $C is past the ${#CELLS[@]} cells; nothing to do"
    exit 0
fi
read -r TAU K <<< "${CELLS[$C]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

claims() { awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$1" 2>/dev/null; }

# Best answer for this cell right now, by the emitted claim line -- the same quantity
# finalize ranks on.  Excludes this job's own output tags so a re-run refines the current
# best rather than its own previous result.
BEST=""; BEST_N=-1
for f in "${RUN}"/s*_tau${TAU}_k${K}.txt "${RUN}"/polished_tau${TAU}_k${K}.txt; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in s9${TAG}*) continue ;; esac
    N=$(claims "$f"); [ -z "$N" ] && continue
    if [ "$N" -gt "$BEST_N" ]; then BEST_N="$N"; BEST="$f"; fi
done

if [ -z "$BEST" ]; then
    echo "no answer on disk for tau=${TAU} k=${K}; nothing to refine"
    exit 0
fi

OUT="${RUN}/s9${TAG}${C}_tau${TAU}_k${K}.txt"
echo "host=$(hostname) tau=${TAU} k=${K} from=${BEST} (${BEST_N}) -> ${OUT}"
echo "deadline $(date -u -d @${REFINE_END}) UTC"

"$BIN" refine --data "$DATA" --solution "$BEST" \
    --rounds 6 --trials 48 --event-verts 28 --event-radius 220 \
    --relocate-edges 48 --relocate-radius 120 \
    --deadline "$REFINE_END" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"

NEW=$(claims "$OUT"); NEW="${NEW:--1}"
echo "tau=${TAU} k=${K}: ${BEST_N} -> ${NEW} (${OUT})"
# A refinement that somehow came back short is simply left on disk: finalize ranks by the
# maximum over all s* files, so it can be ignored but never lose the cell.
