#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/engine/cpp/build-wasm"
OUTPUT_DIR="$SCRIPT_DIR/engine/wasm"

# Install cmake if not available.
if ! command -v cmake &> /dev/null; then
    echo "cmake not found, installing..."
    pip install cmake || { echo "ERROR: Could not install cmake"; exit 1; }
fi

# Install Emscripten if not available.
if ! command -v emcmake &> /dev/null; then
    echo "Emscripten not found, installing..."
    EMSDK_DIR="/tmp/emsdk"
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    "$EMSDK_DIR/emsdk" install latest
    "$EMSDK_DIR/emsdk" activate latest
    source "$EMSDK_DIR/emsdk_env.sh"
fi

# Build.
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc)

# Copy outputs.
mkdir -p "$OUTPUT_DIR"
cp blokie-solver.js blokie-solver.wasm "$OUTPUT_DIR/"

echo "WASM build complete: $OUTPUT_DIR/blokie-solver.{js,wasm}"
