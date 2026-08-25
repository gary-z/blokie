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

## What the play order is worth

Everything above measures survival, which is what a game's score is mostly made
of. The one lever that changes points without touching survival is the order a
hand is played in: the search picks the three placements, and only their
sequence is left open, so every order reaches the same board and dies on the
same hand. What separates them is which piece triggers each clear, and whether
the hand ends on one.

`streak-order-experiment` walks a run once and scores every ordering policy
against that single trajectory, so there is no survival difference to control
for:

```bash
node engine/tools/streak-order-experiment.js --hands 150000
```

It reports `make_move`'s current policy, that policy with the clear streak
priced at a range of weights, and the best an ordering policy could have done
had it known every later hand -- a two-state forward pass, which `--verify N`
checks against exhaustive search over the first N hands.

The current policy is already at that ceiling: over 450,000 hands the
clairvoyant optimum was 2 points ahead in total. Ending a hand on a clear costs
points on 0.7% of hands, and when it costs anything the price is exactly 9,
because the order that leaves a clear last is the one that splits two adjacent
clears and forfeits the streak bonus between them. The streak it buys is
collected on 14% of the hands that inherit one, since only a clear on the very
next placement collects it. Pricing the streak at its face value of 9 loses
0.44 points per hand. Use the tool to re-check this if the scoring changes;
there is nothing here to win as it stands.

## Whether the placements are the best-scoring ones

The search chooses placements by evaluation, and every placement set that ends
on the same board evaluates the same, so which one comes back is decided by the
order they are enumerated in. Points never enter into it. That would not matter
if they all scored the same, and they do not: three pieces that exactly fill a
3x3 cube clear it wherever that cube sits, so the board cannot tell those
placements apart, but the piece that completes the cube forfeits its own squares
to the clear and swapping which piece that is moves the score.

`placement-set-experiment` enumerates every placement of the three pieces, in
every order, that reaches the board the search chose, and asks whether any of
them outscores what was played:

```bash
node engine/tools/placement-set-experiment.js --hands 20000
```

It is roughly ten times slower per hand than the search it is checking, so it is
a measurement and not a candidate implementation. `--verify N` re-runs the first
N hands without the reachability prunes and checks the answers match.

Over 60,000 hands an alternative outscored the engine on 0.09% of them -- about
one hand in a thousand -- and never by more than 9 points, for 0.0023 points a
hand against a baseline of 37.2. Roughly 7% of hands have some alternative
placement set reaching the same board, and the score really does vary across
them on 69%, so the engine is not landing on the best one by construction; it is
landing on it because the ordering pass in `make_move` already recovers most of
what the spread is made of. Closing the last of it means scoring placement sets
the evaluator considers identical, which costs an order of magnitude more search
than the points are worth.

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
