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

```
hazard by move index          204 independent games, capped at 1,000 moves each
  0-9         6.04e-7  +-7.1e-7
  10-29       2.47e-5  +-3.7e-5
  30-99       2.10e-5  +-2.6e-5
  100-299     3.40e-5  +-2.6e-5
  300-999     1.93e-5  +-8.5e-6
```

The steady state, from a separate 600,000 move run, is 2.24e-5. Only the first
bucket is distinguishable from it -- and it is distinguishable in the *safe*
direction, some thirty times below. By move ten the hazard is already at the
level it keeps for the rest of the game, and every bucket after that is within
its own error bar of the steady state. The opening is safe because the board is
still empty enough that no deal at all can end the game.

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
| `big` | 34, four and five squares | 1,311 | 11 core-hours/side |
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

### Why the small pieces matter, which is not the obvious reason

The natural reading of the table above is that the little pieces are an escape
hatch: a 1x1 fits anywhere, so it bails the engine out of positions the big
pieces cannot. That is testable, and it is mostly wrong.

`--hazard-pool` measures the hazard against a different set of deals from the
ones being played. Play the real game, with all 47 pieces, and ask what fraction
of *small-piece-free* deals those same boards could not have taken. Because the
hazard measurement does not touch the RNG, two runs with the same `--seed-base`
walk identical trajectories, so this is a perfectly paired comparison -- the same
boards, graded twice:

```
150,000 identical boards, reached by playing the full pool

deals graded against          1 in N fatal    vs full pool
all 47                              42,120            1.0x
no 1-2 square                       32,737            1.3x
no 1-2-3 square                     22,046            1.9x
5-square only                        6,186            6.8x
```

Taking the thirteen small pieces out of the deal makes the boards the engine
actually reaches only **1.9x** more dangerous. But playing whole games without
them makes games **37x** shorter. So the escape hatch is worth 1.9x and the
remaining 20x is something else: without small pieces the engine cannot keep the
board clean in the first place, and it spends the game on boards it would never
otherwise have visited.

The small pieces are not mainly a rescue. They are how the board stays tidy.

(That 1.9x is worth more than the +-44% on each row suggests. The rows are
paired, so the ratio is far better determined than either number in it -- an
independent seed put the absolute hazards at 31,223 and 16,551, a third lower
across the board, and the ratio at 1.89x.)

Two consequences. Practically, `--pool big` is largely measuring *can this
engine keep a clean board using only big pieces*, which is a different skill
from *can this engine avoid a rare death* -- so trust it to reject, not to
confirm. And `--hazard-pool` is the tool for asking mechanism questions like
this one, because it separates what a piece set does to the board the engine
ends up on from what it does to the engine's chances on a board it is already
on.

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

**Read the error bar curves.** Consecutive boards are nearly the same board, so
treating each hazard sample as independent would claim an error bar several
times smaller than the truth. Each run prints its error bar two ways: against
where the trajectory was cut into supposedly independent pieces, and against how
many consecutive samples were averaged first.

```
error bar vs where cut:  <=2:43.5% <=4:59.1% <=8:58.8% <=12:59.3% <=20:59.4%
error bar vs block size: 1:56.4% 4:58.9% 16:59.5% 64:59.7% 256:59.3% 1024:59.9%
```

Both settle on the same number, which is the point of printing both. A curve
still climbing at its right hand end has not found the run's real error yet, and
the run says so. An early version of this harness used a single fixed block size
and confidently reported +-4.6% for a run whose honest error was +-9.4%.

Why the first curve exists is in the next section.

**Compare like with like.** The harness once printed the death based error as
one standard error and the hazard based error as two, which made the hazard --
the better estimator -- look like the worse one. Everything is a 95% half width
now.

### Where to cut a trajectory, and why it cannot bias anything

An empty board would be an ideal place to cut a run in two. It is not merely a
safe board: it is *exactly* the state a new game starts from, and the board is
the whole of the state the move search sees, so what follows one is drawn from
the same distribution as what follows the other. Cycles between empty boards
would be independent in the strongest sense available, with no approximation
anywhere.

The engine never gets there. Over 40,000 moves it emptied the board zero times,
and near-empty is not common either -- five blocks or fewer is under half a
percent of moves. It holds the board at **18 blocks on average** and stays
there. Clearing out entirely is not a thing this engine does on the way past; it
is a thing that would take a clear so large it essentially never comes up.

So exact regeneration is unavailable, and the question becomes whether cutting
at *nearly* empty boards is good enough -- which sounds like it needs a
judgement about which boards are safe, made by the same kind of reasoning the
engine's evaluation makes, to grade the engine's evaluation.

It does not, and the reason is worth being precise about. The estimate is

```
hazard = (hazard summed over the boards seen) / (number of boards seen)
```

and **every partition of the same boards has the same numerator and the same
denominator**. Cutting cannot move the answer. Measured over 200,000 boards,
cutting at 1 block and cutting at 20 give 2.27664e-05 either way, to every digit
printed. So no choice of cut point can bias the estimate, and there is no
circularity to worry about: the only thing a cut point affects is the error bar.

What a good cut buys is that the pieces either side of it are close to
independent. Cutting at low occupancy turns out to do that unusually well, for a
reason specific to this game: the hazard lives on crowded boards, so a dangerous
stretch *is* a stretch of high occupancy, and cutting whenever the board comes
back down puts at most one such stretch in each piece. That is exactly the
correlated unit.

The payoff is not a smaller error bar -- it is the same one -- but a far better
determined one. Over the same 200,000 boards:

| Cut | Pieces | 95% error |
| --- | --- | --- |
| at 20 blocks or fewer | 134,270 | 35.0% |
| at 8 blocks or fewer | 6,950 | 34.9% |
| every 64 boards | 3,123 | 34.9% |
| every 4,096 boards | 47 | 34.3% |

The fixed size curve has to climb to a block of 64 before it stops
underestimating, and by 4,096 there are 47 blocks left to compute a spread from,
so the error bar has its own error bar of around 10%. The occupancy cut is at
its converged value immediately and has tens of thousands of pieces behind it.
That is what removes the eyeballing: the harness quotes the occupancy cut and
prints the fixed size curve beside it as a cross check.

None of this adds signal. A run contains what it contains, and reorganising it
into cycles does not create information -- which also answers the tempting idea
of stopping a run at a regeneration point and crediting it with the expected
remaining life. The credit you would give it is the quantity being estimated, so
that is a renewal equation, not a shortcut, and it yields the same time average
by a longer route.

The variance that is left is not fixable by bookkeeping. It comes from the
hazard's spread across boards -- a standard deviation 72 times its own mean --
which is to say from how often the engine wanders somewhere dangerous. Attacking
that needs a method that deliberately spends more samples on the rare dangerous
excursions, such as splitting a trajectory when it enters one and reweighting.
Worth noting that the hazard would make a sound importance function for it,
being exact and independent of the weights, so that approach would not be
circular either.

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
