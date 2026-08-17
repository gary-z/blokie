# Trying to fix the evaluation with the golden corpus

The corpus said the jaggedness signal was wrong in 25 of 32 places. Every change
it suggested made the engine worse — the mildest by half, the best-scoring by a
factor of 164. This records what was tried and why it failed, because the corpus
is still sitting there and will tempt the next attempt in exactly the same way.

Nothing here changed the engine. The shipped evaluation survived the probe.

## What was tried

The corpus implicates one mechanism, and the mechanism is real. The four
`blocked_*` masks in `eval.cpp` treat the board edge as a blocker, so an open run
that ends at the wall ends there for counting. A piece placed flush against the
wall shortens a run; the same piece inland *splits* one, and pays two transitions
for every column it crosses. A 5-bar on row 0 leaves 36 transitions; the same bar
on row 2 leaves 46. At 4,450 a transition that is a 44,500 penalty for playing
inland, which is what parks pieces on the rim.

So the edge was made free: outside the board counted as open, so the wall neither
starts nor ends a run. One variant applied that to every mask, another confined
it to the transition count. Both were screened on the corpus, weights were then
fitted to the corpus, and all of it was played out.

| arm | corpus | mean game length | 95% CI | vs shipped |
|---|---:|---:|---|---:|
| shipped eval | 7/32 | **56,971** | 49,731..65,264 | — |
| rework, shipped weights | 11/32 | 22,706 | 19,883..25,929 | 2.5× worse |
| jaggedness lowered only | 11/32 | 20,995 | 18,345..24,028 | 2.7× worse |
| rework + jaggedness lowered | 16/32 | 13,789 | 12,106..15,704 | 4.1× worse |
| weights fitted to the corpus | 24/32 | 1,179 | 1,079..1,287 | 48× worse |
| same, shipped signal | 22/32 | 347 | 327..367 | 164× worse |

200 measured deaths an arm, fixed-exposure chains, 25-move burn-in excluded.
The corpus score rises monotonically as the engine gets worse.

## The weight the corpus most wanted changed is already optimal

Sweeping the transition weight alone, everything else untouched, 300 deaths a
point:

| transition | 0 | 1,112 | 2,225 | 3,337 | **4,450** | 6,675 | 8,900 | 13,350 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| mean length | 9,121 | 19,043 | 31,242 | 40,714 | **48,990** | 34,613 | 16,019 | 2,980 |

The shipped value sits on the peak of a sharp unimodal curve. A quarter less
costs 17%, half less costs 36%, zero costs 5.4×. The corpus scored best at 0 and
1,112.

**The jaggedness signal is not what is wrong with the evaluation.** It is the
most load-bearing weight it has, and it is already tuned to its optimum.

## Why the corpus pointed the wrong way

Three reasons, and they compound.

### Clearing rate is conserved

The mined pairs label a board better when the engine clears more squares from it.
Measured across every policy tried, including two that die more than a hundred
times sooner:

| policy | mean length | squares cleared per move |
|---|---:|---:|
| shipped | 56,971 | 11.74 |
| jaggedness lowered | 20,995 | 11.74 |
| reworked | 22,706 | 11.74 |
| fitted to corpus | 347 | 11.65 |

It cannot be otherwise. In the long run you clear exactly what you place, or the
board fills and the game ends. **Clearing rate is not a free variable** — it is
fixed by the piece distribution, and no policy moves it. A board can out-clear
its sibling over a few sets, but that is borrowing against the future, and a
policy tuned to do it everywhere arrives at the wall sooner. What separated the
arms was occupancy: 18.2 squares for the shipped evaluation against 23.4 for the
one that dies in 347 moves.

### Selecting on failure biases the objective

Twenty of the thirty-two pairs were mined *because the evaluation got them
wrong*. A corpus built that way always wants the weights moved, whether or not
they are already right: the cases it holds are exactly the ones where moving them
helps, and it holds no evidence about everything that would break. Fitting
thirteen weights to thirty-two such pairs drove `deadly` and `3bar` to zero and
pinned others at the ceiling. That scores 24/32 and plays 48× worse.

### A representative corpus is unbiased and nearly uninformative

Sampling decisions at random instead — the evaluation's choice against one random
legal alternative, labelled by which actually survives twenty sets — removes the
bias and most of the signal with it. The shipped evaluation already gets **97 of
98** such pairs right, and its response to the transition weight across the whole
range is flat, 96 to 97.

Which is the trap in one line: a pair is only informative where the evaluation
errs, and selecting on error is what makes the corpus lie.

## What the evaluation is actually doing

Judged on the objective that matters — the chance of being dead within twenty
sets, the window a crowded board resolves in — it is good. Over 1,598 decisions
on boards of 28 to 44 squares:

| placement | P(dead within 20 sets) |
|---|---:|
| a random legal alternative | 0.0072 |
| what the evaluation chooses | **0.0022** |

Three and a third times safer, and beaten by that rival on only 9.9% of
decisions. On a stricter search — best of eight alternatives on shared piece
streams — only 6 of 16 candidate mistakes survived re-measurement on fresh
streams; the other ten were the winner's curse. The six are real: at 42 squares
the evaluation's placement is dead within twenty sets 9.7% of the time against
3.8% for the one it rejected, and it prefers it by 34,247.

So there is headroom. It is small, it lives on crowded boards, and none of it is
what the clearing corpus was pointing at.

## If you pick this up again

* **Do not use squares cleared as ground truth.** It is conserved, so it cannot
  rank policies. The twenty mined pairs that rest on it are true statements about
  clearing and not evidence about playing well.
* **Label pairs by survival within about twenty sets, on crowded boards only.**
  That is the objective, it is measurable there, and it is the window before a
  board either kills you or returns to something ordinary.
* **Keep the sampling representative and expect the corpus to agree with the
  evaluation.** A corpus it passes 97% of is a good regression guard and a bad
  optimisation target. Those are different uses and only the first is safe.
* **Tune against the hazard directly.** The sweep that located the optimum was
  eight runs and about forty minutes. That is what moving one weight honestly
  costs, and it is affordable.
