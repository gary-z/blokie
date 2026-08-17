# Charging a crowded board for having no way to clear

A new evaluation term, measured at **62% longer games**, confirmed on a seed base
no parameter was chosen on. It is committed behind `BLOKIE_CLEAR_OPPORTUNITY`, off by
default, because turning it on changes what the engine plays: the committed WASM
and the reference evaluation in `engine/cpp/tests/eval-test.cpp` both have to be
regenerated, and the other twelve weights were tuned without it.

## What it measures

The evaluation already knows which pieces have nowhere left to go — that is the
deadly-piece term. It knows nothing about how near a row, column or cube is to
completing. On a crowded board those are different questions. A position can
have room for every piece in the game and still have no way to clear, and a
position with no way to clear is a position that only gets fuller.

So: count the placements, over pieces of four or five squares, that would
complete a line. Charge the board for the ones that are **missing**.

```
if occupied >= 30:
    ways = placements of any 4..5 square piece that complete a row, column or cube
    result += max(0, 30 - ways) * (occupied - 20) * weight
```

Two details about the shape. It is a penalty for absence rather than a bonus for
presence, because the search prunes against a running maximum and a negative
term would let a candidate that has already exceeded the bound come back under
it. And it is gated on occupancy, which is what makes it affordable: the mean
board carries 18.2 squares, so the enumeration almost never runs.

## What it is worth

Fixed-exposure chains, 25-move burn-in excluded, `weight = 0` as an exact control
— the same code path with the term multiplied out.

Confirmed at 1200 deaths an arm on seed base 20260817, a seed base no parameter
was chosen on:

| arm | deaths | exposure | hazard | mean length | 95% CI |
|---|---:|---:|---:|---:|---|
| control | 1205 | 55.1M | 2.188e-05 | 45,701 | 43,192–48,355 |
| pieces 3-5 | 1204 | 81.5M | 1.477e-05 | 67,699 | 63,981–71,633 |
| **pieces 4-5** | 1202 | 89.1M | 1.350e-05 | **74,085** | 70,012–78,393 |
| pieces 3-4 | 1202 | 78.3M | 1.535e-05 | 65,166 | 61,584–68,956 |

Against the control, pieces 4-5 is a hazard ratio of **0.617**, **z = 11.8**, a
game-length ratio of **1.62**. Pieces 3-5, which is what the first version of this
term shipped with, is **1.48, 95% CI 1.37 to 1.61**. So the term is worth **+48%**
as first written and **+62%** with the piece filter corrected.

Throughput is 39,348 moves a second against 39,911 for the control, so under 2%.
That is the gate doing its job.

## The cap is a quantile, not a threshold

The filter looked at first like a statement about which pieces are informative. It
is not. Measured on positions that arise in play, above the gate, the median
number of clearing placements is:

| pieces counted | median placements | fraction of boards the cap of 30 charges |
|---|---:|---:|
| 3-5 | 36 | ~45% |
| 4-5 | 31 | ~50% |
| 3-4 | 22 | ~70% |
| 4-4 | 15 | ~85% |

`max(0, 30 - ways)` is zero whenever a board has thirty ways or more, which is
half of them. The term never charged crowded boards; it charged boards in the
scarce **tail**, and changing the piece filter changes the statistic's median,
which slides the cap along its own distribution. The measured ordering follows
that fraction rather than the pieces:

| config | fraction charged | mean length |
|---|---:|---:|
| pieces 3-5, cap 15 | ~8% | 51,117 |
| pieces 3-5, cap 30 | ~45% | 67,699 |
| pieces 4-5, cap 30 | ~50% | **74,085** |
| pieces 3-4, cap 30 | ~70% | 65,166 |
| pieces 3-5, cap 60 | ~80% | 58,488 |

Charge too few boards and the term is off — cap 15 lands near the control.
Charge too many and it degenerates towards the flat crowding penalty that
measures 27% worse than not having the term at all. The optimum is the scarcest
half, and the cap and the filter are two ways of setting the same one number.

This also disposes of a variant that looked promising: placements that clear two
lines at once. Above the gate that count is zero on 79% of boards and on 96% at
lower occupancy, so charging its absence would have been a flat penalty wearing a
combo signal's clothes.

