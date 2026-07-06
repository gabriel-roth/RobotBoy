#!/usr/bin/env bash
# Lane 2: the beads_dsp Catch2 suite (CMake + CTest, offline, vendored Catch2).
set -euo pipefail
cd "$(dirname "$0")"
cmake -B build -G "Unix Makefiles"
cmake --build build -j
ctest --test-dir build --output-on-failure
