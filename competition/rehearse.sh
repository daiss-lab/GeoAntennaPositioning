#!/bin/bash

set -uo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

WORK="${1:-$(mktemp -d "${TMPDIR:-/tmp}/giscup-rehearse-XXXXXX")}"
PY="${PY:-/mmfs1/home/1557acharjs/GIS_CUP/venv/bin/python}"
BIN="${BIN:-./bin/gis_cup}"
EVALDIR="${EVALDIR:-evaluator/official}"
NODEBIN="${NODEBIN:-evaluator/node/bin}"

mkdir -p "$WORK"
PASS=0
FAIL=0

step() {
    local name="$1"; shift
    printf '%-46s' "$name"
    if "$@" > "$WORK/last.log" 2>&1; then
        echo "PASS"
        PASS=$(( PASS + 1 ))
    else
        echo "FAIL"
        FAIL=$(( FAIL + 1 ))
        sed 's/^/      | /' "$WORK/last.log" | tail -12
    fi
}

echo "=============================================================="
echo " rehearsal in $WORK"
echo "=============================================================="

make_fixture() {
    python3 - "$WORK" <<'PYEOF'
import json, sys
work = sys.argv[1]
feats = []
n = 0
for row in range(6):
    for col in range(8):
        x0, y0 = 500000.0 + col * 30.0, 3760000.0 + row * 30.0
        ring = [[x0, y0], [x0 + 18.0, y0], [x0 + 18.0, y0 + 12.0], [x0, y0 + 12.0], [x0, y0]]
        feats.append({"type": "Feature",
                      "properties": {"id": "BLD-%04d" % n},
                      "geometry": {"type": "Polygon", "coordinates": [ring]}})
        n += 1
json.dump({"type": "FeatureCollection",
           "crs": {"type": "name", "properties": {"name": "urn:ogc:def:crs:EPSG::32611"}},
           "features": feats}, open(work + "/dataset.geojson", "w"))
open(work + "/parameters.txt", "w").write(
    "# synthetic parameters, deliberately not the sample's values\n"
    "threshold values (tau): 0.25, 0.5\n"
    "number of antennas (k): 8, 16\n")
PYEOF
}

solve_all() {
    export OMP_NUM_THREADS=4
    mkdir -p "$WORK/final"
    while read -r tau k _rest; do
        case "$tau" in ''|'#'*) continue ;; esac
        "$BIN" solve --data "$WORK/data.bin" --cache "$WORK/arcs.bin" --slot-len 0.05 \
            --tau "$tau" --k "$k" --time 3 --replicas 4 --seed 11 --shortlist 8 \
            --out "$WORK/final/tau${tau}_k${k}.txt" || return 1
    done < "$WORK/cells.txt"
}

validate() (
    [ -x "$NODEBIN/node" ] || { echo "no vendored node; skipped"; return 0; }
    [ -s "$WORK/submission.txt" ] || { echo "no submission to validate"; return 1; }
    export PATH="$ROOT/$NODEBIN:$PATH"
    export NODE_OPTIONS="--max-old-space-size=8192"
    cd "$ROOT/$EVALDIR" || return 1
    node node_modules/.bin/vite-node official-validate.ts -- \
        "$WORK/dataset.geojson" "$WORK/submission.txt" "$WORK/cells.txt"
)

unrelabelled_must_fail() (
    [ -x "$NODEBIN/node" ] || return 0
    "$PY" python/assemble.py --dir "$WORK/final" --out "$WORK/raw.txt" \
        --cells "$WORK/cells.txt" >/dev/null 2>&1 || return 1
    export PATH="$ROOT/$NODEBIN:$PATH"
    export NODE_OPTIONS="--max-old-space-size=8192"
    cd "$ROOT/$EVALDIR" || return 1
    if node node_modules/.bin/vite-node official-validate.ts -- \
        "$WORK/dataset.geojson" "$WORK/raw.txt" >/dev/null 2>&1; then
        echo "the un-relabelled submission was ACCEPTED -- relabelling is not doing anything"
        return 1
    fi
    return 0
)

archive_contents() {
    local listing
    listing=$(unzip -Z1 "$WORK/Final_Submission.zip") || return 1
    echo "$listing" | grep -q '/solution\.txt$' || {
        echo "no solution.txt in the archive"; return 1; }
    echo "$listing" | grep -q '/source/' || {
        echo "no source/ directory in the archive"; return 1; }
    echo "$listing" | grep -q '/source/Makefile$' || {
        echo "source/ has no Makefile, so it cannot be built"; return 1; }
    return 0
}

step "fixture: 48 buildings, string ids"        make_fixture
step "parse_params reads a non-sample file"     "$PY" competition/parse_params.py \
        "$WORK/parameters.txt" --out "$WORK/cells.txt" --expect 4
step "prepare accepts non-integer ids"          "$PY" python/prepare.py \
        "$WORK/dataset.geojson" "$WORK/data.bin"
step "id sidecar written"                       test -s "$WORK/data.bin.ids"
step "precompute"                               "$BIN" precompute --data "$WORK/data.bin" \
        --out "$WORK/arcs.bin" --slot-len 0.05 --spacing 4.0 --max-range 2400
step "solve all cells"                          solve_all
step "un-relabelled submission is rejected"     unrelabelled_must_fail
step "relabel to the dataset's own ids"         "$PY" python/relabel.py \
        --ids "$WORK/data.bin.ids" --in "$WORK/final" --out "$WORK/labelled"
step "assemble"                                 "$PY" python/assemble.py \
        --dir "$WORK/labelled" --out "$WORK/submission.txt" --cells "$WORK/cells.txt"
step "the organisers' parser accepts it"        validate
step "package into an archive"                  "$PY" competition/package.py \
        --solution "$WORK/submission.txt" --cells "$WORK/cells.txt" \
        --dataset "$WORK/dataset.geojson" --out "$WORK/Final_Submission" \
        --root "$ROOT" --force
step "the archive exists"                       test -s "$WORK/Final_Submission.zip"
step "archive holds solution.txt and source/"   archive_contents

echo
echo "=============================================================="
if [ "$FAIL" -eq 0 ]; then
    echo " REHEARSAL PASSED -- $PASS checks"
    echo " artefacts in $WORK"
    echo "=============================================================="
    exit 0
fi
echo " REHEARSAL FAILED -- $FAIL of $(( PASS + FAIL )) checks"
echo " artefacts in $WORK"
echo "=============================================================="
exit 1