## Iterating on the shape

Eight arms, 800 deaths each, seed base 20260818, control 48,238. Five statistics
were tried, not just five parameterisations: placements of a piece-size range,
distinct pieces that can clear at all, and the exact probability that none of the
next three dealt pieces can clear -- which is computable, because pieces are dealt
uniformly and independently from the 47, so it is `((47 - reach) / 47)` cubed.

| form | mean game |
|---|---:|
| **placements, pieces 4-5, cap 30, weight 200** | **74,682** |
| P(none of the next three can clear), weight 12000 | 72,163 |
| placements, pieces 5-5, cap 16, weight 200 | 69,004 |
| placements, pieces 3-5, cap 36, weight 200 | 67,478 |
| placements, pieces 3-5, cap 36, weight 167 | 67,793 |
| distinct pieces that can clear, cap 25, weight 240 | 67,587 |
| placements, pieces 4-4, cap 15, weight 400 | 67,152 |
| placements, pieces 5-5, cap 16, weight 375 | 66,535 |
| P(none of the next three can clear), weight 6000 | 66,301 |

Two things come out of this, one of them a correction.

**Magnitude dominates the choice of statistic.** The probability form moved from
66,301 to 72,163 on nothing but a doubling of its weight -- a larger swing than
any difference between statistics in the table. The weights above were first
picked to hold each form's charge at zero ways equal to the shipped form's, which
is its *maximum* charge and says little about what it charges on the boards that
occur; that systematically under-weighted every alternative. Comparisons between
statistics in the earlier version of this section were substantially comparisons
between magnitudes.

**The piece filter is real and remains unexplained.** Pooling every measurement of
each, pieces 4-5 is 2003 deaths over 148.9M moves (hazard 1.346e-05) against
pieces 3-5 at 2811 deaths over 190.2M (1.478e-05): a hazard ratio of 0.910, SE
0.029, **z = 3.2**. Three explanations were offered and all three failed:

* *The five-square pieces carry the signal.* Then pieces 5-5 alone should be
  strong. It is 69,004, no better than 3-5.
* *It is a quantile -- the filter sets the statistic's median and so the fraction
  of boards the cap charges.* Then capping each filter at its own median should
  make them equal. It made the three median-scaled filters equal to each other,
  at 66.5k, 67.2k and 67.8k, and left all three below 4-5.
* *It is the slope -- holding cap times weight constant forced slopes from 167 to
  400.* Then matching slopes should close the gap. At slope 200 for both, 3-5 is
  67,478 against 74,682.

So pieces 4-5 is an empirically fitted parameter, not a principled one. It is
worth roughly 10% and nobody should read a mechanism into it.

## Predicting death is not the same as being useful

A cheap diagnostic, meant to rank candidate statistics without a game arm each:
sample positions from play, estimate each one's true near-term death probability
by rollout, and score each statistic by how well it orders those positions. AUC is
invariant under monotone transforms, so it scores the *statistic* and says nothing
about the cap or the weight -- and it predicts that two candidates related by a
monotone map must tie, which is why counting the pieces that can clear and
computing the probability that none of three can are the same candidate here.

Read with the power it has, it makes one point rather than a ranking. Above 28
squares the death rate over twelve sets is 0.2%, so 500 positions at 32 rollouts
buy about 35 deaths and `sd(AUC)` near 0.07 -- the whole observed spread, 0.73 to
0.90, is one standard error wide and the ordering within it is not resolvable.

What is larger than that: the **shipped evaluation scores 0.90 and plain occupancy
0.83**, at or above every clear statistic tried. Yet a flat charge on occupancy,
with the same gate and magnitude, measures 27% *worse* than not having the term at
all. Whatever the clear-opportunity term is contributing, it is not a better
forecast of death. An evaluation inside a search is not asked to rank positions in
general; it is asked to rank the siblings the search is choosing between, and
these are different questions. The same lesson came out of the n-tuple work, where
sibling ordering accuracy predicted play strength and neither R-squared nor
agreement on random pairs did.

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

## The three numbers, swept

