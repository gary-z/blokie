# Validation of the board-hazard probe estimator

This records the August 2026 experiment that added `fitness --probe`. It is a
measurement report, not a promise that absolute throughput transfers to another
machine. The important results are variance and cost ratios.

## Result

The estimator works. A uniform 30 probe triples per visited board reduced
compute for a fixed confidence by **2.4–3.0×** across independent seed banks.
Allocating the same average number of probes by board occupancy improved that
by another 9.2% in two replicated banks. The probe itself used about **0.37%**
of aggregate worker time. The tuned recipe is:

    ./fitness 640 --threads 32 --seed-base S --chain-moves 10000 \
        --burn-in 25 --probe-occupancy-bands 15 62 854 23 36 > run.txt

Use a fresh, recorded `S` for every independent run. Use the same seeds in two
arms when comparing weights, and pass `--paired` to `compare-probes.js`.

Use `--probe-occupancy 10 390 29` for a simpler adaptive policy that retains
most of the gain, or `--probe 30` for a deliberately untuned configuration.
The upper theoretical estimate did not survive autocorrelation. More probes
cannot average away correlated variation in the boards the policy visits.

## Environment

- Base commit: `5872344`
- Compiler: GCC 16.2.0, C++23, `-O3 -flto -march=native`
- CMake: 4.4.2
- Host: 13th Gen Intel Core i9-13900KF, 16 cores / 32 logical CPUs
- OS: Linux 6.6.87.2 under WSL2
- Validation date: 2026-08-12

Raw run files were written under `/tmp/blokie-rb-results` and are intentionally
not repository artifacts. The tables below record their command shapes and seed
bases so the runs can be reproduced.

## Correctness gates

`AI::tripleFits` was checked against
`AI::makeMoveSimpleDefault(...).evaluation != UINT64_MAX` over 10,000,000
board/triple pairs from deterministic real play. All 10,000,000 agreed. The run
included 9,743,225 boards at depth at least 1,000 and 227 boards whose dealt
triple actually killed the game.

    ./probe-test 10000000 32

The same check is available as the opt-in `probe-equivalence-10m` ctest by
configuring with `-DBLOKIE_ENABLE_LONG_TESTS=ON`. A 10,000-pair version runs in
the routine test suite.

A separate ctest runs `fitness` with and without probing for the same seeds and
requires every trajectory's move count, ending status, and seed to match. This
locks down the separate RNG stream: probes cannot consume dealt pieces.

## Unbiasedness

The first acceptance run used seed base `202608120000`, 320 chains, 40,000 measured
moves per chain, burn-in 25, and `M=30`. It observed 265 deaths over 12.8 million
moves:

| estimator | hazard | 95% interval |
|---|---:|---:|
| deaths | 2.0703e-5 | 1.8355e-5 .. 2.3352e-5 |
| probes | 2.0560e-5 | 1.9042e-5 .. 2.2078e-5 |

The probe estimate is inside the death interval, and the death estimate is
inside the chain-clustered probe interval. This passes the requested check with
more than 200 deaths.

A larger independent validation (`202608250000`, 6,400 chains × 4,000 moves)
observed 530 deaths over 25.6 million boards:

| estimator | hazard | 95% interval |
|---|---:|---:|
| deaths | 2.0703e-5 | 1.9014e-5 .. 2.2543e-5 |
| probes | 2.1592e-5 | 2.0531e-5 .. 2.2654e-5 |

Again, each point estimate is inside the other estimator's interval. The
uniform probe used 768 million sampled triples and 85.9 aggregate worker
seconds, 0.37% of the 23,317.9 worker seconds used by the run.

Measured `Var_B[p]` was stable around 3.0e-6 to 3.5e-6. The implementation uses
the finite-sample correction with denominator `M-1`; with `M=1`, that component
is not identifiable.

## Probe-count sweep

The primary bank used seed base `202608120000`, 320 chains × 40,000 moves. Cost
ratios use probe time measured inside each worker, avoiding noise from comparing
two separate wall-clock runs.

