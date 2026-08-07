# How often can the whole board be cleared?

A recurring belief among players is that the game reads the board and hands
over the one set of three pieces that wipes it clean. This is what Blokie has
to say about how often that is even possible.

A set counts as a wipe when some order and some choice of placements leaves the
board with nothing on it. That includes wipes partway through a set: the board
can go empty on the first or second piece and be covered up again by the ones
still to play, which is a real board wipe even though it is gone by the time
the next set is dealt.

Everything below comes from `engine/cpp/perfect-clear.cpp`. Pieces are drawn
uniformly from the 47 the engine knows about.

## Playing normally, it essentially never happens

Blokie playing its ordinary game, the one that averages 1.5 million points:

| | |
|---|---|
| piece sets played | 300,000 |
| sets where a wipe was possible | 9 |
| rate | 0.003%, about 1 in 33,000 sets |

A full Blokie game runs about 40,000 sets, so a wipe becomes possible roughly
once per game — for a bot that plays a game a person would need weeks to
finish. A human game runs a few hundred sets, which puts it at something like
one wipe every hundred games.

### Why it is so rare

The reason is arithmetic, not luck. Emptying the board means every block on it
leaves in a completed row, column or cube, and completing those lines costs
squares. Measuring that cost — the fewest squares that would have to be placed
to clear a board completely — against what a set of pieces actually provides:

| | squares |
|---|---|
| needed to wipe a well played board (mean) | 20.4 or more |
| supplied by three pieces (mean) | 11.7 |

A set of three pieces averages 11.7 squares. A well kept board sits at least 20
squares away from being clearable — at least, because the measurement stops
counting at 28 and plenty of boards are past that. The set is not even half of
what the job takes, so on most turns a wipe is not narrowly missed, it is not
remotely on the table.

Playing well makes this *worse*, not better: a strong player carries a steady
15 to 20 blocks, because a board that clears every round is a board with blocks
on it.

The whole effect is driven by how empty the board already is:

| blocks on board | share of sets | wipes found |
|---|---|---|
| 0-3 | 0.1% | 5 |
| 4-5 | 0.4% | 1 |
| 6-10 | 8.0% | 0 |
| 11-15 | 25.9% | 3 |
| 16+ | 65.6% | 0 in 196,874 sets |

Two thirds of a strong player's sets arrive at a board carrying 16 blocks or
more, and not one of those 196,874 sets could have been wiped. The wipes
clustered on the 0.5% of sets that caught the board nearly bare.

## Playing to wipe on purpose

The rarity above is partly a choice: Blokie is playing for score and has no
interest in emptying the board. So the driver also runs a policy that plays for
wipes — it takes one whenever a set offers it, and otherwise steers the board
toward positions a later set can clear, at whatever cost to its own health.

How hard it steers is a weight. Turning it up buys wipes and spends survival:

| steering weight | mean sets per game | blocks carried | wipe rate |
|---|---|---|---|
| 1,000 | never died in 25,000 | 18.4 | 0.004% |
| 4,000 | 8,333 | 20.3 | 0.020% |
| 12,000 | 481 | 28.3 | 0.028% |
| 30,000 | 53 | 36.0 | 0.096% |
| 80,000 | 20 | 36.2 | 0.232% |
| 200,000 | 14 | 34.7 | 0.354% |

The last row is over 120,000 sets; the rest are 25,000 each.

A player who still wants to survive gets a wipe roughly once in 5,000 sets. One
who will throw the game away for them — dying every 14 sets to do it — gets
about **one in 282**. Neither is anywhere near "the game keeps handing me one".

Most of those wipes are ones a casual look would miss. Of the 425 wipes in the
120,000 set run:

| when the board went empty | share |
|---|---|
| after the 1st piece, two still to play | 18% |
| after the 2nd piece, one still to play | 63% |
| at the end of the set | 20% |

Four wipes in five happen partway through a set and are covered over by the
pieces still to come. The board really is empty for a moment, but only 1 in 5
leaves you looking at a bare board when the next set arrives.

## The ceiling

There is a hard limit underneath all of this. Every set arrives at some board,
and the board fixes the odds that the set can wipe it. So the best board there
is caps how often anyone can wipe, no matter how they play or how long they
spend setting up.

Boards worth considering are the ones that fit inside a single line. A block
outside the line the wipe is built on needs a second completed line to take it,
and a second line costs at least eight more squares on top of the first —
more than three pieces can pay for on top of the first line. There are only 511
boards inside a given line, so they can all be checked against all 47^3 =
103,823 deals.

| family | best board in it | wipeable by |
|---|---|---|
| row 4 (middle) | 6 blocks in a run, 3 clear at one end | 9.749% |
| row 0 (edge) | 6 blocks in a run, 3 clear at one end | 9.554% |
| centre cube | 4 blocks on the cube's corners | 8.381% |

The best position in the game to be sitting in is six blocks in a row with a
three square gap at the end of it:

```
.........
.........
.........
.........
######...
.........
.........
.........
.........
```

and even from there only **9.7% of piece sets can wipe the board** — about one
set in ten. That is the ceiling on the whole phenomenon. Everything a player
does, every setup, every read of the board, is spent trying to get near a
number that tops out at one in ten, and normal strong play runs at 1 in 33,000.

Two useful reference points from the same measurement:

| board | wipeable by |
|---|---|
| completely empty | 3.389% |
| a single block | 3.787% |
| a row less one square | 8.888% |
| six in a run (best found) | 9.749% |

Even a totally clean board only gives about a 1 in 30 chance, because three
pieces then have to fall so that everything they put down clears itself.

This scan is exhaustive over boards that fit inside one line. Boards spanning
two overlapping lines — a row plus a cube it crosses, say — are affordable in
principle, so `--ceiling` also hill climbs over boards in general, with no
constraint on shape. It settles on the same 9.749% board from every start that
gets anywhere. So 9.7% is the best found rather than a proven maximum, but
nothing in the search suggests there is much above it.

## What this says about the belief

The feeling that the game is handing over a board-clearing set is real, but
what it is tracking is not a board wipe. Clearing several lines at once, or
getting three pieces that all slot into the mess you had, feels like the game
helping and happens constantly. Clearing the board down to nothing is a
different event, and at about 1 in 33,000 sets in normal play it is not
something the game could be arranging on any regular basis. Even a player
trying for nothing else, throwing away the game to get there, cannot get above
roughly one in a few hundred.

## Running it

```
cd engine/cpp && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make perfect-clear

./perfect-clear 300000 --baseline-only   # how often normal play is offered one
./perfect-clear 120000 -w 200000         # how often a wipe hunter gets one
./perfect-clear --line-scan 1            # every board inside the middle row
./perfect-clear 4000 --ceiling           # hill climb over boards in general
./perfect-clear 6000 --self-test         # check the search against brute force
```

`--self-test` runs the wipe search and the cover bound against searches that do
no pruning at all, on boards built around a nearly finished line so that wipes
actually come up — 6,000 boards, 93 of them wipeable, all agreeing. `--verify`
separately checks the wipe detection used during play against the dedicated
search on every set of a run.
