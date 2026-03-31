#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/engine/cpp/build-wasm"
OUTPUT_DIR="$SCRIPT_DIR/engine/wasm"

# Verify emscripten is available.
if ! command -v emcmake &> /dev/null; then
    echo "Error: Emscripten not found. Install it and source emsdk_env.sh first."
    exit 1
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
