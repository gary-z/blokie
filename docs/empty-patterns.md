# Pricing the ways an empty square is walled in

The evaluation used to charge an empty square through three overlapping
features, and they could all charge the same square at once:

* **transitions** -- one charge per blocked side, where a side is blocked when
  the neighbour is filled or off the board;
* **cornered empty** -- one charge per *adjacent pair* of sides both blocked by
  real filled squares;
* **squashed empty** -- one charge per *opposite pair* of blocked sides, at one
  weight for a square inside the board and another for one on the rim.

The cost of a shape was therefore a sum of terms rather than a number. An empty
square with three filled neighbours accumulated two cornered pairs and one
squashed pair, so its price was pinned at `2 * cornered + squashed` and could not
be moved without moving the price of every two-sided square along with it. There
was no way to say what a three-sided hole is worth.

Now each pattern has its own weight. A side is blocked when the neighbour is
filled *or* off the board -- the board edge walls a square in exactly as a filled
square does -- and the patterns are the orbits of the square's own symmetry
group, which is what keeps the evaluation invariant under flips and transposes as
it was before.

| position | patterns | weights |
|---|---|---|
| interior, four real neighbours | none, one, two adjacent, two opposite, three, four filled | 6 |
| rim, one wall | none, along, inward, inward and along, both along, all three | 6 |
| board corner, two walls | none, one, both | 3 |

For a rim square, `inward` is the neighbour away from the wall and `along` the
two running beside it, which are interchangeable. `docs` and the reference
evaluation in `tests/eval-test.cpp` use those names; the weights are keyed by
`EvalWeights::Slot` so that a slot number appears in exactly one place.

## The starting point is the old evaluation exactly

The fifteen default values are the charge each pattern used to accumulate:

| pattern | default | where it comes from |
|---|---:|---|
| interior two adjacent | 6540 | 1 cornered |
| interior two opposite | 524 | 1 squashed |
| interior three | 13604 | 2 cornered + 1 squashed |
| interior four | 27208 | 4 cornered + 2 squashed |
| edge inward | 3386 | 1 rim squashed |
| edge inward and along | 9926 | 1 cornered + 1 rim squashed |
| edge both along | 3386 | 1 rim squashed |
| edge all three | 19852 | 2 cornered + 2 rim squashed |
| corner one | 3386 | 1 rim squashed |
| corner both | 13312 | 1 cornered + 2 rim squashed |
| the three `no filled neighbour` patterns | 0 | charged nothing before |

So the reparameterisation ships switched off, in the sense that it starts at the
only point the old parameterisation could express. This was checked rather than
argued: the evaluation is **bit-identical over 20,605 boards** spanning every
occupancy from 1 to 78 plus boards taken from real play, and the sample exercises
all fifteen patterns, the rarest of them 5,105 times. Three slots -- 1, 2 and 9 --
were kept by the patterns that inherited their values, so every recorded weight
vector and every reference to `weights[13]` still means what it said.

Two things fell out of writing it down, and both are arithmetic rather than
measurement.

**Patterns that are equally dead are priced 2x to 4x apart.** Group the fifteen
by how many sides are *open*, which is what decides how many of the 47 pieces can
ever fill the square:

| open sides | patterns and their old prices |
|---|---|
| 0 | interior four **27208**, edge all **19852**, corner both **13312** |
| 1 | interior three **13604**, edge inward+along **9926**, edge both along **3386**, corner one **3386** |
| 2 | interior two adjacent **6540**, edge inward **3386**, interior two opposite **524**, edge along **0**, corner open **0** |

The old parameterisation charged an interior hole with no open side twice what it
charged a board-corner one, for no reason except that the corner accumulates one
cornered pair where the interior accumulates four. The 1-open-side row spreads 4x
the same way.

**An earlier version of this note claimed a hole with no open side can only be
filled by the single-square piece. That is false**, and the arm built on it is the
worst result below. Two of the eight three-square pieces are diagonals, and a
diagonal piece can cover a cell whose four orthogonal neighbours are all filled,
provided the diagonal beyond them is open. Measured on boards from play, 43% of
interior holes with all four neighbours filled can still be covered by some
three-square piece. "No open side" is not the same as sealed, and the number of
sides is a poor proxy for how fillable a hole really is -- see the section on
counting pieces below.

**The three `no filled neighbour` patterns are nearly collinear with occupancy**,
because the fifteen counts sum to the number of empty squares. They are in the
list for completeness and default to zero; they are the least likely of the
fifteen to pay for tuning, and were left out of the sweeps below.

## What it costs to run

