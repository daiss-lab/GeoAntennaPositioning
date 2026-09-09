#!/bin/bash

set -euo pipefail
cd "$(dirname "$0")/.."

SOLVE_END="${1:?usage: closeout.sh <solve-end-epoch> [polish-minutes]}"
POLISH_MIN="${2:-30}"

PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA="${DATA:-data/eval.bin}"
RUN="${RUN:-results/run}"
GEOJSON="${GEOJSON:-/mmfs1/home/1557acharjs/GIS_CUP/dataset/GIS-cup-competition-dataset.geojson}"

now=$(date +%s)
if [ "$SOLVE_END" -gt "$now" ]; then
    echo "closeout: waiting until $(date -u -d @"$SOLVE_END") ($(( (SOLVE_END - now) / 60 )) min)"
    while [ "$(date +%s)" -lt "$SOLVE_END" ]; do sleep 30; done
fi

echo "closeout: ending the search at $(date -u -Is)"
for name in gis5-solve gis5-restart gis5-port; do
    ids=$(squeue -u "$USER" -h -n "$name" -o "%A" | sort -u)
    [ -n "$ids" ] && scancel $ids && echo "  cancelled $name ($(echo $ids | wc -w) job(s))"
done
sleep 10

echo "closeout: best on disk per cell"
. slurm/cells.sh
for C in $(seq 0 $(( NCELLS - 1 ))); do
    TAU="${TAUS[$C]}"; K="${KS[$C]}"; m=0; src=""
    for f in "$RUN"/s*_tau${TAU}_k${K}.txt; do
        [ -e "$f" ] || continue
        v=$(awk 'NR==3{n=split($0,a,","); c=0; for(i=1;i<=n;i++) if(length(a[i])>0) c++; print c}' "$f")
        [ -n "$v" ] && [ "$v" -gt "$m" ] && { m="$v"; src=$(basename "$f"); }
    done
    printf "  tau=%-5s k=%-4s %7s  (%s)\n" "$TAU" "$K" "$m" "$src"
done

POLISH_END=$(( $(date +%s) + POLISH_MIN * 60 ))
echo "closeout: polish until $(date -u -d @"$POLISH_END")"
J_POL=$(sbatch --parsable --array=0-$(( NCELLS - 1 ))%"$NCELLS" \
    --job-name=gis5-polish --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=02:00:00 --output=logs/p5-pol-%A_%a.out --error=logs/p5-pol-%A_%a.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",POLISH_END="$POLISH_END" \
    slurm/polish5.sbatch)
echo "  polish: $J_POL"

J_FIN=$(sbatch --parsable --dependency=afterany:"$J_POL" \
    --job-name=gis5-final --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=02:00:00 --output=logs/p5-fin-%j.out --error=logs/p5-fin-%j.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",PY="$PY",GEOJSON="$GEOJSON" \
    slurm/finalize5.sbatch)
echo "  finalize: $J_FIN"
echo "closeout: done; results/submission.txt when $J_FIN finishes"
