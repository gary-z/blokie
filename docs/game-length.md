# How long the engine survives, and how to measure it faster

Blokie is good enough at this game that finding out whether a change made it
better takes longer than making the change. A game runs for tens of thousands of
moves, each finished game is worth exactly one number, and those numbers are
spread out enough that averaging a handful of them says almost nothing. This is
about how to get an answer sooner, and about what the answer is measuring.

Everything below was measured with `engine/cpp/survival.cpp`. Build it with
`npm run build:native`; `survival --help` lists the flags.

## Where the time goes

Three numbers set the budget, all measured on a four core machine:

| | |
| --- | --- |
| Move search | 214 moves/sec/core (`./benchmark`) |
| Mean game | PLACEHOLDER_MEAN moves |
| So: one game | PLACEHOLDER_GAME_MINUTES core-minutes |

Counting deaths is a rate measurement, so its relative error is `1/sqrt(deaths)`
however the deaths are collected. Telling two variants apart at 95% confidence,
with an 80% chance of noticing a difference that is really there, needs the
difference to be about 4 standard errors wide -- so to resolve a relative
difference `d` you need about `(4/d)^2` deaths **per side**:

| Difference to resolve | Deaths per side | Core-hours per side |
| --- | --- | --- |
| 20% | 400 | PLACEHOLDER_CH20 |
| 10% | 1,600 | PLACEHOLDER_CH10 |
| 5% | 6,300 | PLACEHOLDER_CH5 |

That is the whole problem in one table. A 5% change to the weights is a
perfectly ordinary change, and confirming it the obvious way is a week of
compute. Worse, the loop is not just slow but *unbounded*: without knowing the
answer in advance there is no way to tell whether a run that has been going for
an hour is nearly done or nowhere near.

## What a game's length actually looks like

PLACEHOLDER_DISTRIBUTION

### The hazard, move by move

A game ends on the first deal the engine cannot place in full. So the thing that
sets the game's length is the *hazard*: the fraction of possible deals that would
end the game, at the board the engine is currently sitting on. `survival` computes
that fraction exactly, and reports it against how far into the game the board was:

PLACEHOLDER_PROFILE

Two things to take from this.

**The early game is the safe part, not the dangerous part.** For the first
hundred moves or so the hazard is not merely low, it is often exactly zero:
there is no deal at all that can end the game, because the board is still mostly
empty and anything fits somewhere. The hazard climbs to its typical level over
the first few hundred moves. That is a burn-in, and it is why game lengths are
less spread out than a pure exponential would be -- every game gets a free head
start of a few hundred moves before it is really playing.

**After that it is close to flat, which is the memoryless part.** From a few
thousand moves on, the board the engine keeps is about as dangerous at move
30,000 as it was at move 3,000. Nothing accumulates. The engine is not slowly
losing control of the board; it sits in a steady state and eventually a deal
arrives that the steady state cannot absorb. So the mid-game really does behave
like a geometric distribution, and a long game is a lucky one rather than a
well-played one.

The apparent sag in the far right of the profile is worth distrusting. Boards
observed at move 50,000 only exist in games that got to move 50,000, which are
by construction the games that were sitting on safer boards. That is survivor
bias, not the engine improving with age.

## Five things that make the loop shorter

Ranked by how much they actually bought, measured rather than guessed.

### 1. Stop rebuilding to change the weights

`survival --weights 1358,524,...` takes the twelve evaluation weights on the
command line. The weights are data, not code, and taking a compile and a link
out of the loop is free.

### 2. Make the deals harder

This is the big one, and it is the one that turns an overnight run into a coffee
break. `--pool` shrinks the set of pieces a deal is drawn from:

| `--pool` | Pieces | Mean game | Cost of a +-6% measurement |
| --- | --- | --- | --- |
| `all` | 47 | PLACEHOLDER_MEAN | PLACEHOLDER_COST_ALL |
| `no-singles` | 42, no 1x1 or dominoes | ~6,000 | minutes |
| `big` | 34, four and five squares | 750 | ~1 minute |
| `brutal` | 15, five squares only | 30 | ~10 seconds |

A `brutal` game lasts about thirty moves, so a run that would take hours at full
strength takes seconds. Nothing else on this list comes close.

What it costs is that you are no longer measuring the same thing. Under `brutal`
deals the hazard *rises* steadily with move index -- the engine cannot keep the
board clean when every piece is five squares, so junk accumulates and the game is
a wear-out process rather than a memoryless one. Full strength play is the
opposite: a steady state punctuated by bad luck. A weight that helps the engine
recover from a cluttered board will look better under `brutal` than it deserves,
and a weight that helps it stay in a clean steady state will look worse.