The gate, the cap and the piece filter were guesses, and the note here used to
say that 41% was therefore more likely a floor than a ceiling. That was wrong,
and the way it is wrong is worth more than a better number would have been.

Eighteen arms, 200 deaths each, one axis at a time from the committed point,
against a control of 47,366:

| axis | arm | mean game |
|---|---|---:|
| gate | 20 / 24 / 27 | 76,798 / 68,019 / 76,368 |
| gate | **30** / 33 / 36 | **64,030** / 63,645 / 68,284 |
| cap | **15** | **51,117** |
| cap | 20 / **30** / 40 / 60 | 63,689 / **64,030** / 60,529 / 58,488 |
| pieces | 1-5 / **3-5** / 4-5 / 3-4 | 57,608 / **64,030** / 74,515 / 75,388 |
| weight | 130 / **200** / 170 / 250 | 66,165 / **64,030** / 72,429 / 59,314 |

Seventeen of the eighteen fall in one band around 67,000. At 200 deaths
`sd(log ĥ)` is 1/√200 = 7.1%, so a pair of arms is separated only past about 20%,
and the whole 58.5k-to-76.8k spread is about what two standard deviations buys.
Every gate from 20 to 36, every cap from 20 to 60 and every weight from 130 to
250 is, on this evidence, the same point.

The piece filter is the exception, and only because it was followed up. On this
screen it looked like noise too — the three-to-four filter was the *highest* arm
here at 75,388 and confirmed as the *worst* of the three at 1200 deaths. Four-to-
five was the one that replicated. Nothing in this table could have told them
apart; the section above did that, at six times the exposure.

The one arm that leaves the band is `cap 15`, at 51,117, and it leaves for a
mechanical reason rather than a tuning one: a crowded board typically already has
fifteen or more ways to clear, so a cap there charges almost nothing and the term
is switched off. It lands near the control, which is what being switched off
looks like.

So the term is insensitive to how it is parameterised over roughly a factor of
two in every direction. The gain is carried by the mechanism, not by the numbers,
which is a better property to ship than three values that had to be right. The
gate stays at 30 on cost grounds alone -- gate 20 measured no better and cost 13%
of throughput, 34,373 moves a second against 39,949.

Two notes on method. `pieces 3-9` returned bit-identical to `pieces 3-5` -- same
201 deaths, same exposure, same mean -- because the largest piece in the game is
five squares, which incidentally confirms the harness is deterministic at a fixed
seed base. And reading a ranking off a screen this coarse is a mistake that is
easy to make twice: the gate looked monotone through three arms before arm four
contradicted it, and both narrow piece filters looked like a real effect before
the noise floor was worked out.

## What the corpus got right and wrong, again

The pairs found the mechanism and were wrong about the magnitude, which is the
same pattern as `eval-rework-probe.md`. At weight 600 the term fixes four of the
six crowded pairs; at weight 200 it fixes two — and 200 plays 46% better than
600. Even with pairs labelled by survival, pass rate keeps pointing past the
optimum. Use them to find the mechanism and the hazard to set the number.

## What is left before this can ship

* **Make the weight tunable** — it is a compile-time constant here, and belongs
  in `EvalWeights` as a fourteenth slot so the fitness tooling can reach it.
* **Re-tune the other twelve.** They were fitted without this term and the
  balance has moved. In particular the 46,962 figure recorded in `eval.h`
  describes an engine that does not include this.
* **Regenerate the committed WASM**, which `check-wasm.yml` rebuilds and
  compares.
The reference evaluation in `tests/eval-test.cpp` now carries the term as well,
written against a plain 9×9 array rather than through the placement iterator the
evaluation uses, so the two implementations are independent. All seven tests pass
with the option on. The four knobs moved to `eval.h` so both implementations read
the same numbers.

## Reproducing

```bash
cmake -S engine/cpp -B /tmp/on -DCMAKE_BUILD_TYPE=Release -DBLOKIE_CLEAR_OPPORTUNITY=ON
make -C /tmp/on fitness -j
```

The weight, gate and cap are `BLOKIE_CLEAR_OPPORTUNITY_WEIGHT`, `_GATE` and
`_CAP` in `eval.cpp`, overridable from the compiler command line, which is how
the sweep above was run.
