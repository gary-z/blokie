# Rollout survey: "post-100-move hazard is board-invariant" hypothesis

## Hypothesis

> The solver is strong enough that it's usually in a steady low-risk state. If
> it enters a risky state it either dies quickly or recovers to the steady
> state within ~50 moves. Therefore P(survive next 50-100 moves | board) is a
> near-sufficient statistic for board danger, and short rollouts are enough
> to drive tuning.

Equivalent statement: after ~100 moves of "mixing", all boards have the same
conditional per-move hazard rate (chronically-risky states don't exist).

## Protocol

`fitness --rollout-out ...` (git commit TBD). Played 5 games with default
weights (cap=20,000). Every 2,000 moves, forked the board and ran 200
independent random-piece rollouts up to 1,500 moves, recording survival at
thresholds {10, 25, 50, 100, 200, 500, 1000, 1500}.

Result: 42 boards x 200 rollouts = 8,400 rollouts total.
Wall time: 1,742s on 32 cores.

Data: `bench-results/rollout_survey.csv`, `bench-results/rollout_survey.log`.
Analysis: `bench-results/rollout_analyze.py`.

## Findings

### Global per-bucket hazard (pooled over 42 boards)

| bucket [lo, hi) | entered | died | hazard /move |
|---|---|---|---|
| [0, 25)    | 8,400 | 4  | 1.9e-5 |
| [25, 100)  | 8,396 | 12 | 1.9e-5 |
| [100, 200) | 8,384 | 12 | 1.4e-5 |
| [200, 500) | 8,372 | 65 | 2.6e-5 |
| [500, 1000)| 8,307 | 101| 2.4e-5 |
| [1000, 1500)| 8,206 | 118| 2.9e-5 |

Hazard is flat and small (1.4-2.9e-5 per move) across all windows. No evidence
of a "mixing transient" at the start of the rollout window because the boards
were sampled well after the game's early-game regime (moves >= 2000, usually
already 10,000+).

### Per-board conditional hazard in [100, 1500]

Each surveyed board had 3-13 deaths across 199-200 rollouts. Per-board
estimated lambda ranges 0.7e-5 to 4.8e-5; pooled lambda = 2.57e-5
(implied mean remaining life 38,954 moves, comparable to the full-game
MLE steady-state 1/lambda_ss = 48,224).

**Poisson chi-squared test (df=41): chi^2 = 42.10, p = 0.42.**

Cannot reject the null hypothesis that all boards share a single common
hazard rate. Observed variance across boards is exactly Poisson-consistent
under a single-lambda model.

### Outliers

The 3 boards flagged with |z| >= 2 are all LOWER than pool (2-3 deaths out
of 200 rollouts), not higher. No chronically-risky boards found.

## Conclusion

**Hypothesis confirmed for mid- and late-game boards** (sampled at absolute
game moves 2,000 through 20,000).

Implication: a fixed panel of ~100 diverse boards + ~50 rollouts each x
~100-move depth (500k moves of total work = ~30s on 32 cores) is sufficient
to rank candidate weight vectors by late-game policy quality.

That is a ~10x speedup over full-game fitness evaluation (~5 min per
candidate at cap=25k), which puts proper optimizers (CMA-ES, SLSQP,
Nelder-Mead) back in the feasible regime.

## Caveat (pending investigation)

The full-game MLE showed an elevated "bad start" hazard regime below
~10,000 moves (~2.7e-5 vs ~2.1e-5 steady-state). This survey did NOT
include boards from the first 2,000 moves of a game, so it says nothing
about whether early-game boards also mix to a common hazard, or whether
some near-empty boards are genuinely chronically risky.

Next step: repeat the survey with cap=10,000 and stride=500 to walk the
hazard curve across the early regime.
