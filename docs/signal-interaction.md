# The clear signal and the deadly signal interact, but charging the interaction does not help

The evaluation carries two crowded-board signals that fire on nearly the same
region of play: the clear-opportunity term (can any piece clear a line) and the
deadly-piece / scarcity terms (is a hard piece about to have nowhere to go). They
are combined additively. This asks whether they should instead be combined so that
one amplifies the other, and finds that the interaction is real in the data and
worthless in the evaluation.

## The interaction is real

On the leaves the search scores above the gate, the two raw signals are close to
independent -- correlation 0.18 between the clear shortfall and the deadly
scarcity. But the chance of dying within twelve sets, estimated by rollout under
the shipped policy, is supermultiplicative in their conjunction:

| | deadly quiet | deadly fires |
|---|---:|---:|
| clear available | 0.14% | 0.47% |
| no clear | 0.87% | **3.96%** |

No-clear alone multiplies the risk 6.2x, deadly alone 3.4x. If they acted
independently the corner where both fire would sit at 0.14% x 6.2 x 3.4 = 2.95%.
It is 3.96%, a third higher -- and on an additive scale, more than three times the
1.20% the marginals predict. The board that can neither clear nor safely place its
hard pieces is worse than the sum of its problems.

## Charging it does not help

An interaction term was added on top of the existing additive terms, so that a
weight of zero reproduces the shipped evaluation exactly and any gain would show as
an interior peak in the weight sweep. Three forms, 400 deaths an arm, against a
shipped baseline of 87,214 on the same seed:

| form | weight sweep -> mean game length |
|---|---|
| `missing * scarce * W` | 383 -> 88,182; 766 -> 83,547; 1532 -> 82,804 |
| `missing * scarce * crowded * W` | 21 -> 87,525; 42 -> 87,189; 84 -> 82,241 |
| occupancy switches which term leads | x1.5 -> 63,580; x2.0 -> 57,228 |

No form has a peak away from zero. Both product forms are flat where the added
charge is small and worse where it is large, which is what a useless term looks
like: it can only distort. The version that lets occupancy pick a leading term is
sharply harmful in both directions.

## Why real and worthless are not a contradiction

An evaluation inside a search is not a death predictor. The two additive terms
already steer play away from the doubly-dangerous corner; by the time the board is
scored, the search has usually declined to enter it. Re-penalising the corner
explicitly changes nothing there and instead distorts the many boards where the
product is nonzero but survivable -- both shortfalls are positive on 57% of gated
leaves, so the term is loud across ordinary crowded play rather than concentrated
on the rare killer.

This is the same lesson as three earlier results in this engine: the golden-corpus
agreement that was anti-predictive of play strength (`eval-rework-probe.md`), the
shipped evaluation scoring higher AUC for death than the statistics that beat it as
a term (`clear-opportunity.md`), and the paired window screen that ranked a change
the wrong way (`weight-screen.md`). A quantity can predict the outcome and still be
the wrong thing to add to an evaluation, because what the evaluation needs is not a
better forecast of the boards it reaches but a better ordering of the boards it is
choosing between.

The evaluation is unchanged.
