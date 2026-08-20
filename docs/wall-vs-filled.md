# A wall is not a filled square

A filled neighbour can be cleared away. The edge of the board cannot. The
jaggedness features did not draw that distinction, and in one case they drew it
backwards:

* **Cornered empty ignores wall corners entirely.** The mask is
  `blocked_up_left - (row(0) | column(0))`, which subtracts exactly the squares
  where a wall forms the corner. So an empty square wedged against the rim by one
  filled neighbour -- a corner that no clear can ever open -- is charged **nothing**.
* **Squashed empty splits on the wrong thing.** It asks whether the *square* sits
  on the rim, not whether the thing blocking it is a wall. A square on the top row
  squashed between two filled neighbours to its left and right is charged the same
  3386 as one squashed between the wall above it and a filled square below.

Four weights free those distinctions. All four default to zero, so the evaluation
starts bit-identical to the one that shipped -- checked over 20,605 boards
spanning occupancy 1 to 78 plus boards from real play.

| weight | what it charges | fires per board |
|---|---|---:|
| `SQUASHED_AGAINST_WALL` | opposite pair, one side the wall | 1.82 |
| `CORNERED_ONE_WALL` | adjacent pair, one wall and one filled square | 7.91 |
| `CORNERED_TWO_WALLS` | adjacent pair, both walls: an empty board corner | 2.50 |
| `THREE_SIDED_SHALLOW_ESCAPE` | shut in on three sides, the open side running off the board two steps on | 0.07 |

The frequencies matter as much as the ideas. `CORNERED_ONE_WALL` fires 7.91 times
a board against 3.24 for cornered empty, so the same weight would be more than
twice the charge; the arms below are calibrated to contribute about a quarter of
what cornered empty contributes, rather than to a round number.

## Three of the four are worth nothing

96 chains of 600,000 measured moves an arm, 25-move burn-in, seed base 20260902,
against a baseline of 678 deaths and 84,956 moves a game.

| arm | deaths | mean game | vs shipped |
|---|---:|---:|---|
| shipped | 678 | 84,956 | |
| squashed against wall, 3000 | 685 | 84,088 | HR 1.010, z = +0.2 |
| cornered one wall, 700 | 685 | 84,088 | HR 1.010, z = +0.2 |
| **cornered two walls, 2000** | **646** | **89,164** | HR 0.953, z = -0.9 |
| three-sided dead end, 20000 | 770 | 74,805 | HR 1.136, z = +2.4 |

The two nulls are the interesting failures, because the argument for them was the
better one. A corner made with a wall really can never be freed, and it really is
unpriced today, and charging it is worth nothing at this magnitude.

The dead-end arm is the fourth time in this work that a large charge on a rare
configuration has made play worse rather than more careful, after the convex arm
and the corner-first arm in `empty-patterns.md` and the steep room table in
`three-way-fill.md`. At 0.07 firings a board it cannot contribute much even at the
weight cap, so this is not evidence that the shape is harmless -- it is evidence
that pricing it is.

## The one that worked: charge an empty board corner

A dose-response, same seed base:

| weight | deaths | mean game | vs shipped |
|---:|---:|---:|---|
| 0 | 678 | 84,956 | |
| **1000** | **637** | **90,424** | HR 0.940, z = -1.1 |
| 2000 | 646 | 89,164 | HR 0.953, z = -0.9 |
| 4000 | 675 | 85,333 | HR 0.996, z = -0.1 |
| 8000 | 804 | 71,642 | HR 1.186, z = **+3.3** |

Unimodal, with a peak near 1000 and a significantly harmful tail. The harmful end
is what makes the left side credible: it proves the weight moves play, so the
improvement is not noise around an inert parameter.

Confirmed on three further seed bases, each with its own baseline arm, none of
which any parameter was chosen on:

| seed base | baseline | candidate | HR |
|---|---:|---:|---:|
| 20260902 (chosen on) | 678 deaths, 84,956 | 637, 90,424 | 0.940 |
| 20260921 | 677, 85,081 | 644, 89,441 | 0.951 |
| 20260935 | 670, 85,970 | 624, 92,308 | 0.931 |
| 20260949 | 638, 90,282 | 612, 94,118 | 0.959 |
| **pooled** | 2663 deaths | 2517 deaths | **0.945, z = -2.03** |

Four seed bases, four hazard ratios below one, spread 0.931 to 0.959. Pooled that
is **a 5.8% longer game**, 95% CI 0.895 to 0.998.

**Read the significance carefully.** Pooled over all four the interval excludes
one, but only just, and one of the four is the seed base the weight was chosen on.
On the three it was not chosen on, the estimate is **HR 0.947, z = -1.69**, whose
interval includes one. This weight was also the best of about twenty arms tried
across a day, and one arm in twenty at p = 0.04 is roughly what chance supplies.

What argues against that being the whole story: the effect size barely moves
between seed bases, all four point the same way, and the dose-response has a
significant harmful tail, which a spurious parameter does not.

## Why an empty board corner is worth charging

The weight is not a shape penalty. There are four board corners, so it is close to
a positional preference for filling them, and that makes it unlike everything else
in the evaluation.

The mechanism is measurable. On an empty board, 34 placements cover a corner cell
against 184 for the centre, and 13 of the 47 pieces can never cover a corner at
all. So a corner is the cell most likely to end up stranded, and it is cheapest to
fill early, while pieces still have room to reach it. `empty-patterns.md` reaches
the same measurement from the other side and warns about the trap next to it:
charging a *walled-in* corner more than an interior hole costs 36% to 58%,
because the evaluation prices what creating a hole costs rather than how hard the
hole is to fill. Charging an *empty* corner is the opposite intervention -- it
gets the square filled before it can become a hole at all -- and it is the one
that measures better.

## Reproducing

```bash
cmake -S engine/cpp -B /tmp/b -DCMAKE_BUILD_TYPE=Release && make -C /tmp/b fitness -j
/tmp/b/fitness 96 --chain-moves 600000 --burn-in 25 --seed-base 20260902 \
  --weights 1358,524,6540,4450,18185,2665,204,908,1776,3386,1607,3067,200,335,0,0,1000,0
```

The four new weights are slots 14 to 17 and all default to zero; setting them to
zero is an exact control, which is how every baseline above was measured. Note
that `--weights` now requires exactly as many values as the evaluation has
weights, rather than padding a short vector with zeros -- a vector written down
before these four were added is short, and would silently have been a different
evaluation.
