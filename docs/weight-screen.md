# Screening a weight change in a minute instead of an hour

`weight-screen` compares two weight vectors by paired window risk. It exists
because measuring what we actually care about — the hazard, the rate at which
games end — is far more expensive than it looks, and most weight changes are not
worth an hour of machine time to reject.

## Why the obvious approaches are too slow

Deaths are rare. A game lasts about 81,000 sets of three pieces, so a run long
enough to see 1500 deaths is 120M moves and most of an hour, and that only pins
the hazard to about 3.7%. Resolving a 5% change needs four times that.

`fitness --probe` helps less than it should. It prices each board by sampling
hands that cannot be placed, which removes the coin flip from the estimate and
keeps only the board-to-board variation. Measured on the same run: the death fit
gave 22.4% relative error from 20 deaths, the probe estimate 13.9%. That is a
**2.6x** gain, not the order of magnitude the Rao-Blackwell argument suggests,
because what dominates is clustering within a chain rather than the flip.

## What this measures instead

The hazard is the mean of `p(board)` over the boards a policy visits, where
`p(board)` is the chance a random hand of three cannot be placed. **`p` does not
depend on the weights at all.** Only where the policy goes does.

So run both vectors from the *same* sampled boards on the *same* piece streams,
and difference the risk they accumulate. The two walks stay together until the
first disagreement, and everything they share cancels. A candidate identical to
the base reads exactly `0.000e+00`, which is the tool's null test.

```
weight-screen --candidate 1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,75,200
```

A positive delta means the candidate is riskier, which is worse.

## What it costs and what it can see

About a minute for 960 boards at eight streams, which is roughly 12x the
information per second of `fitness --probe`.

Its resolution depends on the size of the change, because the pairing is what
buys the precision. A small change keeps the two walks together and the standard
error is around 2% of the risk level; deleting a whole term makes them diverge
immediately and the error grows to 11%. So it is sharper on the changes worth
screening than the calibration below suggests.

Calibration, from the one case with a known answer — removing the
clear-opportunity term, worth 74% in real hazard:

| | |
|---|---|
| window risk | +42% |
| t | 3.9 |
| wall time | 52s |

Risk changes are roughly half the size of the hazard change they imply. Treat the
number as ordinal.

## Two things it gets wrong if you let it

**It cannot see past its window.** Twelve sets of play from a crowded board says
nothing about a weight that trades near-term safety for long-term shape. Every
finding has to be confirmed on full games before it means anything.

**Crowded boards arrive in runs.** One dangerous excursion yields a dozen
consecutive boards above 36 squares, and counting those as a dozen independent
observations understates the standard error badly. Hence `--stride`, which keeps
one qualifying board in N, collection from one independent game per thread, and a
standard error clustered on the starting board.

## What it found

Every one of the fourteen weights, halved and doubled, against the shipped
vector. Nothing cleared `|t| = 2.1` — and across fourteen weights, about one hit
that size is expected by chance. The largest gradient, on the aligned-transition
weight, **reversed sign** when it was re-measured on independent boards, which is
what that expectation looks like in practice.

Three results were worth keeping:

* **The transition weight is pinned.** Halving it costs 14.5% and doubling it
  costs 14.7%, so 4450 sits at a local optimum. The jaggedness signal was not
  displaced by the clear-opportunity term.
* **The deadly-piece weight is saturated.** Halving 18,185 barely changes
  anything, because at that size the penalty rarely reorders moves. It is a
  "large enough" weight, not a tuned one.
* **The crowded-scarcity weight wants to come down**, from 200 to about 75, on a
  smooth unimodal curve. That is the one term whose job the clear-opportunity
  term partly took over, and it is the only weight in the vector that moved.
  Removing it entirely is *worse* than halving it, so the overlap is partial.

The global scale needs no attention: scaling all thirteen original weights by a
constant is arithmetically the same as scaling `weights[13]` the other way, and
both directions of that measured worse.
