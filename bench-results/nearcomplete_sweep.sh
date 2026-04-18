#!/bin/bash
set -e
F=/workspaces/blokie/engine/cpp/build-native/fitness
D=/workspaces/blokie/bench-results
BASE="1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067"

run() {
  local w12=$1
  local tag="w12_$w12"
  echo "=== $tag ==="
  $F 128 --cap 25000 --seed-base 10000000 --weights "$BASE,$w12" \
    2> $D/nc_$tag.err > /dev/null
  tail -4 $D/nc_$tag.err
  echo
}

run 0       # baseline (same as default)
run 500
run 1000
run 2000
run 5000
run 10000
