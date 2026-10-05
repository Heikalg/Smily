#!/usr/bin/env bash
# Build and run the host preview + behaviour harness.
#   out/*.png  - sprite sheet of every animation frame
#   stdout     - state machine / input / motion assertions
set -euo pipefail
cd "$(dirname "$0")"

FW=../../firmware/smily
mkdir -p out

g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 \
    -I stubs -I "$FW" \
    preview.cpp \
    "$FW/faces.cpp" \
    "$FW/animation.cpp" \
    "$FW/display.cpp" \
    "$FW/input_handler.cpp" \
    "$FW/motion.cpp" \
    "$FW/power.cpp" \
    "$FW/state_machine.cpp" \
    -o out/preview

./out/preview
status=$?

python3 pgm2png.py out/*.pgm
rm -f out/*.pgm
exit $status
