# Measuring whether a change to the evaluation helped

The engine lasts about 40,000 sets of three pieces per game. That number is the
reason a small improvement is hard to see: a run long enough to resolve a 5%
change takes days, and most of the days are spent watching games that were
always going to be long.

This is what the harness measures instead, and why.

## Why the noise is so bad

Game length is very close to exponential. The engine plays a deterministic
policy against an i.i.d. piece stream, so the board is a Markov chain with one
absorbing state — dying. Conditioned on still being alive, its state converges
to a fixed distribution, and from then on the chance of dying is the same every
move. Call that chance the **hazard**, `h`. A run of 40,000 means `h ≈ 2.5e-5`.

A geometric variable has a standard deviation about equal to its mean, so:

    coefficient of variation = sd / mean ≈ 1

Everything painful follows from that one number. Detecting a relative change `δ`
in mean length at 95% confidence and 80% power needs

    games per arm ≈ 2 (1.96 + 0.84)² / δ² ≈ 16 / δ²

| effect | games/arm | moves/arm | core-hours/arm | 4 cores, both arms |
|-------:|----------:|----------:|---------------:|-------------------:|
|    10% |     1,570 |    6.3e7  |             50 |            1.0 day |
|     5% |     6,279 |    2.5e8  |            199 |           4.2 days |
|     2% |    39,244 |    1.6e9  |          1,246 |            26 days |
|     1% |   156,978 |    6.3e9  |          4,983 |           104 days |

(at the measured 350 moves/sec/core)

You can confirm the model on runs you already have. For an exponential,
`sd/mean = 1`, `p50/mean = 0.69`, `p90/mean = 2.30`, `p95/mean = 3.00`. The
harness prints all of these. `--hazard-bins W` prints deaths and moves survived
per `W` moves of depth, which shows the hazard directly and shows how deep you
have to go before it stops trending.

Measured, it holds up well. Three runs of different shapes:

| run | deaths | exposure | hazard | implied mean |
|---|---:|---:|---:|---:|
| 700 games, cut at 3,000 | 37 | 2,052,938 | 1.80e-5 | 55,485 |
| 300 games, cut at 800   |  6 |   237,848 | 2.52e-5 | 39,641 |
| 8 games, played out     |  8 |   213,918 | 3.74e-5 | 26,740 |
| **pooled**              | **51** | **2,504,704** | **2.04e-5** | **49,112** |

Pooled 95% CI on the mean: 37,300 .. 64,600. Note that those three rows combine
by adding deaths and adding exposure, despite being cut at different depths or
not cut at all. That is the property worth having — designs that could not share
an average share a rate.

Four things check out:

- **`cv = 1.03`** on the games played out, against the 1.00 an exponential
  predicts. This is the number the whole cost problem comes from.
- **The left tail is exponential.** 6 of 300 games died before move 800, or
  2.00%, against the 1.98% an exponential with a 40,000 mean predicts.
- **The hazard does not trend.** Splitting the first 3,000 moves at 100, 250,
  500 and 1,000 and testing the early hazard against the rest gives p = 0.64,
  0.77, 0.39, 0.73. Deaths per 1,000 moves of depth were 11, 10, 16 against
  12.5, 12.3, 12.1 expected under a flat hazard.
- **The burn-in is ~25 moves**, from the occupancy plateau above.

So "after a certain point" turns out to mean after about 25 moves, not after
some large fraction of a game.

Two caveats on the absolute constant. 51 deaths pins the rate only to ±14%, and
the harness deals uniformly from all 47 pieces, which need not be the
distribution behind any particular quoted average. 40,000 sits comfortably
inside the interval.

The other is provenance: those rates were measured against the evaluation as it
stood before the crowded-piece scarcity term, so they describe that engine and
not this one. The shape survives the change — re-measured on the current
evaluation, occupancy still reaches its plateau by move ~25, so the burn-in is
the same — but the rate itself would have to be re-measured to be quoted.
Nothing below depends on which constant is right.

Which is itself the point. Whether that term helped is not answerable at the
scale these runs operate at: 350 games against the earlier 300 put the hazard
ratio at 0.43 with a 95% interval of 0.11 to 1.71 and p = 0.32. Nine deaths
between them can only catch a change of 198% or more. Settling it to 5% needs
about 6,300 deaths an arm.

## Cutting games off

Stop a game at `X` moves and it is **right-censored**: you know it lasted longer
than `X` and nothing more. That is still evidence. Fit the hazard by

    ĥ = D / R        D = games that ended, R = total moves survived

Every cut-off game contributes its `X` moves to `R` and no death to `D`, which
is exactly what was observed about it. The log-likelihood is `D log h − h R`,
whose maximum is `D/R` and whose curvature gives

    sd(log ĥ) = 1 / √D

**Precision depends only on the number of deaths.** Not on the number of games,
not on `X`.

Now count the cost. With a burn-in `b` before the hazard settles, and
`q = 1 − e^(−h(X−b))` the chance a game dies before the cutoff:

    moves per game  = b + q/h
    deaths per game = q
    moves per death = 1/h + b/q

The `1/h` term does not move. A death costs `1/h` moves of exposure to produce
no matter how you slice the exposure into games. Truncation gives you fewer
deaths per game and proportionally cheaper games, and the ratio is fixed.

The burn-in is the only thing a cutoff can waste, because every restart pays it
again. Measured (700 games, 2.05M moves), it is about **25 moves**: mean board
occupancy climbs from an empty start to its plateau of 18.2 cells by move ~25
and never moves again. That is 0.06% of a game, so the `b/q` term stays small
until the cutoff gets very aggressive:

