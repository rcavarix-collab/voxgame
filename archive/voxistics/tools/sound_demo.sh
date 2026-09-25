#!/bin/sh
# Builds and runs tools/sound_demo.cpp (host compiler; no Windows needed).
# Usage: tools/sound_demo.sh analyze | demo OUTDIR
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/voxistics_sound_demo"
g++ -std=c++17 -O2 -Wall -Wextra -I.. sound_demo.cpp ../sfx_synth.cpp ../music_synth.cpp -o "$OUT"
"$OUT" "$@"
