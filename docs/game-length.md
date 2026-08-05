# How long the engine survives, and how to measure it faster

Blokie is good enough at this game that finding out whether a change helped
takes far longer than making the change. A game runs for tens of thousands of
moves, each finished game is worth exactly one number, and those numbers are
spread out enough that averaging a handful of them says almost nothing.

This is what the game's length distribution actually looks like, and what can be
done about the measurement problem it creates. Everything here was measured with
`engine/cpp/survival.cpp`; build it with `npm run build:native` and see
`survival --help` for the flags.

## Where the time goes

At 214 moves/sec/core (`./benchmark`) and a mean game of about 48,000 moves, one
game costs 3.8 core-minutes. Counting deaths is a rate measurement, so its
relative error is `1/sqrt(deaths)` however the deaths are collected. Resolving a
relative difference `d` between two variants, at 95% confidence and with an 80%
chance of noticing a difference that is really there, takes about `2*(2.8/d)^2`
deaths **per side**:

| Resolve | Deaths per side | Moves per side | Core-hours per side |
| --- | --- | --- | --- |
| 20% | 392 | 19M | 25 |
| 10% | 1,568 | 76M | 98 |
| 5% | 6,272 | 302M | 392 |

That is the whole problem in one table. A 5% change to the weights is an
ordinary change, and confirming it the obvious way is four days of a four core
machine *per side*. Worse, the loop is unbounded: without already knowing the
answer there is no way to tell whether a run that has been going an hour is
nearly done or nowhere near.

## What a game's length actually looks like

Forty-eight games played to the end (`./fitness 48 --seed-base 1000`, 75 minutes
on four cores):

```
n=48  mean=48,196  sd=53,185  CV=1.10
min=2,808  p10=6,312  p25=12,587  p50=28,452  p75=56,225  p90=119,121  max=234,020
```

A coefficient of variation of 1.10 is an exponential distribution; so is a
median at 0.59 of the mean and 65% of games finishing below the mean. The
sharper test is the mean residual life -- given a game has already lasted this
long, how much longer does it run:

| Given it reached | Games | Expected remaining |
| --- | --- | --- |
| 0 | 48 | 48,196 |
| 5,000 | 45 | 46,201 |
| 10,000 | 40 | 46,751 |
| 20,000 | 33 | 46,187 |
| 30,000 | 23 | 53,979 |
| 40,000 | 18 | 57,715 |

Flat is memoryless. Having survived 20,000 moves, the engine still expects about
46,000 more -- almost exactly what it expected at move zero. **The mid-game
really is geometric.** Nothing accumulates and nothing wears out; the engine
sits in a steady state and eventually a deal arrives that the steady state
cannot absorb. A long game is a lucky one, not a well-played one. (The upward
drift in the last two rows is 18 and 23 games' worth of noise, and if anything
points at a slightly heavy tail rather than a light one.)

### The hazard, move by move

A game ends on the first deal the engine cannot place in full, so what sets the
length is the *hazard*: the fraction of the 103,823 possible deals that would
end the game, at the board the engine is currently on. `survival` computes that
fraction exactly, and can report it against how far into the game the board was.
Two hundred games capped at 1,000 moves each, to put many independent games
through the early stages:

PLACEHOLDER_EARLY

Only the opening is safe, and only barely: for the first ten moves or so the
hazard is not merely low but essentially zero, because the board is still empty
enough that no deal at all can end the game. By move ten it is already at the
level it keeps for the rest of the game.

**So the early game is not a dangerous phase.** It is a very short safe one, and
short enough not to matter: ten free moves against a mean of 48,000 is 0.02% of
a game, which is why the length distribution shows no sign of it at all. If the
game had a meaningfully safe opening, lengths would be *less* spread out than an
exponential -- a head start plus a memoryless tail has a CV below 1. The measured
CV is 1.10.

Two warnings about reading these profiles, both of which caught me:

- **Late buckets are survivor-biased.** Boards seen at move 100,000 only exist
  in games that reached move 100,000, which are by construction the games
  sitting on safer boards. A profile that sags to the right is not the engine
  improving with age.
- **Count the games, not the samples.** A bucket with 200,000 samples drawn from
  two games is two observations. The harness reports the game count and an error
  bar computed across games for exactly this reason. An early version reported
  neither, and a single lucky game produced a profile I nearly believed.

## What makes the loop shorter

### Make the deals harder -- worth about 1,500x

`--pool` shrinks the set of pieces a deal is drawn from. This is by far the
biggest lever, and the only one that turns an overnight run into a coffee break:

| `--pool` | Pieces | Mean game | Cost of resolving 5% |
| --- | --- | --- | --- |
| `all` | 47 | 48,196 | 392 core-hours/side |
| `no-singles` | 42, no 1x1 or dominoes | 7,900 | 64 core-hours/side |
| `big` | 34, four and five squares | PLACEHOLDER_BIG | PLACEHOLDER_BIG_COST |
| `brutal` | 15, five squares only | 30 | 15 core-minutes/side |

`brutal` games last about thirty moves, so a comparison that would take four days
at full strength takes a quarter of an hour.

What it costs is that you are no longer measuring the same thing. Under `brutal`
deals the hazard *rises* steadily with move index -- the engine cannot keep the
board clean when every piece is five squares, so junk accumulates and the game
becomes a wear-out process rather than a memoryless one. Full strength play is
the opposite. A weight that helps the engine dig out of a cluttered board will
look better under `brutal` than it deserves; one that helps it hold a clean
steady state will look worse.

Use the stress pools to reject bad ideas and catch anything catastrophic. Do not
use them to confirm a small improvement -- anything that survives the fast loop
still has to be confirmed at `--pool all`.

### Measure the hazard instead of counting deaths -- worth about 5x

With `--hazard-every 1`, every move contributes an exact probability instead of
a coin flip that comes up heads once in fifty thousand moves. The mean game
length is one over the average of those probabilities.

Measured over 600,000 moves at full strength: the hazard estimate carried the
precision of 80 deaths, where counting deaths over the same moves would have
produced about 13. That is **5.9x the precision per move, 4.9x after the 20%
extra wall clock** the computation costs. It also makes a run's answer arrive
smoothly rather than in jumps of one death.

The gain shrinks as deaths get more common -- under `--pool brutal` the same
comparison is 1.6x rather than 5.9x -- because the trick works by replacing a
rare coin flip with its probability, and there is less rarity to exploit when
the coin comes up heads three times in a hundred. It never goes negative, so
leave it on.

Why it is only 5x and not 5,000x: the hazard is itself wildly variable across
boards. Its standard deviation across the 600,000 boards sampled was 72 times
its mean, one board in 713 was dangerous enough to lose to a deal in a thousand,
and the worst board seen would have lost to **52% of all possible deals**. Most
of the variance in "when does this game end" is not the coin flip, it is which
boards the engine visits.

That also kills the obvious cheap surrogate. Counting how many of the 47 pieces
have nowhere to go is nearly free, but at full strength it averages 0.0007 --
the engine essentially never leaves a board that any single piece cannot fit on,
right up until it dies. There is no shortcut around computing the real thing.

Two useful properties. The hazard does not depend on the evaluation weights at
all -- whether a deal fits is a fact about the board, and the weights only
choose which boards get visited -- so it is a fixed ruler, and a weight change is
measured entirely by the danger of the boards it leaves behind. And it is cheap
because it asks whether pieces fit rather than where they should go: `survival`
builds a table of which pieces still fit after each *pair* of pieces is placed,
1,128 entries, and reads all 103,823 deals off that table, stopping each entry as
soon as everything has been shown to fit.

### Spend a move budget, not a game budget