| cutoff X | X/mean | P(dies first) | moves/death, b=25 | cost | at b=200 | at b=1000 |
|---------:|-------:|--------------:|------------------:|-----:|---------:|----------:|
|     none |      — |         1.000 |            40,000 |   0% |       0% |        0% |
|   80,000 |   2.00 |         0.865 |            40,029 | 0.1% |     0.6% |      2.9% |
|   40,000 |   1.00 |         0.632 |            40,040 | 0.1% |     0.8% |      4.0% |
|   10,000 |   0.25 |         0.221 |            40,113 | 0.3% |     2.3% |     12.4% |
|    4,000 |   0.10 |         0.095 |            40,264 | 0.7% |     5.5% |     34.6% |
|    1,000 |   0.03 |         0.025 |            41,038 | 2.6% |    25.3% |         — |
|      400 |   0.01 |         0.010 |            42,679 | 6.7% |   100.3% |         — |

So the honest answer to "does a cutoff buy signal": **no.** At the measured
burn-in it is very nearly free — under 1% of efficiency all the way down to a
tenth of the mean — but it buys nothing per unit of compute either.

### What a cutoff is genuinely worth

Not information, but scheduling. Game lengths are exponential, so with `n` games
in flight the longest one runs `H_n / h` moves — 2.7× the mean for 8 games,
4.7× for 64. Threads sit idle behind that straggler while it finishes. Capping
at `X = 2/h` removes the tail and costs 0.1%.

It also turns one 40,000-move blocking unit into a stream of small ones, which
is what makes sequential stopping practical: you can watch deaths arrive and
stop as soon as the answer is clear, instead of committing to a game count up
front.

### The mistake to avoid

Having truncated, do not average `min(T, X)`. It is biased low, and worse, it is
*compressed*: at `X = 0.1 × mean`, a true 5% gain in game length moves the
truncated mean by 0.25%. Some of that is offset by the truncated mean also being
less variable, and the two effects nearly cancel — the naive average costs

    4/3 × the moves the D/R fit needs

in the limit of small `X`. So it degrades gracefully rather than catastrophically,
but it is a third more compute for no reason, and the effect size it reports is
not one you can interpret.

## Comparing two runs

Given `D_A` deaths on `R_A` moves and `D_B` on `R_B`, condition on the total
number of deaths. How they split is binomial with a probability set only by how
much exposure each arm bought:

    D_A | (D_A + D_B) ~ Binomial(D_A + D_B, R_A / (R_A + R_B))

Under the null that both arms have the same hazard, that is an **exact** test —
no normal approximation to a very skewed statistic. `engine/tools/compare-fitness.js`
runs it, along with the effect size:

    hazard ratio = (D_B/R_B) / (D_A/R_A)
    log ratio ± 1.96 √(1/D_A + 1/D_B)

Mean game length moves as the reciprocal, so a hazard ratio of 0.95 is a 5.3%
longer game. Report the ratio: it is the scale on which the interval is
symmetric and on which a cutoff changes nothing.

## Where the real speedup is

Precision is `1/√D`, and a death costs `1/h` moves. So

    sd(log ĥ) = 1 / √(M · h)     for a budget of M moves

Compute scales as `1/h`. The only lever that changes the exponent is **making
the test environment deadlier**, so that deaths arrive sooner:

| regime | mean length | moves/arm @5% | core-hours | speedup |
|---|---:|---:|---:|---:|
| 3 pieces (current)   | 40,000 | 2.5e8 | 199 |   1× |
| a harder piece mix   |  8,000 | 5.0e7 |  40 |   5× |
| 2 pieces             |    800 | 5.0e6 |   4 |  50× |
| 2 pieces, hard mix   |    200 | 1.3e6 |   1 | 200× |

Only the first row is measured. The rest are what the arithmetic gives at those
mean lengths, and the mean lengths themselves are targets rather than
measurements — the 800 is extrapolated from the README's note that two-piece
play costs over 98% of the score, which is a claim about score and not directly
about length. Measure whichever regime you pick before trusting its numbers.

The catch is that a deadlier environment is a different game, and a weight
vector that wins there need not win at three pieces. That is an empirical
question, and a cheap one:

1. Take 8–12 weight vectors whose full-regime order you already know.
2. Measure them in the accelerated regime.
3. Check the rank correlation.

If it holds, use the accelerated regime as a **screen** over many candidates and
spend full-regime compute only on the two or three finalists. Prefer the mildest
acceleration that gives the speedup you need — a slightly harder piece
distribution at three pieces distorts less than dropping to two.

## Bank the baseline

`Var(log ratio) = 1/D_A + 1/D_B`. If the baseline weights and the environment
are unchanged, its deaths and exposure stay valid, so measure it once and carry
the numbers forward. As `D_A` grows the first term vanishes and a new candidate
needs `Z²/δ²` deaths instead of `2Z²/δ²` — half as many, on top of not re-running
the baseline arm at all. About 4× less compute per experiment, for free.

`compare-fitness.js` takes a literal `deaths:exposure` pair in place of a file
for exactly this.

## Recipe

    # measure a candidate, capped so no game blocks a thread
    ./fitness 4000 --max-moves 80000 --seed-base 1 > candidate.txt

    # check the hazard really is flat before trusting any of this
    ./fitness 400 --max-moves 20000 --hazard-bins 2000

    # verdict against a banked baseline -- deaths:exposure accumulated over
    # every run of the current weights, here a baseline resolved to about 2.5%
    node engine/tools/compare-fitness.js 6200:248000000 candidate.txt

Deaths are the currency. Everything else is bookkeeping.
