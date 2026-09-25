#!/bin/sh
# Builds and runs the native tests (host compiler; no Windows needed).
# Usage: tests/run.sh   (from anywhere)
set -e
cd "$(dirname "$0")"
sh ../tools/check_msvc.sh   # what Visual Studio's SDL checks reject and GCC doesn't
OUT="${TMPDIR:-/tmp}/cacophony_tests"
g++ -std=c++17 -O1 -Wall -Wextra -I.. tests.cpp ../terrain.cpp ../mech.cpp ../sun.cpp -o "$OUT"
"$OUT"