| M | raw variance gain | probe cost ratio | net speedup |
|---:|---:|---:|---:|
| 1 | 0.71 | 1.0003 | 0.71 |
| 10 | 1.94 | 1.0014 | 1.94 |
| 20 | 2.10 | 1.0025 | 2.10 |
| 30 | 2.45 | 1.0037 | **2.44** |
| 100 | 2.45 | 1.0117 | 2.43 |
| 300 | 2.51 | 1.0342 | 2.42 |
| 1000 | 2.51 | 1.1127 | 2.25 |

An independent bank (`202608150000`, 640 chains × 10,000 moves) found net gains
of 2.27, 2.99, 2.97, 3.13, and 3.08 at `M=10,20,30,50,100`. The intervals
overlap broadly. Two independent 40,000-move `M=30` banks measured 2.44× and
2.53×. This supports a broad plateau, not a precisely known optimum. Among
uniform policies, `M=30` is the recommendation because it reached the plateau
in every design while its cost stayed negligible.

### Why M=1 is not an exact variance identity

At 10,000 moves per chain, the `M=1` variance ratio was 1.05 with bootstrap 95%
interval 0.83–1.35, as expected. Two 40,000-move banks instead measured 0.71
and 0.73, with intervals just below one.

This does not indicate bias: an independent one-triple probe and the dealt
triple have the same marginal Bernoulli distribution. It exposes a flaw in the
claimed time-series sanity check. A failed dealt triple resets the game; a
failed independent probe does not. Their lagged covariances can therefore
differ even when their expectations and one-step variances agree. The empirical
speedup comparison remains the correct measurement and automatically includes
that difference.

## Chain length and burn-in

At fixed total board exposure and `M=30`, pooling repeat runs gave this
compute-normalized variance cost (lower is better):

| measured moves/chain | relative compute cost |
|---:|---:|
| 400 | 1.45 |
| 1,000 | 1.30 |
| 4,000 | **1.00** |
| 10,000 | 1.07 |
| 40,000 | 1.20 |

The seed bases by row were `202608180000` and `202608210000` (400),
`202608190000` (1,000), `202608200000` and `202608220000` (4,000),
`202608150000` and `202608230000` (10,000), and `202608120000` and
`202608160000` (40,000). Each row pooled the listed repeats at equal total
exposure.

The exact ordering is noisy, but 4,000–10,000 is a stable plateau. Very short
chains repeatedly pay burn-in; long chains provide fewer independent units and
worse scheduling.

On 16,000 identical 400-move seeds starting at `202608210000`, paired bootstrap
comparisons found that raising burn-in from 0 to 25 increased the measured
hazard by 8.0% (`p=0.0069`),
and 10 to 25 by 5.4% (`p=0.043`). Raising 25 to 50 changed it by only 2.7%
(`p=0.31`). Keep the 25-move burn-in.

## The crowded-piece scarcity decision

Both arms used the generic evaluator so specialization could not create a timing
difference. Arm A used the default `weights[12]=200`; arm B set it to zero.
Two independent paired runs used seed bases `202608170000` (320 × 40,000) and
`202608240000` (640 × 10,000), both with burn-in 25 and `M=30`.

Pooled across 960 chains, removing the term increased hazard by **17.6%**,
paired-bootstrap 95% interval **7.1% to 29.2%**, `p=0.000595`. The death-count
cross-check estimated +20.5%, 95% interval 5.3% to 37.8%, `p=0.0071`.

The independent replication demonstrates the practical value. On its own, the
probe estimator measured +18.6% (4.0% to 35.5%, `p=0.010`); deaths on the exact
same 12.8 million moves measured +6.7% (-15.7% to 35.1%, `p=0.63`). The probe
settled a decision that the death-only path still could not. Across arms and
runs, matching the probe confidence took roughly one third as much compute as
matching it with deaths.

The scarcity term helps and should remain enabled.

## Adaptive probing

The first board-only trigger was whether any of the 47 standard pieces had no
individual placement. It fired on only 0.0219% of boards.

