#!/bin/bash
# Builds the native harnesses: benchmark (moves per second), fitness (plays
# games to the end) and survival (measures the hazard). None of them ship with
# the app -- they are what a change to the solver or the weights gets measured
# with. See docs/game-length.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/cpp/build-native"

if ! command -v cmake &> /dev/null; then
    echo "cmake not found, installing..."
    pip install cmake || { echo "ERROR: Could not install cmake"; exit 1; }
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo
echo "Built in $BUILD_DIR:"
echo "  ./benchmark [moves]   how fast the move search runs"
echo "  ./fitness [games]     plays whole games, reports their lengths"
echo "  ./survival --help     measures the hazard; start here"
