#!/bin/bash
# cells.sh -- load the (tau, k) sub-problems every stage works through.  Sourced, not run.
#
# Sets TAUS, KS and NCELLS.  Reads $CELLS_FILE when it is set and readable -- the file
# competition/parse_params.py writes from the organisers' own parameters file -- and
# otherwise falls back to the sample dataset's nine combinations, which is what every
# stage had hard-coded before this file existed.
#
# The fallback is deliberate rather than lazy: an unset CELLS_FILE means someone is running
# a stage by hand for a development experiment, and that should keep working exactly as it
# did.  The competition path always sets it, and GO.sh refuses to start without it.
#
# "Set but unreadable" is a hard error, and is not the same thing as "unset".  A SLURM task
# inherits CELLS_FILE through --export=ALL but runs on a different machine, so a path under
# a node-local /tmp is exported perfectly and then cannot be opened.  Treating that as
# "no file, use the defaults" is the worst thing this script could do: every task would
# quietly solve the sample dataset's nine combinations instead of the competition's, and
# every stage would look healthy right up to a submission that answers the wrong questions.
# This was not hypothetical -- it is what happened the first time this was tested under
# SLURM, with the cell list on a node-local filesystem.

if [ -n "${CELLS_FILE:-}" ] && [ ! -r "${CELLS_FILE}" ]; then
    echo "cells.sh: CELLS_FILE=${CELLS_FILE} is set but cannot be read from $(hostname)." >&2
    echo "cells.sh: it must be on a filesystem every compute node can see." >&2
    exit 1
fi

if [ -n "${CELLS_FILE:-}" ]; then
    TAUS=(); KS=()
    while read -r _tau _k _rest; do
        case "${_tau}" in ''|'#'*) continue ;; esac
        [ -z "${_k}" ] && continue
        TAUS+=("${_tau}"); KS+=("${_k}")
    done < "${CELLS_FILE}"
    unset _tau _k _rest
    if [ "${#TAUS[@]}" -eq 0 ]; then
        echo "cells.sh: ${CELLS_FILE} contained no '<tau> <k>' lines" >&2
        exit 1
    fi
    CELLS_SOURCE="${CELLS_FILE}"
else
    TAUS=(0.25 0.25 0.25 0.5 0.5 0.5 0.75 0.75 0.75)
    KS=(  50   500  1000 50  500 1000 50   500  1000)
    CELLS_SOURCE="built-in sample defaults"
fi

NCELLS="${#TAUS[@]}"
