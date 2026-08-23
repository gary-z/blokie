# Running evaluation experiments

The native tools in `engine/cpp` measure game length, screen weight changes,
inspect search behavior, and validate board-pair examples.

## Build and test

Use a release build for performance measurements:

```bash
cmake -S engine/cpp -B /tmp/blokie-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/blokie-build --parallel
ctest --test-dir /tmp/blokie-build --output-on-failure
```

The build prefers GCC 16 when it is installed. Use the same compiler, machine,
build flags, and thread count for every arm of a comparison.

## Compare evaluators

`fitness` runs independent fixed-exposure chains. A chain restarts when a game
ends, excludes the initial burn-in after each restart, and continues until it
has accumulated the requested measured moves.

Build the baseline and candidate in separate directories, then run both with
the same options:

```bash
/tmp/blokie-baseline/fitness 640 --threads 32 \
    --chain-moves 10000 --burn-in 25 \
    --probe-occupancy-bands 15 62 854 23 36 > baseline.txt

/tmp/blokie-candidate/fitness 640 --threads 32 \
    --chain-moves 10000 --burn-in 25 \
    --probe-occupancy-bands 15 62 854 23 36 > candidate.txt

node engine/tools/compare-probes.js baseline.txt candidate.txt
```

Lower hazard is better. Its reciprocal is the expected number of three-piece
sets per game. The probe estimate is the primary comparison; the observed death
rate printed by `fitness` is an independent cross-check.

The summary goes to stderr. Chain data goes to stdout so it can be saved and
passed to the analysis tools. Keep those files outside the repository.

For a quick smoke test, reduce the chain count or measured moves. Use the full
fixed exposure for a result you intend to keep. Do not compare throughput from
runs built with different compilers or flags.

## Useful diagnostics

Inspect probe allocation and cost:

```bash
node engine/tools/analyze-probes.js candidate.txt
```

Compare observed death rates directly:

```bash
node engine/tools/compare-fitness.js baseline.txt candidate.txt
```

Check whether hazard is stable over game depth:

```bash
/tmp/blokie-build/fitness 400 --max-moves 20000 --hazard-bins 2000
```

`--max-moves` right-censors unfinished games. Use the reported hazard rather
than averaging truncated game lengths.

## Screen weight changes

`weight-screen` is a fast paired rollout for ranking candidates before a full
`fitness` run:

```bash
/tmp/blokie-build/weight-screen \
    --candidate 1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,250,335
```

The vector order matches `EvalWeights::getDefault()` in `engine/cpp/eval.h`.
A positive risk delta is worse. Treat the screen as triage; confirm promising
changes with `fitness`.

## Inspect the search and board pairs

```bash
/tmp/blokie-build/benchmark
/tmp/blokie-build/search-stats 20000
/tmp/blokie-build/golden
/tmp/blokie-build/golden_measure
```

- `benchmark` reports solver throughput.
- `search-stats` reports search shape, pruning, and repeated work.
- `golden` compares the current evaluator with the board-pair corpus.
- `golden_measure` checks those pairs through real play.

See [Golden board pairs](../engine/golden/README.md) for the corpus format and
editor.

The configurable tools print their full option lists with `--help`.