Fifteen patterns take more work to separate than three overlapping features took
to add up, and the cost is measurable by timing the same fixed-seed run against a
build of the previous evaluation -- a fair comparison precisely because play is
bit-identical, so both builds do exactly the same work.

The first working version cost **13% to 18%**. Two changes brought that down, and
neither touches what the evaluation computes:

* **Ask all four walls at once.** The first version ran a six-mask lambda once per
  wall. What the rim classes actually need to know is whether the inward side is
  blocked and whether none, one or both of the along sides are, and those three
  facts can be built for the whole rim in one pass. The board corners reuse the
  same two direction masks.
* **Skip a pattern priced at zero.** Five of the fifteen are unpriced by default,
  and a zero-weighted pattern cannot change the result, so its mask is never
  built. The test is on a weight and not on the board, so it is the same branch on
  every call.

Together those leave **about 5%** -- 4.8% on medians and 6.0% on minima, six runs
each way on an idle machine. Nearly all of it is the first change; the second is
below this measurement's noise floor and is kept because it costs nothing and
because it also pays off for anyone who switches a pattern off deliberately.

A caution on measuring it, since it caught me once. Run-to-run spread on a busy
machine is comparable to the effect being measured: one pass reported +12.7%
where a cleaner one reported +5%, for the same two binaries, and the entire
difference was a single fast outlier in the *baseline* -- the candidate's own time
had not moved at all. Wait for the load average to settle, take several runs each
way, and read the raw times rather than the ratio.

It still buys nothing on its own -- it is the price of being able to ask the
question below. Nothing here changes what the engine plays.

## Tuning the twelve new free variables

`weight-screen` could not resolve them. Every one of fifteen arms came back
inside `|t| = 1.5` with relative swings of 30% to 70%, which is what an
uninformative screen looks like rather than a flat optimum. The reason is
visible in the tool's own output: its base window risk is now **3.1e-05** where
`docs/weight-screen.md` calibrated it at **6.7e-04**, a twenty-fold collapse,
because the engine that carries the clear-opportunity term escapes crowded boards
far faster than the one the screen was built against. There is much less risk per
sampled board to difference, so the screen needs re-calibrating -- more boards, a
higher occupancy floor, or a longer horizon -- before it can screen anything
again.

So the arms below were chosen on the structure above rather than on the screen,
which is also what `docs/weight-screen.md` says to do: use it to reject, not to
pick.

Eleven candidates went to full games, all on 96 chains of 600,000 measured moves
with a 25-move burn-in, seed base 20260902. Every arm including the baseline went
through `--weights`, so all of them ran one code path; the generic evaluator was
separately checked against the default one over 18,000 moves and agrees on every
board and every score. The baseline reproduced exactly -- 678 deaths, twice, on
two different binaries -- so it is a fixed reference rather than a re-measured one.

Nine of the arms move the ordering between the three position classes while
preserving each group's total charge over the sampled boards, so they test the
ordering and not the magnitude.

| arm | interior : edge : corner | deaths | mean game | vs shipped |
|---|---|---:|---:|---|
| **baseline, shipped** | 2.04 : 1.49 : 1 | 678 | **84,956** | |
| steeper | 5.41 : 2.27 : 1 | 739 | 77,943 | HR 1.090, z = +1.6 |
| steeper | 3.5 : 1.9 : 1 | 774 | 74,419 | HR 1.142, z = +2.5 |
| steeper | 8 : 3 : 1 | 793 | 72,636 | HR 1.170, z = +3.0 |
| equalised | 1 : 1 : 1 | 799 | 72,090 | HR 1.178, z = +3.1 |
| convex, magnitude raised | 1 : 1 : 1 | 990 | 58,182 | HR 1.460, z = +7.6 |
| corner-first | 1 : 1.5 : 2 | 1063 | 54,186 | HR 1.568, z = +9.1 |
| corner-first | 1 : 2.27 : 5.41 | 1632 | 35,294 | HR 2.407, z = +19.2 |

**The shipped ratio is a local optimum, and not a shallow one in both
directions.** Going the other way -- charging a walled-in board corner *more* than
an interior hole, which is what "a corner hole is the worst kind" implies -- costs
36% and then 58%. Making the classes equal costs 18%. Going further in the
shipped direction is only mildly worse, and the nearest arm to it, 5.41 : 2.27 : 1,
is the one arm here that is not separated from the baseline at all. So the ragged
2x-to-4x spread is not an artefact to be tidied; it is where the optimum is.