So: use the stress pools to reject bad ideas quickly and to catch anything
catastrophic, and never to confirm a small improvement. Anything that survives
the fast loop still has to be confirmed at `--pool all`.

### 3. Measure the hazard instead of counting deaths

With `--hazard-every 1`, every move contributes an exact number -- the fraction
of deals that would have ended the game right there -- instead of a coin flip
that comes up heads once in tens of thousands of moves. The mean game length is
one over the average of those numbers.

This is worth roughly **PLACEHOLDER_RB_GAIN** at full strength: a run reports how many
observed deaths its precision is worth, and that figure came out around
PLACEHOLDER_RB_DETAIL. It costs about 20% more wall clock.

The gain evaporates when deaths are common. Under `--pool brutal` the same run
gave +-9.4% from the hazard against +-6.1% from simply counting the 271 deaths
it saw. That is not a bug: the trick works by replacing a rare coin flip with its
probability, and when the coin comes up heads three times in a hundred there is
not much rarity left to exploit. **Use the hazard at full strength; count deaths
in the stress pools.**

Two properties of the hazard are worth knowing. It does not depend on the
evaluation weights at all -- whether a deal fits is a fact about the board, and
the weights only choose which boards get visited -- so it is a fixed ruler, and a
weight change is measured entirely by the danger of the boards it leaves behind.
And it is computed by asking whether pieces fit rather than where they should go,
which is why it is cheap: `survival` builds a table of which pieces still fit
after each *pair* of pieces is placed, 1,128 entries, and reads every one of the
103,823 possible deals off that table.

### 4. Spend a move budget, not a game budget

`fitness N` plays N games and waits for the slowest. With games drawn from
something near an exponential and one game per core, the wait is not the mean
game but the longest of them: about `1 + 1/2 + ... + 1/N` times the mean, so 2.1x
for four cores. `survival --moves N` shares a move budget instead. Every thread
works until the budget is gone, a game still running when it runs out
contributes the moves it survived to the denominator rather than being thrown
away, and the run takes exactly as long as you asked for.

A related trap: watching a `fitness` run's output as it streams. Short games
finish first, so the partial output is biased short. Thirty-two games into the
48 game run below the running mean was PLACEHOLDER_PARTIAL_MEAN; the finished
answer was PLACEHOLDER_MEAN.

### 5. Know when you have run long enough

`node tools/survival-report.js baseline.json candidate.json` compares two runs
and, when it cannot call it, says how many moves per side would settle it:

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

# Is this change catastrophic? Ten seconds.
./survival --pool brutal --moves 20000 --weights $W

# Is it plausible? A minute or two.
./survival --pool big --moves 100000 --weights $W

# What is it really worth? Compare against the baseline properly.
./survival --moves 1000000 --hazard-every 1 --seed-base 1 --json > base.json
./survival --moves 1000000 --hazard-every 1 --seed-base 1 --weights $W --json > new.json
node ../../../tools/survival-report.js base.json new.json

# Where is the danger concentrated? Read the hazard profile.
./survival --moves 200000 --hazard-every 1
```

Use the same `--seed-base` for both sides of a comparison. It gives the two
variants the same piece sequences, which they will diverge from within a few
moves, but the early correlation is free precision.

## How not to fool yourself

**Check that the two estimates agree.** Every run reports the mean game length
twice, once from counting deaths and once from averaging the hazard. They are
estimates of the same number by completely different routes, so a run where they
disagree by more than their error bars is a run with something wrong in it.
`survival-report.js` says so explicitly when it happens.

**Read the blocking curve.** Consecutive boards are nearly the same board, so
treating each hazard sample as an independent observation would claim an error
bar several times smaller than the truth. Each run prints its error bar against
how many consecutive samples were averaged together first:

```
error bar vs block size: 1:8.7% 4:10.6% 16:9.6% 64:8.8% 256:9.4%
```

A curve that has flattened out has found the run's real error bar. One still
climbing at the right hand end has not, and the run says so. This matters more
than it sounds: an early version of this harness used a fixed block size and
confidently reported +-4.6% for a run whose honest error was +-9.4%.

**Run more than one thread at full strength.** A single thread at `--pool all`
spends a 60,000 move budget inside one or two games. The estimator is still
unbiased, but it has seen one or two boards' worth of luck, and no amount of
sampling within those games fixes it. Independent threads are independent games.

**The self check is not decoration.** `survival --self-check 25` plays to random
boards and checks that the pair table, a plain recursive search, and the move
search the engine actually uses all agree about which deals are playable. Add
`--exhaustive` to check every one of the 103,823 deals rather than a sample --
minutes per board, and worth it after touching the counting. A hazard that is
quietly 10% too low reads as an engine that quietly got 10% better.
