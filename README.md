# Geospatial Antenna Positioning: GIS Cup 2026

#### This project aims to find a solution to the GIS cup problem under SIGSPATIAL 2026. Given a set of building footprints as polygons and a number of antennas, this project finds the placement of antennas that maximizes the coverage of service among the buildings.

## Layout

    src/            C++17 solver: candidates, visibility, search, refinement, scoring
    python/         GeoJSON conversion, independent verification, submission assembly
    slurm/          SLURM batch scripts and pipeline drivers
    competition/    one-command entry points used on competition day

## Method

1. **Candidates.** Every polygon vertex, plus points spaced along each edge.
2. **Visibility.** An angular sweep per candidate gives the intervals of perimeter it can
   see; the result is cached once and shared by every sub-problem.
3. **Construction.** Randomised lazy greedy on a clipped surrogate objective that credits
   partial coverage, with bundled two-antenna moves so a building no single antenna can
   finish is still reachable.
4. **Improvement.** Large-neighbourhood search: tear out a few antennas, rebuild greedily,
   keep the result if it is not worse. Replicas run in parallel and periodically adopt the
   best solution found; with `--share-dir` they do so across nodes.
5. **Polish.** Coordinate ascent over legal positions, moving each antenna to the best
   tangency event on its edge or to a nearby vertex, scored exactly.

Every number reported comes from re-reading the emitted answer and scoring it from
scratch, never from the search's own bookkeeping.

## Build

A C++17 compiler with OpenMP. No third-party C++ dependencies.

    make            # optimised, OpenMP, -march=native  -> bin/gis_cup
    make portable   # same without -march=native, if the run host differs
    make test       # geometry self-tests

`python/verify.py` needs Python 3 with Shapely; the solver itself needs no Python.

## Running

    python3 python/prepare.py DATASET.geojson data/eval.bin

    ./bin/gis_cup precompute --data data/eval.bin --out data/eval_arcs.bin \
        --slot-len 0.05 --spacing 4.0 --max-range 2400

    ./bin/gis_cup solve --data data/eval.bin --cache data/eval_arcs.bin --slot-len 0.05 \
        --tau TAU --k K --time SECONDS --replicas 32 --out results/tauTAU_kK.txt

    ./bin/gis_cup refine --data data/eval.bin --solution results/tauTAU_kK.txt \
        --out results/polished.txt

    ./bin/gis_cup score --data data/eval.bin --solution results/polished.txt

`--slot-len` must match between `precompute` and `solve`; `solve` refuses a cache built for
a different discretisation. Run `./bin/gis_cup` with no arguments for the full option list.

Answers carry the solver's internal building handles. `python/relabel.py` rewrites them
into the dataset's own ids using the `<binary>.ids` sidecar `prepare.py` writes, and
`python/assemble.py` joins the per-cell answers into one submission file.

## On a SLURM cluster

    competition/GO.sh DATASET.geojson competition-parameters.txt HOURS
    competition/FINISH.sh DATASET.geojson results/submission.txt NAME

`GO.sh` reads the parameters file, submits every stage against absolute deadlines derived
from `HOURS`, and `FINISH.sh` validates the assembled answer and builds the archive. The
stage-by-stage commands above need no cluster.
