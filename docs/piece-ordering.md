# Ordering the pieces in hand by how many placements they have

The move search walks up to six orderings of the dealt triple. Which piece it
tries first is a free choice, and an obvious one to make on placement counts:
put the piece with the fewest — or the most — legal placements first, and hope
the walk gets smaller.

It does not. **The ordering is worth about 0.2% of instructions at best**, which
is less than the noise from recompiling the same walk, and the other direction
loses half a percent. This records the measurement, the reason the answer
comes out flat, and the two ways of implementing it that silently delete boards.

## The two ways it goes wrong

Both are the same mistake: the pruning rules in `makeMoveSimpleImpl` are a
canonical-representative argument, and every one of them names *an ordering the
loop also walks* as the one responsible for a board it drops. That argument
holds for any strict total order over the hand, but only if the same order does
all three jobs — sorting the hand, generating the permutations, and answering
every `p1 < p0` and `p2 < p1` the rules ask.

**Sorting the permutations by a second key.** This is what "Most placements
first" (#74 reverted it) did: it built the six orderings from the piece order,
then re-sorted that *list* by placement count. The first ordering walked was no
longer the ascending one, so the pair-swap rule — which drops a back-to-front
pair on the grounds that the ordering playing it the right way round is walked
and does not drop it — started dropping both. The fix it shipped with was to
exempt the first ordering from the rule, which left a whole extra ordering
expanding its third level into boards the union test then threw away: 88,138,840
boards built against 71,642,890, for the same 31,057,948 scored.

**Collapsing ranks on the wrong question.** Sorting the hand once and then
permuting a rank per slot is the cheap way to do this, because it turns every
comparison the rules make into a comparison of two bytes. Two slots have to
share a rank exactly when they hold the same piece, so that a repeated piece
still collapses the walk to three orderings instead of six — and the sorted
array says which those are, since adjacent entries are equal exactly where the
comparator does not put the earlier one first. Reaching for the piece order to
answer that instead looks equivalent and is not: it fires on every adjacent pair
the two orders disagree about, which after sorting on placement count is most of
them. Those pieces get one rank between them, and every ordering that swaps them
stops being walked. `search-test` catches it on the spot — the reordered-hand
fixture, the brute-force sample and both pruning sweeps all fail — which is what
the sweeps added in #74 exist for.

Done correctly there is no exemption and nothing to trade: one comparator,
applied everywhere, and the whole suite passes with the search returning the
same evaluation on every position measured below.

## Where an ordering can change anything at all

Split the moves by whether a clear is available from two pieces or fewer, which
is what decides between walking six orderings and walking one. About 9% of moves
take the one-ordering path.

Counted over 3,000 positions on each of three seed streams, taken from engine
self-play with the shipped weights so the boards are the ones the engine
actually visits — mean occupancy 17.9 to 18.2 squares, against the plateau of
18.2 that `evaluating-changes.md` measured. The counts come from a copy of the
search with counters at each level, so they are facts about the walk and not
about any compiler:

**Six orderings walked (about 91% of moves).** Level-0 and level-1 board counts
are *bit-identical* under all three orderings, on every seed. They have to be:
level-0 boards sum the placements of whichever piece is first over six orderings,
and each piece is first in exactly two of them; level-1 boards sum the legal
pairs over all six ordered pairs of distinct pieces, and there are six orderings
and six such pairs. Neither sum can tell what the labels were.

| counter | fewest first | most first |
|---|---:|---:|
| level-0 boards | 0.00% | 0.00% |
| level-1 boards | 0.00% | 0.00% |
| level-2 setups | −0.07 .. −0.09% | +0.35 .. +0.41% |
| level-2 boards | −0.08 .. −0.11% | +0.43 .. +0.50% |
| evaluations | −0.21 .. −0.31% | +1.16 .. +1.36% |

What is left moving is only what clears do. Legality with nothing cleared is
disjointness, which is symmetric, so the orderings differ only in which of them
the pair rule prunes and in how much room each piece's clearing placements open
for the next one.

**One ordering walked (about 9% of moves).** Here the mirror image holds: no
clear can happen at level 0 or 1, so the leaves are the disjoint triples and
level-2 boards and evaluations are *bit-identical* under all three orderings.
The ordering moves only the levels above them, and moves them a lot.

