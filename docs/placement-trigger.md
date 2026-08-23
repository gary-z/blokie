# Gating the clear lookahead on legal piece placements

The clear-opportunity lookahead used to run only at 30 or more occupied squares.
That is cheap, but occupancy cannot distinguish an orderly board with plenty of
room from a fragmented board with the same number of filled cells.

Replacing that trigger with the number of legal placements of the four- and
five-square pieces improved the board-hazard estimate on two independent seed
banks. The evaluator now runs the clear lookahead when those pieces have at most
686 placements in total. The rest of the clear-opportunity term is unchanged.

```
crowded = max(0, occupied - 20)
if crowded > 0:
    placements = total legal placements of every 4..5 square piece
    if placements <= 686:
        ways = number of 4..5 square pieces that could clear
        score += max(0, 70% * occupied - ways) * crowded * weight
```

The `crowded > 0` check is not a second behavioral gate: the score multiplier is
zero below it. It only avoids computing placements and clear ability when the
term cannot affect the score.

## Calibration

A temporary tool sampled 320,000 post-burn-in boards from deterministic play on
seed base `202610010000`, using 32 chains and all 32 logical CPUs. The old
`occupied >= 30` gate fired on 9,735 boards, or 3.042%.

Three scarcity statistics were calibrated to that same activation rate before
any fitness comparison:

| trigger statistic | equal-rate threshold | activation | overlap with old gate |
|---|---:|---:|---:|
| 17 deadly-piece orientations | 204 | 3.064% | 50.7% |
| every 4–5-square piece | 499 | 3.057% | 52.7% |
| all 47 pieces | 870 | 3.046% | 57.6% |

Only about half the selected boards overlap. Legal placements are correlated
with occupancy, but add substantial information about geometry.

The broader 4–5-square thresholds below were chosen from the same calibration
table. Their activation rates match the tails of the old occupancy distribution,
not fitted hazard outcomes.

| placement threshold | activation rate | comparable occupancy gate |
|---:|---:|---:|
| 422 | 1.17% | 33 |
| 499 | 3.06% | 30 |
| 591 | 8.03% | 27 |
| 686 | 17.49% | 24 |
| 752 | 26.84% | 22 |
| 822 | 38.56% | 20 |

## Method

The experiment followed [`evaluating-changes.md`](evaluating-changes.md): 25
moves of burn-in after each restart, fixed measured exposure, independent
chains, matched seeds, occupancy-band board-risk probes, and paired chain
bootstrap intervals. Lower hazard is better.

Exploration used the bank at seed base `202609020000`, 640 chains × 4,000
measured moves. Confirmation used the independent bank at `202609010000`, 640
chains × 10,000 moves. The unchanged baselines had already been banked on those
exact seeds. Every run used 32 threads.

The build used GCC 12.2 because GCC 16 was unavailable. That prevents comparing
absolute throughput with historical GCC 16 measurements, but not paired policy
quality.

## Which placements to count

The initially calibrated triggers produced these exploratory probe hazard ratios
against the occupancy gate:

| statistic | threshold | hazard ratio | paired 95% CI |
|---|---:|---:|---:|
| deadly pieces | 204 | 1.006 | 0.736 .. 1.367 |
| 4–5-square pieces | 499 | 0.742 | 0.602 .. 0.913 |
| all pieces | 870 | 0.791 | 0.621 .. 1.010 |

Deadly placements alone add no detectable value. Counting every piece is weaker
than counting only the 4–5-square pieces and adds easy-piece placements that are
rarely scarce. The 4–5-square statistic was promoted for threshold tuning.

## Threshold sweep

This is a multi-arm exploratory bank. The point estimates identify a region to
confirm; they are not independent claims.

| threshold | activation | probe hazard ratio | paired 95% CI |
|---:|---:|---:|---:|
| 422 | 1.17% | 0.878 | 0.693 .. 1.111 |
| 499 | 3.06% | 0.742 | 0.602 .. 0.913 |
| 591 | 8.03% | 0.779 | 0.613 .. 0.986 |
| **686** | **17.49%** | **0.605** | **0.473 .. 0.769** |
| 752 | 26.84% | 0.675 | 0.522 .. 0.880 |
| 822 | 38.56% | 0.668 | 0.508 .. 0.875 |

Performance falls off on both wider settings, so 686 is an interior choice
rather than the edge of the tried range. The broad 686–822 plateau is more
credible than the exact ordering of three selected values.

## Independent confirmation

The tuned threshold replicated on the independent 6.4-million-move bank:

| candidate vs occupancy gate | probe ratio | paired 95% CI | p | deaths |
|---|---:|---:|---:|---:|
| threshold 499 | 0.958 | 0.815 .. 1.125 | 0.601 | 72 vs 62 |
| **threshold 686** | **0.756** | **0.634 .. 0.896** | **0.0022** | **45 vs 62** |

The death ratio for 686 was 0.726. Its interval, 0.495–1.065, still crosses one,
as expected from only 107 deaths, but it agrees with the primary probe estimator.

The two placement thresholds can also be compared directly because they used
identical chains. Threshold 686 beat 499 with a probe ratio of **0.789**, 95% CI
**0.673–0.929**, `p = 0.0059`. Deaths were 45 against 72, ratio 0.625, exact
`p = 0.0159`.

The equal-rate 499 trigger therefore did not replicate; the gain comes from
letting geometry trigger the clear lookahead substantially earlier than the old
occupancy gate did, not merely swapping one 3%-frequency condition for another.

## Cost and correctness

The final implementation counts placements only when `occupied > 20`, because
the term is otherwise multiplied by zero. On the repeated 2.56-million-move
exploration run it reproduced every chain, death, probe statistic, and adaptive
allocation count from the generic threshold-686 experiment exactly.

A later controlled throughput check alternated six old/new runs of the fixed-seed
10,000-move benchmark. Native C++ fell from 2,549 to 2,405 move-sets/s, a **5.7%**
throughput cost. WASM fell from 1,778 to 1,687 move-sets/s, a **5.1%** cost. In
elapsed-time terms the change costs 6.0% and 5.4%, respectively. This fixed-work
comparison supersedes the noisier fitness-run timing, where the policies visit
different boards.

The deliberately crowded `search-test` sweep does expose the worst case: it rose
from about 13.5 to 28.4 seconds because almost every synthetic board must count
placements. The full routine suite remains under 30 seconds on this host.

`GameState::countPlacements` uses the same precomputed placement-anchor data as
the game iterator. Tests independently count placements on a scalar 9×9 array,
and the complete scalar evaluator independently implements the new gate. All
seven native regression tests pass.

## How often the policy changes

On 320,000 board/hand decisions generated exclusively by the new evaluator, the
new and old policies selected different full three-piece plans 10,637 times:
**3.324%**, chain-bootstrap 95% CI **3.244%–3.406%**. The first placed piece
changed on 2.668% (95% CI 2.599%–2.738%). Every full-plan disagreement reached a
different final board; none was merely a different ordering of the same result.
Both policies always agreed on whether the hand fit.

This used 320 independent 1,000-board chains, 25 moves of burn-in after every
restart, and the new policy for every trajectory. Both committed WASM evaluators
were asked about the same board and hand at each measured decision.

## Decision

Ship the 4–5-square legal-placement trigger at 686. Do not use the deadly-only or
all-piece variants. The new trigger is sensitive to actual room for difficult
pieces, replicates on an untouched seed bank, and leaves the clear-opportunity
penalty itself unchanged.