| policy | average probes/board | net speedup, primary bank |
|---|---:|---:|
| uniform 10 | 10.000 | 1.94 |
| adaptive 10/1000 | 10.217 | 2.16 |
| adaptive 20/1000 | 20.215 | 2.37 |
| uniform 30 | 30.000 | 2.44 |

On the independent bank, uniform 20, adaptive 20/1000, and uniform 30 measured
2.99×, 2.96×, and 2.97×. Adaptive sampling did not beat the simpler uniform
policy. `--probe-adaptive` remains available for reproduction, but this trigger
is not recommended.

Occupancy is a much better allocation signal. The 25.6-million-board
`202608250000` run recorded risk and probe cost at every occupied-square count.
Boards below 29 occupied squares were 94.747% of visits but contributed only
7.51% of failures. The remaining 5.253% contributed 92.49% of failures. Probe
time rose from about 110 ns/triple on open boards to roughly 250 ns/triple on
the most crowded boards, confirming that proving non-fit is more expensive.

`--probe-occupancy 10 390 29` spends 10 probes on the first group and 390 on
the second. Its realized average was about 30 probes/board, so these are direct
same-budget comparisons:

| seed/design | uniform 30 | occupancy 10/390/29 | relative gain |
|---|---:|---:|---:|
| `202608250000`, 6,400 × 4,000 | 2.729× | 2.913× | 6.7% |
| `202608260000`, 3,200 × 4,000 | 2.661× | 2.831× | 6.4% |

For any variable-`M` policy, estimate hazard as the mean of each board's
`failures/M`, which the output records as `probe_sum_p / probe_boards`. The raw
`probe_failures/probe_draws` ratio overweights crowded boards that deliberately
receive more draws and is only a diagnostic count.

Pooling the 9,600 paired chains, the variance ratio was 1.066, with paired
bootstrap 95% interval 1.027–1.109. Alternatives 10/235 at threshold 27 and
22/387 at threshold 32 measured 2.837× and 2.832× on the held-out bank. The
near-identical results show a plateau rather than a sharply tuned magic
threshold.

A three-band allocation derived from the occupancy table spends 15 probes
below 23 occupied squares, 62 from 23 through 35, and 854 at 36 or above. It
realized 30.09 and 30.01 probes/board in the two banks. Net speedups were
3.000× and 2.872×, compared with 2.913× and 2.831× for two bands. Pooling all
9,600 matched chains, three bands reduced variance another 2.44%, paired
bootstrap 95% interval 1.03%–3.89%. Relative to uniform 30, the pooled gain was
about 9.2% at effectively identical cost.

The three-band policy is the tuned recommendation. The replicated two-band
policy is a reasonable lower-complexity fallback; its wider threshold plateau
also makes it the safer choice after a large change to the evaluator.

As a portability check, the same three-band settings were applied to the
scarcity-disabled evaluator on the 640 × 10,000 `202608240000` bank. They
averaged 30.58 probes/board and improved net speedup from 3.044× to 3.275×
(+7.6%). The paired variance-gain interval was -1.0% to +16.0%, so this is
supportive evidence rather than another conclusive replication.

## Assumptions and limits

- Chains, not boards, are treated as independent. Hundreds of chains are
  needed to estimate a variance ratio; its bootstrap interval remains wide.
- Burn-in changes which board distribution is measured. The paired sweep
  supports 25 moves for this policy and piece distribution, not universally.
- The probe estimates the hazard on boards generated by the tested policy. It
  does not provide common random boards across policies, and it does not remove
  board-to-board or trajectory variation.
- The occupancy allocation is always unbiased because it depends only on the
  current board, but its 9.2% efficiency gain was tuned and validated on the
  current default policy. Recheck the allocation after a large policy change.
- The 2.4–3.0× speedup is an empirical range, not the 7× independent-board
  ceiling. Tuning `M` until one noisy bank looks best overstates performance.
- Absolute throughput and wall time are machine-specific; compare variance per
  aggregate worker time when moving the recipe to another host.
