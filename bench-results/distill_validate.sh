#!/bin/bash
set -e
F=/workspaces/blokie/engine/cpp/build-native/fitness
D=/workspaces/blokie/bench-results

run() {
  local tag=$1 weights=$2
  local args="128 --cap 25000 --seed-base 10000000"
  [ -n "$weights" ] && args="$args --weights $weights"
  echo "=== $tag === $args"
  $F $args 2> $D/distill_val_${tag}.err > $D/distill_val_${tag}.out
  tail -5 $D/distill_val_${tag}.err
}

run baseline ""
run l10000 "976,0,5156,5028,18181,1455,121,1039,1376,2690,969,2992"
run l0     "0,0,4365,5218,0,1477,0,1033,1608,1825,224,3147"
