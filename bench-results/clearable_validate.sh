#!/bin/bash
set -e
F=/workspaces/blokie/engine/cpp/build-native/fitness
D=/workspaces/blokie/bench-results
BASE="1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,0"
SEED=42

run() {
  local w13=$1
  local tag="w13_${w13}_s${SEED}"
  echo "=== $tag ==="
  $F 128 --cap 25000 --seed-base $SEED --weights "$BASE,$w13" \
    2> $D/clv_$tag.err > /dev/null
  tail -4 $D/clv_$tag.err
  echo
}

run 0
run 500