Two arguments were offered for the corner-first direction and both were measured
rather than assumed, which is the only reason it is clear which one was wrong. A
corner cell *is* the hardest cell to fill: on an empty board 34 placements cover a
corner against 184 for the centre, and 13 of the 47 pieces can never cover a
corner at all. But the evaluation does not price how hard a hole is to fill; it
prices what creating one costs, and sealing a centre cell destroys 184 future
placements where sealing a corner destroys 34. The same measurement, read for the
right question, gives the ordering that shipped.

## Counting the pieces that can still fill a hole

The number of blocked sides is a poor proxy for fillability, and this is
measurable. There are eight three-square pieces -- two bars, two diagonals and
four L shapes -- and asking how many of them can still cover an open cell gives a
quantity the fifteen patterns cannot express, because within a single pattern the
count runs across nearly the whole range:

| pattern | pieces that can still fill it | mean |
|---|---|---:|
| interior two adjacent | 1 to 8 | 6.73 |
| interior two opposite | 1 to 7 | 4.93 |
| edge along | 1 to 8 | 6.26 |
| interior three | 0 to 5 | 3.27 |
| interior four | 0 to 2 | 0.79 |

That last row is the one that matters most, because it contradicts the idea that a
four-sided hole is sealed: **43% of interior holes with all four neighbours filled
can still be covered by a three-square piece**, because a diagonal reaches past
the orthogonal neighbours. Among fully walled holes the mean runs edge 0.08 <
corner 0.13 < interior 0.79, a third ordering again.

The evaluation already asks this question of the two bars -- `THREE_BAR` charges an
open cell once per bar orientation that can no longer cover it. Extending it to
the other six pieces is two more weights, `NO_DIAGONAL_THREE` and `NO_L_THREE`,
charged per orientation exactly as the bars are. Both default to zero, so the
addition is inert until priced.

Priced, it loses:

| arm | charge per fully dead cell | deaths | mean game | vs shipped |
|---|---:|---:|---:|---|
| all six at 1300 | 7800 | 787 | 73,189 | HR 1.161, z = +2.8 |
| four L shapes at 2665 | 10660 | 916 | 62,882 | HR 1.351, z = +5.9 |
| two diagonals at 2665 | 5330 | 1030 | 55,922 | HR 1.519, z = +8.5 |

It is not monotone in the charge: the diagonal arm adds the least penalty and does
the most damage. The reason is how often each orientation fails, measured over
open cells in play:

| orientation | share of open cells it cannot cover |
|---|---:|
| L shapes | 7.5% to 7.9% |
| bars | 8.8% to 9.1% |
| **diagonals** | **24.3%** |

A diagonal triomino needs a three-cell staircase, which is rare in an ordinary
board, so "no diagonal fits here" is true of a quarter of all open cells including
perfectly healthy ones. Charging it is close to indiscriminate, and an
indiscriminate penalty does not add caution, it adds noise to the ordering the
search is choosing between. The L shapes are the most selective of the eight and
still lose, because they are strongly correlated with the bars -- a cell with no
room for a bar usually has none for an L -- so pricing them charges twice for a
signal the evaluation already carries.

So the information is real, it is orthogonal to the patterns, and charging for it
makes play worse. That is the same result as the AUC diagnostic in
`clear-opportunity.md`, where the shipped evaluation predicted death better than
every statistic that beat it as a term, and it is worth stating as a rule: an
evaluation inside a search is not a description of the board, it is an ordering
over the moves available, and a true fact about the board earns nothing until it
separates good moves from bad ones.

## Where this leaves the evaluation

Eleven arms, none better than what shipped, and the two nearest misses fail in
opposite directions. The empty-square weights are at a local optimum that is
tighter than the ragged look of the numbers suggests, and the untangling that made
them individually adjustable found nothing to adjust.

What that is worth is a diagnostic rather than a gain. The reparameterisation is
what made the 2x-to-4x spread visible, made the corner-first hypothesis testable
at all, and turned up two facts that were being asserted wrongly: that a
four-sided hole is sealed, and that the position ordering was backwards. All three
of those needed the weights separated before they could even be stated.

What is left that this parameterisation makes askable, and nobody has asked:

* The 2-open-side row, where interior two adjacent at 6540 sits against interior
  two opposite at 524 -- a 12.5x asymmetry. Those two were tunable before this
  change and are the one part of the table with an empirical justification, so they
  are the most likely place for a real gradient and nothing here touched them.
* Single weights moved by 10%. Every arm here moved several at once by 20% to 630%,
  which measures a direction and not a derivative.
* `weight-screen` needs re-calibrating before either is affordable. Its base window
  risk has fallen twentyfold since it was built -- the engine escapes crowded
  boards far faster now -- so it currently costs an hour of full games to learn
  what it used to screen in a minute. Fixing that is worth more than any single
  weight, because it is what makes a local search possible at all.