| counter | fewest first | most first |
|---|---:|---:|
| level-0 boards | −4.5 .. −5.9% | +40 .. +46% |
| level-1 boards | −3.6 .. −6.5% | +49 .. +55% |
| level-2 boards | 0.00% | 0.00% |
| evaluations | 0.00% | 0.00% |

A 3.6 to 6.5% cut, on a regime holding 2.0 to 2.3% of the corpus's level-1
boards. That is the whole prize.

Over all moves it comes to −0.18 .. −0.26% of evaluations and −0.07 .. −0.10% of
level-2 boards for fewest-first, against +0.96 .. +1.14% and +0.40 .. +0.47% for
most-first.

## What that is worth in instructions

Measured under callgrind, on the build the engine ships (unity translation unit,
`-O3 -flto`) with the ISA pinned to `x86-64-v3`, because valgrind cannot run what
`-march=native` emits on the measuring host.

Two builds compiled from different source cannot resolve this. Three builds that
walk *bit-identically* — verified by a checksum over the placements and the board
each search settled on, not just its score — came out 0.55% apart on the same 400
positions purely from codegen:

| build, same walk | instructions |
|---|---:|
| as shipped | 4,431,656,181 |
| ranking through a `{key, piece}` array | 4,456,117,258 |
| ranking through a rank byte | 4,437,812,386 |

So the ordering was measured inside **one binary**, choosing the key at run time,
which leaves the compiled code identical and the walk the only difference:

| positions | shipped order | fewest first | most first |
|---|---:|---:|---:|
| 400, seed 1 | 4,438,422,982 | 4,432,810,278 (−0.127%) | 4,466,154,751 (+0.625%) |
| 1,000, seed 7 | 9,885,420,004 | 9,860,987,368 (−0.247%) | 9,931,016,132 (+0.461%) |

The counting itself is not what costs. Running the same binary with the placement
counts skipped and the order left alone is 273 instructions per move cheaper, or
0.002%: `countPlacements` is a popcount over the anchor mask the placement walk
already builds, not a walk. The reverted attempt counted by iterating the
placements, which is a different price.

Wall clock cannot see any of this. Seven interleaved runs of 3,000 positions gave
medians of 2.590 s fewest-first, 2.638 s shipped and 2.580 s most-first, with
each arm's seven runs spanning 5% to 12% of its own median. Most-first, which
callgrind puts half a percent behind, timed ahead.

## Reordering the last two pieces again

Once the first piece is down, the two that are left have new placement counts on
the board it left, and both orderings of them see that same board. So a canonical
choice made there is one both orderings agree on, and it can replace the hand's
own order in the suffix rule. It buys exactly nothing, and the reason is that the
rule was never dropping leaves on the strength of *which* ordering was canonical:
a leaf whose last two placements did not clear is kept by exactly one of the two
orderings whatever the choice function is, so the choice moves which ordering
pays and not how many pay. Measured, every counter is identical to the digit —
12,083,559 level-1 boards, 6,316,211 level-2 setups, 142,669,050 level-2 boards,
58,437,820 evaluations, with the per-node rule and without it — for 948,568
placement counts spent to get there.

There is one place a per-node choice is legal in a stronger sense. When no clear
is available from two pieces or fewer, nothing clears at level 0 or 1, so the
last two commute at every node and only one of their orders has to be walked at
all — and the cheaper one can be picked per level-0 node instead of once per
move. That is a real reduction and it is worth 605 level-1 boards out of
12,083,559, or 0.005%, for 16,440 extra placement counts. Applied to the shipped
order rather than on top of fewest-first it recovers more of the same small
prize, 8,668 boards or 0.072%, and still costs more counting than it saves.

The reason both come out flat is the one above: placing a piece barely moves the
*relative* placement counts of the two that are left, so the ranking taken on the
starting board already predicts the ranking after the first placement nearly
every time.

## The state to trust

The search is left as it was. Fewest-placements-first is a real 0.1-0.25%, and it
is not worth carrying a second concept through the hottest function in the engine
for a gain smaller than the swing from recompiling it — nor worth moving the
board the search settles on among boards of equal score, which is the trajectory
the shipped weights were measured against. Most-placements-first, which is the
direction the reverted attempt took, is a 0.46-0.63% loss on the walk alone,
before whatever its own ranking costs.

If it is tried again, the measurement to make is the node counts above, not a
benchmark: they are exact, they are free of the compiler, and they say the answer
before a line of the search has to be rewritten.
