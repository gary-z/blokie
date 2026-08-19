# One signal for how much room an empty square has, and why it lost

An experiment: delete jaggedness, the three-bar fit, cornered empty and squashed
empty -- four features and six weights -- and put one signal in their place. For
every empty square, count the **placements** of a three-square piece that still
cover it.

Six pieces, two bars and four L shapes, with the diagonal staircases left out on
purpose. A square can be any of a piece's three cells, so the count runs **0 to
18**, and both ends occur in play. Nothing special happens at the board edge: a
placement that would run off simply does not fit, so rim and corner squares have
fewer ways without a rule saying so. The signal reads everything within two steps,
which is what a three-square piece can reach, so unlike a count of blocked sides
it can tell a hole with a dead end from a hole that opens out.

The diagonals are excluded because they cannot cover **24%** of open squares even
on a healthy board -- a diagonal triomino needs a three-cell staircase -- so
counting them adds noise rather than caution. Measured in `empty-patterns.md`.

## It starts as a least-squares fit to what it replaces

For every empty square on boards from real play, the count was tabulated against
the total charge the four removed features put on that same square. The
conditional mean is the closest this shape can start to the evaluation it
replaces, and it is strikingly clean -- monotone, and **exactly zero past twelve
placements**, because beyond that the four features charged a square nothing at
all:

| placements left | share of squares | mean charge from the four features |
|---:|---:|---:|
| 0 | 0.29% | 36,755 |
| 1 | 0.59% | 22,470 |
| 2 | 1.47% | 21,076 |
| 3 | 1.34% | 16,257 |
| 4 | 2.10% | 12,084 |
| 5 | 8.63% | 9,706 |
| 6 | 5.09% | 9,252 |
| 7 | 2.45% | 8,385 |
| 8 | 4.55% | 4,768 |
| 9 | 12.19% | 4,492 |
| 10 | 14.83% | 4,192 |
| 11 | 6.66% | 3,598 |
| 12 | 3.77% | 2,784 |
| 13 to 18 | 36.1% | 0 |

Over 102,326 squares the four features have mean charge 4,661 and standard
deviation 5,585, and the count explains **R-squared = 0.857** of that variance.
So one number recovers 86% of what four features were saying. That is the reason
the experiment looked worth running.

## It loses by a factor of two and a half

96 chains of 600,000 measured moves an arm, 25-move burn-in, seed base 20260902 --
the same exposure and seed base as every arm in `empty-patterns.md`, against the
shipped baseline that reproduced exactly twice at 678 deaths.

| arm | deaths | mean game | vs shipped |
|---|---:|---:|---|
| **shipped, four features** | 678 | **84,956** | |
| room, tail smoothed | 1694 | 34,002 | HR 2.499, z = +20.1 |
| room, fitted table | 1859 | 30,984 | HR 2.742, z = +22.5 |
| room, low end doubled | 8135 | 7,081 | HR 11.998, z = +62.2 |

Two smaller things fall out. Smoothing the cliff from 2,780 at twelve placements
straight to zero at thirteen is worth about 10% of the gap, which says the cliff
was an artefact of fitting rather than a real discontinuity. And doubling the
charge on the squares with least room is a **twelvefold** disaster -- the third
time in this work that amplifying a penalty on a rare bad configuration has
backfired, after the convex arm and the corner-first arm. Over-charging a rare
shape does not add caution; it reorders the common cases, which is where nearly
all the moves are.

## Why 86% of the charge is nowhere near 86% of the value

Both evaluations can be computed in one binary, since every other term is shared:
the new one comes from the engine, the old one is that minus the room charge plus
the four features. Run over 3,750 sibling groups -- every placement of one piece
from a board in real play, which is exactly the choice the search makes:

| | |
|---|---:|
| variance of the four features' charge explained by the count | **85.7%** |
| sibling **pairs** ordered the same way | 83.5% |
| groups where it picks the **same best placement** | **66.0%** |

**An almost 86%-accurate account of the evaluation's level disagrees with it about one
move in three.** That is the whole result, and it is not a paradox. Siblings are
one placement apart, so their total charges are close together; what separates
them is the fine structure, and the fine structure is exactly what a conditional
mean averages away. A residual of 2,112 per square sounds small against a mean of
4,661 and is enormous next to the gap between two siblings.

So the four features are not four bad readings of one quantity, even though 86% of
their charge is predictable from that quantity. Jaggedness knows **which** sides
are blocked and whether the block falls on a 3x3 cube boundary; the room count is
blind to both, and that blindness is most of the ordering. The complement holds
too, from `empty-patterns.md`: within a single blocked-sides pattern the room
count runs across almost the whole range, so the geometry cannot see the room
either. The two are genuinely complementary, and the evaluation was using the
geometry.

The obvious follow-up, untested: **add** the room count alongside jaggedness rather
than in place of it, starting the new weights at zero so the arm begins exactly at
the shipped evaluation. That is the one form of this idea the measurements above
actually support.

## Reproducing

The branch is `experiment/three-way-fill`. It is not a candidate for merging: it
deletes four features that pay for themselves and measures 2.5x worse.

`getThreeWays(ways)` is the table, `weights[14 + ways]`. Slots 1, 2, 3, 5, 8 and 9
are the ones the deleted features used and are unread. The count is built by
adding eighteen masks a bit-plane at a time -- `BitBoard::operator^` was added for
it -- and all twelve neighbour shifts it needs were already being computed for the
deadly-piece term. The reference evaluation in `tests/eval-test.cpp` counts the
same thing from shape offsets instead of masks, so the two implementations are
independent, and the sweep there exercises every weight in isolation.
