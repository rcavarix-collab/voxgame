#!/bin/sh
# Builds and runs the native tests (host compiler; no Windows needed).
# Usage: tests/run.sh   (from anywhere)
set -e
cd "$(dirname "$0")"
sh ../tools/check_msvc.sh   # what Visual Studio's SDL checks reject and GCC doesn't
OUT="${TMPDIR:-/tmp}/voxistics_tests"
g++ -std=c++17 -O1 -Wall -Wextra -Istub -I.. \
    tests.cpp ../world.cpp ../glowlight.cpp ../worldfile.cpp ../vtex.cpp ../blocktex.cpp ../mesher.cpp ../shapes.cpp ../icons.cpp ../theline.cpp ../pulse.cpp ../fliers.cpp ../essence.cpp ../essencemap.cpp \
    ../music_synth.cpp ../sfx_synth.cpp ../soundscape.cpp \
    -o "$OUT"
"$OUT"
