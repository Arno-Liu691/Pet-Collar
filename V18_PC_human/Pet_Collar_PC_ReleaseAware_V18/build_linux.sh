#!/usr/bin/env bash
set -euo pipefail

gcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
  -Iinclude -Ialgorithm \
  src/main_pc.c src/csv_reader.c \
  algorithm/rest_detector.c algorithm/vital_estimator.c \
  -o pet_collar_pc_v18 -lm

echo "Build successful: pet_collar_pc_v18"
