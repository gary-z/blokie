# Generating placements a batch at a time

The search does two things per candidate: build the board, then score it.
Building it is shifts and masks; scoring it is mostly popcount. Only the first
is worth widening, and that asymmetry is the whole design.

## What changed

The innermost loop of `makeMoveSimpleImpl` used to walk placements one at a
time through `NextGameStateIterator`. It now takes four anchors at once,
translates the piece to all four, ors them into the board, and works out what
each one cleared, in four lanes. Scoring stays exactly as it was: one board at
a time, through the same `simpleEvalDefault`.

Written in GNU vector extensions (`__attribute__((vector_size))`), not
intrinsics. There is no second copy of anything, nothing names an instruction
set, and the same source compiles to whatever width the target has.

## Why the evaluation is left alone

Scoring is the larger share of the time, which makes it the obvious target and
the wrong one. `simpleEvalImpl` takes the best score so far and abandons a
candidate the moment it passes it, so most candidates are dropped part way
through. A batch cannot do that: lanes share a control path, so a vector runs
until *every* lane is finished, and the unpruned work costs more than the width
saves. Batching the evaluation means giving up the cutoff, and the cutoff is
worth more.

Generation has no such escape — every candidate is built in full either way —
so widening it gives up nothing.

Popcount is the second reason. The evaluation is mostly popcount, and outside
of AVX-512 there is no vector popcount instruction; a compiler asked to
vectorize one extracts each lane, counts it scalar, and puts it back.

## Why four lanes

Four 64-bit lanes is one 256-bit register on x86-64 with AVX2. Measured on the
machine below, at equal correctness:

| lanes | vs scalar |
|---:|---:|
| 2 | 0.94× |
| **4** | **1.12×** |
| 16 | 0.73× |

Two lanes do not cover the per-batch overhead. Sixteen spill. Eight would be
one AVX-512 register, and on a target without AVX-512 it is also a build error
under `-Werror`: returning a 512-bit vector by value from a function changes the
ABI, and GCC says so.

`GEN_LANES` is one constant in `solver.cpp`, so re-checking this on a machine
with different registers is a one-line experiment.

## The three multiplies

The scalar clear detection smears a marker bit across a row, a column or a cube
with a multiply. A 64-bit vector multiply is not something every target has —
`vpmullq` is AVX-512 — and where it is missing the compiler scalarizes the lane,
which would have given most of the win back.

Every one of those constants is a sum of powers of two, so each becomes shifts
and adds:

- `* ROW_0` is `* (2^9 - 1)`, so `(x << 9) - x`
- `* TOP_LEFT_CUBE` is `* 7 * (1 + 2^9 + 2^18)`
- `* LEFT_MOST_COLUMN_A` is `* (1 + 2^9 + 2^18) * (1 + 2^27)`

The generated code contains `vpsllvq`/`vpsrlvq` for the per-lane translate,
`vpcmpgtq` for the 54/27 split, and no multiply at all.

## Where it is compiled in

`BLOKIE_VECTOR_GENERATION` in `solver.h` is on for native GCC and Clang, and off
for a WASM build unless `simd128` is enabled — without a vector unit the batch
is slower than the walk it replaces, and the web app is the one caller that
cannot be measured from here.

With it off, `solver.cpp` compiles to **byte-identical assembly** to the version
before the batch existed. That is checked rather than assumed:

    g++ -O3 -DNDEBUG -DBLOKIE_VECTOR_GENERATION=0 -S solver.cpp

so the committed WASM binary is unaffected by this change.

## Correctness

The batch is not allowed to pick a different move, only to reach the same one
faster, so it holds the walk order the iterator had: anchors lowest first out of
`a` and then `b`. Ties are settled by the first board that beats the best, which
makes that order part of the answer.

Checked by hashing every decision the search returns — chosen board, evaluation,
and all three placements — over 200,000 deterministic moves against the scalar
build. Identical. `fitness` also produces the same games, move counts and deaths
from the same seeds, and the existing test suite passes unchanged.

## Environment

- Base commit: `9956bf8`
- Compiler: GCC 16.2.0, C++23, `-O3 -flto -march=native`
- Host: 13th Gen Intel Core i9-13900KF (AVX2, no AVX-512)
- Measured: 2026-08-13

Absolute throughput is machine-specific. Every figure here is a ratio taken
from interleaved runs — each variant measured once per round, rounds repeated,
only within-round ratios quoted — because this box drifts by more over minutes
than the effect being measured.

| | ratio |
|---|---:|
| single-threaded `benchmark` | 1.12× |
| 16-thread `fitness` harness | 1.15× |

Generation was 24.9% of runtime before the change, measured by stubbing the
evaluation, so 1.33× is the ceiling for this approach and the rest of it is in
the scoring the change deliberately does not touch.
