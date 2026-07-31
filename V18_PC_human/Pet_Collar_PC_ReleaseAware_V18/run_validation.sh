#!/usr/bin/env bash
set -euo pipefail

EXE="${1:-./pet_collar_pc_v18}"
export MPLCONFIGDIR="${MPLCONFIGDIR:-/tmp/pet_collar_matplotlib}"

"$EXE" validation/datasets/stable/input.csv validation/datasets/stable/v18_results.csv
"$EXE" validation/datasets/rr_change/input.csv validation/datasets/rr_change/v18_results.csv
"$EXE" validation/datasets/disruption/input.csv validation/datasets/disruption/v18_results.csv

python3 validation/evaluate_v18.py