`fitness N` plays N games and waits for the slowest. With lengths near
exponential and one game per core, the wait is not the mean game but the longest
of them. `survival --moves N` shares a move budget instead: every thread works
until the budget is gone, a game still running when it runs out contributes the
moves it survived to the denominator rather than being thrown away, and the run
takes exactly as long as you asked.

The 48 game run above spent its last 40 minutes finishing 12 stragglers. A
related trap is watching such a run stream: short games finish first, so partial
output is biased short. Fourteen games in, the running mean was 31,000 and the
sample looked *tighter* than an exponential (CV 0.61). The finished answer was
48,196 with a CV of 1.10, and the opposite conclusion.

### Stop rebuilding to change the weights

`survival --weights 1358,524,...` takes the twelve evaluation weights on the
command line. They are data, not code, and taking a compile out of the loop is
free.

### Know when you have run long enough

`node tools/survival-report.js baseline.json candidate.json` compares two runs
and, when it cannot call it, says what would settle it:

```
  ratio          1.025  95% CI [0.894, 1.175]
  no call: the candidate looks 2.5% longer, but the interval still covers 1.000.
    5% difference: needs about 122,134 moves per side (15.3x this run)
```

Deciding up front what size of difference is worth detecting, and reading off
what that costs, is the difference between a measurement and a vigil.

## Recipes

```bash
npm run build:native
cd engine/cpp/build-native

# Is this change catastrophic? Seconds.
./survival --pool brutal --moves 20000 --weights $W

# Is it plausible? A minute.
./survival --pool big --moves 100000 --weights $W

# What is it really worth? Hours, not days.
./survival --moves 1000000 --hazard-every 1 --seed-base 1 --json > base.json
./survival --moves 1000000 --hazard-every 1 --seed-base 1 --weights $W --json > new.json
node ../../../tools/survival-report.js base.json new.json

# Where is the danger? Many independent games through the early stages.
./survival --moves 200000 --max-game-moves 1000 --hazard-every 1
```

Use the same `--seed-base` on both sides of a comparison. The two variants get
the same piece sequences, which they diverge from within a few moves, but the
early correlation is free precision.

## How not to fool yourself

**Check that the two estimates agree.** Every run reports the mean game length
twice, from counting deaths and from averaging the hazard. They are estimates of
the same number by completely different routes, so a run where they disagree by
more than their error bars has something wrong in it. Across the two big runs
here they agreed well: 48,196 (+-31%) from 48 games played out, against 44,642
(+-22%) from the hazard over 600,000 moves -- and the hazard got the better
answer in 40% of the wall clock.

**Read the blocking curve.** Consecutive boards are nearly the same board, so
treating each hazard sample as independent would claim an error bar several
times smaller than the truth. Each run prints its error bar against how many
consecutive samples were averaged together first:

```
error bar vs block size: 1:18.2% 4:21.0% 16:24.5% 64:24.9% 256:24.8% 1024:24.5% 4096:23.5% 16384:22.0%
```

A curve that has flattened has found the run's real error bar; one still
climbing at the right hand end has not, and the run says so. An early version of
this harness used a fixed block size and confidently reported +-4.6% for a run
whose honest error was +-9.4%.

**Compare like with like.** The harness once printed the death based error as
one standard error and the hazard based error as two, which made the hazard --
the better estimator -- look like the worse one. Everything is a 95% half width
now.

**Run more than one thread at full strength.** A single thread spends a 60,000
move budget inside one or two games. The estimator stays unbiased, but it has
seen one or two games' worth of luck and no amount of sampling within them fixes
that.

**The self check is not decoration.** `survival --self-check 25` plays to random
boards and checks that the pair table, a plain recursive search, and the move
search the engine actually uses all agree about which deals are playable. Add
`--exhaustive` to check every one of the 103,823 deals rather than a sample --
minutes per board, and worth it after touching the counting. CI runs the cheap
version of all three. A hazard quietly 10% too low reads as an engine that
quietly got 10% better.
