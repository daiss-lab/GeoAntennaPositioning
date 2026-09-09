#!/bin/bash
# pipeline5.sh -- GeoJSON in, checked submission file out, one command, no human decisions.
#
#   ./slurm/pipeline5.sh dataset.geojson 20        # 20 hours from now
#
# The window between the evaluation dataset appearing and the answer being due is 24 hours,
# so every stage is driven by an absolute deadline rather than a duration: a stage that
# starts late shortens itself instead of overrunning.
#
# ---- how this differs from pipeline4.sh, and why -------------------------------------
#
# pipeline4 spent the window on the island model: many jobs per cell sharing elites through
# the filesystem, in two waves.  Its own A/B (job 994479, `results/isab`) then measured that
# at equal cost:
#
#                       tau=0.5,k=1000   tau=0.75,k=500
#   A  1 job,  600 s        10272             3208
#   B  1 job, 2400 s        10346             3277      <- best in both cells
#   C  4 shared islands     10307             3215
#   D  4 independent        10330             3229
#
# Sharing lost to plain best-of-four in both cells, and C's four islands finished at
# 3212/3214/3215/3214 -- they had converged onto one trajectory, which is the failure mode
# replica sharing inside a node already had to be tuned away from.  Meanwhile B, one job
# given four times the wall clock on the same 32 cores, beat every arm that spread the same
# core-seconds over four nodes.
#
# So this pipeline does not share between nodes.  It runs ONE LONG JOB PER (CELL, SEED) and
# takes the best, which is arm D-with-B's-budget: the two things that actually won.
#
# Caveat worth stating: B beat C and D on two cells at one seed and 4x scale.  The ordering
# could change once a single search saturates, which is why SEEDS is a variable and not a
# constant -- if a confirmation run at window scale says otherwise, raise it.
#
# Five stages, submitted as dependent SLURM jobs, then this returns:
#
#   prepare     GeoJSON -> binary                          (here, seconds)
#   precompute  visibility cache at 0.05 m slots, shared by all nine cells (one node, ~30 s)
#   solve       9 cells x SEEDS seeds, each to 80% of the window
#   polish      across-edge refinement of the best answer per cell
#   finalize    pick the best per cell over everything, re-score, verify, assemble
#
# Dependencies are `afterany`, never `afterok`: if one cell's task dies the other eight
# still have checkpointed answers, and a submission with eight good blocks beats none.

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"

GEOJSON="${1:?usage: pipeline5.sh <dataset.geojson> <hours> [seeds-per-cell]}"
HOURS="${2:?usage: pipeline5.sh <dataset.geojson> <hours> [seeds-per-cell]}"
SEEDS="${3:-4}"

PY="${PY:-./.venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
DATA=data/eval.bin
CACHE=data/eval_arcs.bin
RUN=results/run

# CELLS_FILE, when exported, is the (tau, k) list competition/parse_params.py derived from
# the organisers' parameters file.  Unset, every stage falls back to the sample dataset's
# nine combinations, which is what this pipeline assumed before the parameters became
# something we have to read.  competition/GO.sh always sets it.
. slurm/cells.sh
export CELLS_FILE
echo "cells: ${NCELLS} from ${CELLS_SOURCE}"

NOW=$(date +%s)
DEADLINE=$(( NOW + $(printf '%.0f' "$(echo "$HOURS * 3600" | bc -l)") ))
SOLVE_END=$(( NOW + (DEADLINE - NOW) * 80 / 100 ))
POLISH_END=$(( NOW + (DEADLINE - NOW) * 93 / 100 ))

mkdir -p "$RUN" logs data results

echo "pipeline5: deadline $(date -d @${DEADLINE}), solve to $(date -d @${SOLVE_END}), \
polish to $(date -d @${POLISH_END}), ${SEEDS} seeds per cell"

# ---- stage 1: prepare, here and now -----------------------------------------------------
"$PY" python/prepare.py "$GEOJSON" "$DATA"
echo "prepared $DATA"

# ---- stage 2: precompute ----------------------------------------------------------------
# 96 cores and 256G rather than a whole 128-core node.  Every other stage waits on this one,
# so what matters is when it STARTS, not how wide it is: on competition day the largest free
# block on any node was 112 cores and a 128-core request sat in PENDING (Resources) behind a
# whole node draining.  96/256G places immediately on a partly-used node and the stage is a
# few minutes of work either way.  PRE_CPUS is overridable for an uncontended cluster.
PRE_CPUS="${PRE_CPUS:-96}"
PRE_MEM="${PRE_MEM:-256G}"
J_PRE=$(sbatch --parsable \
    --job-name=gis5-pre --partition=batch --nodes=1 --ntasks=1 --cpus-per-task="$PRE_CPUS" \
    --mem="$PRE_MEM" --time=01:00:00 --output=logs/p5-pre-%j.out --error=logs/p5-pre-%j.err \
    --wrap "export OMP_NUM_THREADS=$PRE_CPUS OMP_PROC_BIND=spread OMP_PLACES=cores; \
            $BIN precompute --data $DATA --out $CACHE --slot-len 0.05 --spacing 4.0 \
                 --max-range 2400")
echo "precompute: $J_PRE"

# ---- stage 3: solve, 9 cells x SEEDS seeds ---------------------------------------------
# 32 cores per task rather than 128.  Solution04 §5 measured a 216-task array getting four
# slots on a full machine; a 32-core task backfills into gaps a 128-core task cannot use,
# and on a contended cluster throughput beats per-run size.
N=$(( NCELLS * SEEDS ))
J_SOLVE=$(sbatch --parsable --dependency=afterany:"$J_PRE" \
    --array=0-$(( N - 1 ))%"$N" \
    --job-name=gis5-solve --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=24:00:00 --output=logs/p5-solve-%A_%a.out \
    --error=logs/p5-solve-%A_%a.err \
    --export=ALL,DATA="$DATA",CACHE="$CACHE",BIN="$BIN",RUN="$RUN",SEEDS="$SEEDS",\
SOLVE_END="$SOLVE_END" \
    slurm/wave5.sbatch)
echo "solve: $J_SOLVE"

# ---- stage 4: polish --------------------------------------------------------------------
J_POL=$(sbatch --parsable --dependency=afterany:"$J_SOLVE" \
    --array=0-$(( NCELLS - 1 ))%"$NCELLS" \
    --job-name=gis5-polish --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=06:00:00 --output=logs/p5-pol-%A_%a.out --error=logs/p5-pol-%A_%a.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",POLISH_END="$POLISH_END" \
    slurm/polish5.sbatch)
echo "polish: $J_POL"

# ---- stage 5: finalize ------------------------------------------------------------------
J_FIN=$(sbatch --parsable --dependency=afterany:"$J_POL" \
    --job-name=gis5-final --partition=batch --nodes=1 --ntasks=1 --cpus-per-task=32 \
    --mem=64G --time=02:00:00 --output=logs/p5-fin-%j.out --error=logs/p5-fin-%j.err \
    --export=ALL,DATA="$DATA",BIN="$BIN",RUN="$RUN",PY="$PY",GEOJSON="$GEOJSON" \
    slurm/finalize5.sbatch)
echo "finalize: $J_FIN"

echo
echo "submitted.  the submission will be at results/submission.txt"
echo "watch with: squeue -u \$USER"
