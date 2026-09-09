#!/bin/bash
# UPDATE.sh -- assemble the best answers currently on disk into a SECOND, separately named
# archive, without touching the first one.
#
#   ./competition/UPDATE.sh [output-name]        # default: UpdatedGISCUP2026
#
# WHY THIS EXISTS RATHER THAN JUST RE-RUNNING FINISH.sh
#
# FINISH.sh packages results/submission.txt, which is written by the finalize job.  That
# job is queued behind polish and will not run until ~13:00 UTC.  This script does the same
# selection and assembly finalize5 does -- best-of-everything per cell, re-scored from the
# emitted file -- but does it now, from whatever has landed, into its own file names:
#
#     results/final_upd/       instead of results/final/
#     results/labelled_upd/    instead of results/labelled/
#     results/submission_updated.txt
#     <output-name>.zip
#
# Nothing it writes is read by the queued chain, so running it cannot disturb the finalize
# that follows, and the already-validated GISCUP2026.zip is never opened.  It is idempotent:
# run it again whenever more results land and it rebuilds from the new best.
#
# WHAT IT REFUSES TO DO
#
# Every cell's chosen answer is re-scored by `score` reading the emitted file -- the route
# the organisers' evaluator takes -- and the run aborts if the scorer disagrees with the
# file's own claim line, or if any claim is unsupported.  A submission is worth nothing if
# it is wrong, so a disagreement stops the archive rather than shipping a number nobody
# checked.  FINISH.sh then runs the organisers' own parser as a second gate.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

OUTNAME="${1:-UpdatedGISCUP2026}"

PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA="${DATA:-data/eval.bin}"
RUN="${RUN:-results/run}"
GEOJSON="${GEOJSON:-data/competition-dataset.geojson}"
export CELLS_FILE="${CELLS_FILE:-$ROOT/data/cells.txt}"
# FINISH.sh resolves its cell list as "$ROOT/$CELLS_FILE", so it must be handed a
# repo-relative path.  Passing the absolute one doubles the prefix and the organisers'
# validator dies on ENOENT before it reads a single antenna -- which reads like a rejected
# submission rather than a caller error, so strip the root here rather than there.
CELLS_REL="${CELLS_FILE#"$ROOT"/}"
BASELINE="${BASELINE:-42762}"

[ -x "$PY" ] || { echo "UPDATE: no python at $PY"; exit 1; }
[ -x "$BIN" ] || { echo "UPDATE: no solver binary at $BIN"; exit 1; }

. slurm/cells.sh

FINAL=results/final_upd
LAB=results/labelled_upd
SUB=results/submission_updated.txt
rm -rf "$FINAL" "$LAB"
mkdir -p "$FINAL"

claims() { awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$1" 2>/dev/null; }

NOW=$(date +%s)
TOTAL=0
printf '%-6s %-5s %9s %9s %8s  %s\n' tau k packaged best delta source

for C in $(seq 0 $(( NCELLS - 1 ))); do
    TAU="${TAUS[$C]}"; K="${KS[$C]}"

    # Best over everything, preferring files that have been still for 60 s.  A solver
    # checkpointing right now can only be read short, never long -- a torn line 3 loses
    # claims -- so this is belt-and-braces on top of ranking by the maximum.
    BEST=""; BEST_N=-1; BEST_ANY=""; BEST_ANY_N=-1
    for f in "${RUN}"/s*_tau${TAU}_k${K}.txt "${RUN}"/polished_tau${TAU}_k${K}.txt; do
        [ -e "$f" ] || continue
        N=$(claims "$f"); [ -z "$N" ] && continue
        if [ "$N" -gt "$BEST_ANY_N" ]; then BEST_ANY_N="$N"; BEST_ANY="$f"; fi
        AGE=$(( NOW - $(stat -c %Y "$f") ))
        [ "$AGE" -lt 60 ] && continue
        if [ "$N" -gt "$BEST_N" ]; then BEST_N="$N"; BEST="$f"; fi
    done
    if [ -z "$BEST" ]; then BEST="$BEST_ANY"; BEST_N="$BEST_ANY_N"; fi
    [ -n "$BEST" ] || { echo "!! tau=${TAU} k=${K}: NO ANSWER"; exit 1; }

    cp "$BEST" "${FINAL}/tau${TAU}_k${K}.txt"

    # Re-score from the emitted file and insist the scorer agrees with it.
    OUT=$("$BIN" score --data "$DATA" --solution "${FINAL}/tau${TAU}_k${K}.txt" 2>&1)
    LINE=$(printf '%s\n' "$OUT" | grep -E '^SCORE ' || true)
    [ -n "$LINE" ] || { echo "!! tau=${TAU} k=${K}: scorer produced no SCORE line"; exit 1; }
    SERV=$(printf '%s\n' "$LINE" | sed -n 's/.*serviced=\([0-9]*\).*/\1/p')
    CLM=$(printf '%s\n' "$LINE" | sed -n 's/.*claims=\([0-9]*\).*/\1/p')
    if [ "$SERV" != "$CLM" ] || [ "$CLM" != "$BEST_N" ]; then
        echo "!! tau=${TAU} k=${K}: scorer says serviced=${SERV} claims=${CLM}, file claims ${BEST_N}"
        exit 1
    fi
    if ! printf '%s\n' "$OUT" | grep -q "0 claimed but not supported"; then
        echo "!! tau=${TAU} k=${K}: the claim line is not supported by the exact evaluator"
        printf '%s\n' "$OUT" | grep "claim line" || true
        exit 1
    fi

    PACK=$(claims "results/final/tau${TAU}_k${K}.txt")
    printf '%-6s %-5s %9s %9s %+8d  %s\n' "$TAU" "$K" "${PACK:-?}" "$BEST_N" \
        "$(( BEST_N - ${PACK:-0} ))" "$(basename "$BEST")"
    TOTAL=$(( TOTAL + BEST_N ))
done

echo
printf 'total %d   (packaged GISCUP2026.zip: %d,  delta %+d)\n' \
    "$TOTAL" "$BASELINE" "$(( TOTAL - BASELINE ))"

if [ "$TOTAL" -le "$BASELINE" ]; then
    echo
    echo "No improvement over the archive you already have; not building a second one."
    echo "(Set BASELINE=0 to package anyway.)"
    exit 0
fi

# Solver ids -> dataset ids, then assemble.  Same tail as finalize5.sbatch: an unrelabelled
# submission parses fine and scores zero.
IDS="${DATA}.ids"
DIR="$FINAL"
if [ -r "$IDS" ]; then
    "$PY" python/relabel.py --ids "$IDS" --in "$FINAL" --out "$LAB"
    DIR="$LAB"
else
    echo "!! no id sidecar at ${IDS}; assuming the solver's ids are the dataset's"
fi

"$PY" python/assemble.py --dir "$DIR" --out "$SUB" --cells "$CELLS_FILE"
echo "wrote $SUB"

# Keep the first archive's validation report; FINISH.sh writes results/validation.json.
[ -r results/validation.json ] && [ ! -r results/validation_safety.json ] && \
    cp results/validation.json results/validation_safety.json

exec env CELLS_FILE="$CELLS_REL" ./competition/FINISH.sh "$GEOJSON" "$SUB" "$OUTNAME"
