#!/bin/bash
set -e
F=/workspaces/blokie/engine/cpp/build-native/fitness
D=/workspaces/blokie/bench-results
# 13 base weights (existing defaults + w12=0 for not-near-complete)
BASE="1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,0"

run() {
  local w13=$1
  local tag="w13_$w13"
  echo "=== $tag ==="
  $F 128 --cap 25000 --seed-base 10000000 --weights "$BASE,$w13" \
    2> $D/cl_$tag.err > /dev/null
  tail -4 $D/cl_$tag.err
  echo
}

run 0        # sanity: identical to pre-feature baseline
run 100
run 500
run 1000
run 2000
run 5000
