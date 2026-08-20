# Pre-registration: does charging an empty board corner help?

Written and committed **before** the confirming run, because this candidate has
already been over-claimed once. At four seed bases it pooled to HR 0.945,
z = -2.03, and was reported as confirmed. Two more seed bases took it to HR 0.960,
z = -1.78, and the interval stopped excluding one. That is what a marginal
selected finding looks like when it regresses, and continuing to add seed bases
until the p-value cooperates would be the same mistake with more compute behind
it. So the rule is fixed here first.

## The candidate

`CORNERED_TWO_WALLS = 1000` -- a charge on an empty square in a board corner,
where the corner is formed by two walls and can never be opened by a clear.
Everything else is the shipped evaluation. Zero is an exact control.

## What is already measured

Six seed bases, 96 chains of 600,000 measured moves an arm, 25-move burn-in:

| seed base | baseline deaths | candidate deaths | HR |
|---|---:|---:|---:|
| 20260902 (the weight was chosen here) | 678 | 637 | 0.940 |
| 20260921 | 677 | 644 | 0.951 |
| 20260935 | 670 | 624 | 0.931 |
| 20260949 | 638 | 612 | 0.959 |
| 20260963 | 626 | 604 | 0.965 |
| 20260977 | 615 | 627 | 1.020 |

Pooled over all six: HR 0.960, 95% CI 0.918 to 1.004, z = -1.78.
Out-of-sample, the five it was not chosen on: HR 0.964, z = -1.44.

## The run

Four further seed bases -- 20260991, 20261007, 20261019, 20261033 -- each with its
own baseline arm and its own candidate arm, same 96 chains of 600,000 measured
moves, same burn-in. Eight arms, four threads each. That adds roughly 2,600
deaths per arm and takes the out-of-sample total to about 5,700 per arm, for a
standard error near 0.018 on the log hazard ratio.

## The analysis, fixed in advance

* **Primary endpoint:** the pooled hazard ratio over every **out-of-sample** seed
  base -- the nine that are not 20260902 -- inverse-variance weighted on the log
  hazard ratio, candidate against the baseline measured on the same seed base.
* **Confirmed** if the primary z is at most -2.0.
* **Refuted** if the primary z is at least +2.0, or if the 95% interval excludes
  a 2% improvement, which would mean any real effect is too small to be worth a
  weight.
* **Unresolved** otherwise, and reported as unresolved rather than as either.
* **No further data on this candidate after this run**, whichever of the three it
  lands on. If it comes out unresolved, that is the answer: an effect this size
  is not separable from noise at a cost anyone should pay.

The all-seed pooled figure and the per-seed table are reported as secondary,
because the discovery seed base cannot be part of the test that the discovery was
real.
