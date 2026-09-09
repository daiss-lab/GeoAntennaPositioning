#!/bin/bash

#SBATCH --job-name=gis5-refwide
#SBATCH --partition=batch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=04:00:00
#SBATCH --output=logs/p5-rwide-%A_%a.out
#SBATCH --error=logs/p5-rwide-%A_%a.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

: "${DATA:?}" "${BIN:?}" "${RUN:?}" "${REFINE_END:?}"
TAG="${TAG:-w}"

. slurm/cells.sh

C="${SLURM_ARRAY_TASK_ID:-0}"
if [ "$C" -ge "$NCELLS" ]; then
    echo "array index $C is past the $NCELLS cells in ${CELLS_SOURCE}; nothing to do"
    exit 0
fi
TAU="${TAUS[$C]}"; K="${KS[$C]}"

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=spread
export OMP_PLACES=cores

claims() { awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$1" 2>/dev/null; }

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
    --rounds 8 --trials 96 --event-verts 48 --event-radius 400 \
    --relocate-edges 96 --relocate-radius 240 \
    --deadline "$REFINE_END" \
    --out "$OUT"

"$BIN" score --data "$DATA" --solution "$OUT"

NEW=$(claims "$OUT"); NEW="${NEW:--1}"
echo "tau=${TAU} k=${K}: ${BEST_N} -> ${NEW} (wide refine, ${OUT})"
