#!/bin/bash
set -e
F=/workspaces/blokie/engine/cpp/build-native/fitness
D=/workspaces/blokie/bench-results
BASE="1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,0"

run() {
  local tag=$1 weights=$2
  echo "=== $tag ==="
  $F 64 --cap 80000 --seed-base 42 --weights "$weights" \
    2> $D/cl80_${tag}.err > /dev/null
  tail -4 $D/cl80_${tag}.err
  echo
}

run baseline "$BASE,0"
run w13_500  "$BASE,500"
