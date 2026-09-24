#!/bin/sh
# Builds and runs the native tests (host compiler; no Windows needed).
# Usage: tests/run.sh   (from anywhere)
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/voxistics_tests"
g++ -std=c++17 -O1 -Wall -Wextra -Istub -I.. \
    tests.cpp ../world.cpp ../worldfile.cpp ../vtex.cpp ../blocktex.cpp ../mesher.cpp \
    -o "$OUT"
"$OUT"
