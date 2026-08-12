#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/cpp/build-wasm"
OUTPUT_DIR="$SCRIPT_DIR/wasm"
EMSDK_VERSION="$(cat "$SCRIPT_DIR/.emscripten-version" | tr -d '[:space:]')"

# Install cmake if not available.
if ! command -v cmake &> /dev/null; then
    echo "cmake not found, installing..."
    pip install cmake || { echo "ERROR: Could not install cmake"; exit 1; }
fi

# Install Emscripten if not available.
if ! command -v emcmake &> /dev/null; then
    echo "Emscripten not found, installing via emsdk (version $EMSDK_VERSION)..."
    EMSDK_DIR="/tmp/emsdk"
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    "$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
    "$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"
    source "$EMSDK_DIR/emsdk_env.sh"
fi

# Build.
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
# nproc is GNU coreutils, so it is missing on macOS and the BSDs, where the
# same question is sysctl's. Falling back to a single job would still build.
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)"
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j"$JOBS"

# Copy outputs.
mkdir -p "$OUTPUT_DIR"
cp blokie-solver.js blokie-solver.wasm "$OUTPUT_DIR/"

echo "WASM build complete: $OUTPUT_DIR/blokie-solver.{js,wasm}"
