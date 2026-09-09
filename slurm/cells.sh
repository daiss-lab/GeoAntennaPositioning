#!/bin/bash

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
