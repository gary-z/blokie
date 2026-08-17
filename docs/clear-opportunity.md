# Charging a crowded board for having no way to clear

A new evaluation term, measured at **41% longer games**, replicated on two
independent seeds. It is committed behind `BLOKIE_CLEAR_OPPORTUNITY`, off by
default, because turning it on changes what the engine plays: the committed WASM
and the reference evaluation in `engine/cpp/tests/eval-test.cpp` both have to be
regenerated, and the other twelve weights were tuned without it.

## What it measures

The evaluation already knows which pieces have nowhere left to go — that is the
deadly-piece term. It knows nothing about how near a row, column or cube is to
completing. On a crowded board those are different questions. A position can
have room for every piece in the game and still have no way to clear, and a
position with no way to clear is a position that only gets fuller.

So: count the placements, over pieces of three to five squares, that would
complete a line. Charge the board for the ones that are **missing**.

```
if occupied >= 30:
    ways = placements of any 3..5 square piece that complete a row, column or cube
    result += max(0, 30 - ways) * (occupied - 20) * weight
```

Two details about the shape. It is a penalty for absence rather than a bonus for
presence, because the search prunes against a running maximum and a negative
term would let a candidate that has already exceeded the bound come back under
it. And it is gated on occupancy, which is what makes it affordable: the mean
board carries 18.2 squares, so the enumeration almost never runs.

## What it is worth

Fixed-exposure chains, 25-move burn-in excluded, both arms on the same piece
streams. `weight = 0` is an exact control — the same code path with the term
multiplied out.

| weight | seed 555 | seed 999 |
|---:|---:|---:|
| 0 (control) | 47,036 | 47,584 |
| 100 | **68,589** | — |
| **200** | **68,230** | **65,061** |
| 350 | 62,036 | — |
| 600 | 46,799 | — |

Pooled across both seeds:

| | deaths | exposure | hazard | mean length |
|---|---:|---:|---:|---:|
| control | 634 | 30.0M | 2.113e-05 | 47,325 |
| weight 200 | 631 | 42.0M | 1.501e-05 | **66,613** |

Hazard ratio **0.710**, log-ratio standard error 0.056, **z = 6.1**, p ≈ 1e-9.
The confidence intervals are disjoint on each seed separately.

Throughput is 38,311 moves a second against 40,848 for the stock evaluation, so
about 5%. That is the gate doing its job.

## The control that says what the gain is

A penalty is a penalty, and a term that only fires above thirty squares could
buy its improvement simply by making the engine avoid thirty squares. So the same
charge was run again with the same gate and the same magnitude, blind to how many
ways there are to clear — a flat `30 * (occupied - 20) * weight`:

| | seed 555 | seed 999 |
|---|---:|---:|
| control | 47,036 | 47,584 |
| clear-aware, weight 200 | **68,230** | **65,061** |
| flat charge, weight 200 | 35,368 | 33,720 |

Blind crowding aversion is **27% worse than not having the term at all**. The
gain is the clear counting.

## Why it could not be a cheaper term

The quantity is a one-ply lookahead, not a property of the board, and that is
worth knowing because it is the reason nothing already in the evaluation stands
in for it. Measured against the six crowded pairs in `engine/golden/golden.json`,
where the label is survival rather than clearing:

| candidate | pairs ordered correctly |
|---|---:|
| **clearing placements, all pieces** | **5/6** |
| clearing placements, 3-5 square pieces | 5/6 |
| pieces with any clearing placement | 4/6 |
| clearing placements, 5 smallest pieces | 3/6 |
| lines within 3 of clearing, gap contiguous | 2/6 |
| lines at 7 or more, lines at 8 or more | 1/6 |
| weighted near-lines, sum of squared nearness | 1/6 |
| empty cells that would complete a line | 1/6 |
| row-and-column combo cells | 1/6 |
| max line fill, occupancy, the shipped evaluation | 0/6 |

Every statistic over how full the lines are orders at most two of the six. The
enumeration orders five. A first attempt at the cheap version — charge a crowded
board when nothing is within two squares of clearing — fixed one of six and is
not in this branch.

## What the corpus got right and wrong, again

The pairs found the mechanism and were wrong about the magnitude, which is the
same pattern as `eval-rework-probe.md`. At weight 600 the term fixes four of the
six crowded pairs; at weight 200 it fixes two — and 200 plays 46% better than
600. Even with pairs labelled by survival, pass rate keeps pointing past the
optimum. Use them to find the mechanism and the hazard to set the number.

## What is left before this can ship

* **Sweep the three numbers that were guessed and never tuned**: the gate at 30
  squares, the cap at 30 ways, and the three-to-five square piece filter. The
  41% comes from untuned guesses, so it is more likely a floor than a ceiling.
* **Make the weight tunable** — it is a compile-time constant here, and belongs
  in `EvalWeights` as a fourteenth slot so the fitness tooling can reach it.
* **Re-tune the other twelve.** They were fitted without this term and the
  balance has moved. In particular the 46,962 figure recorded in `eval.h`
  describes an engine that does not include this.
* **Regenerate the committed WASM**, which `check-wasm.yml` rebuilds and
  compares.
* **Extend the reference evaluation** in `tests/eval-test.cpp`. Turning the
  option on today fails exactly one test, `evaluation`, for that reason; the
  other six pass.

## Reproducing

```bash
cmake -S engine/cpp -B /tmp/on -DCMAKE_BUILD_TYPE=Release -DBLOKIE_CLEAR_OPPORTUNITY=ON
make -C /tmp/on fitness -j
```

The weight, gate and cap are `BLOKIE_CLEAR_OPPORTUNITY_WEIGHT`, `_GATE` and
`_CAP` in `eval.cpp`, overridable from the compiler command line, which is how
the sweep above was run.
