# Golden board pairs

Human-curated pairs of boards that share a parent placement. Each pair says
**A is better than B** — the engine’s eval (lower is better) should score
`eval(A) < eval(B)`.

A pair is a **directional indicator**, and only that. It says which of two
boards is preferable; it says nothing about how much the preference is worth.
Read [How to use these pairs](#how-to-use-these-pairs) before treating a pass
rate as evidence about the engine — the corpus is for finding places the eval
is blind or backwards, not for tuning weights and not as a fitness proxy.

## File

`engine/golden/golden.json` is the source of truth. It is a JSON array,
parsed by `golden` and `golden_measure`. Each entry has an `id`,
a `description`, and two boards `a` (preferred) and `b` as arrays of nine
9-character strings (one per row, `'.'` empty, `'#'` occupied).

```json
[
  {
    "id": "keep-center-open",
    "description": "Keeping the centre 3x3 free leaves more room for 5-bars.",
    "a": [
      ".........",
      ".........",
      ".........",
      "...###...",
      "...#.#...",
      "...###...",
      ".........",
      ".........",
      "........."
    ],
    "b": [
      ".........",
      ".........",
      ".........",
      "...###...",
      "...###...",
      "...###...",
      ".........",
      ".........",
      "........."
    ]
  }
]
```

Rules:

* Boards are 9 strings of 9 characters. `'.'` = empty, `'#'` = occupied. First string is row 0 (top),
  first char is column 0 (left). One string per line keeps diffs readable.
* `a` is the **preferred** board, `b` is the other. The checker enforces
  `eval(a) < eval(b)`; lower eval is better.
* `id` should be short, unique, filesystem-safe (used in reports). If omitted
  the pair becomes `pair_1`, `pair_2`, …
* `description` is free-form human reasoning for why `a` is better.
* Pairs are assumed to be siblings — two placements of the same piece from the
  same parent — so a pair can encode “don’t put the 3-bar here”. Nothing checks
  that assumption: `golden` only enforces `eval(a) < eval(b)`, and a pair whose
  boards could not have come from one parent still scores. Equal occupancy on
  the two sides, below, is the part that is worth keeping honest.
* Boards should already be *cleared*: no full row, column, or 3×3 cube remains.
  (A full line is cleared the instant it is made.)
* Keep boards visually scannable. Prefer `.` and `#`. The JSON file itself
  is the spec — no separate comment syntax to break.

* **Both boards should hold the same number of squares.** True siblings give
  this for free, and it is what makes the validation below work: dealt the same
  pieces, the board left holding fewer squares is exactly the board that
  cleared more, with the eval never entering the comparison.

The old `golden.txt` arrow format (`A` then `>` then `B` with `#` comments)
is still parsed for backwards compatibility, but `golden.json` is canonical.

## How to use these pairs

### What a pair can tell you

* **Which side is better, directionally.** And it can be checked: for any
  board, every triple the game can deal — all 47³ = 103,823 — can be enumerated
  and played by the real search, giving the expected squares cleared by the
  next set exactly, with no sampling error, in seconds.
* **That the eval has a sign backwards.** `diversify-diags` is the live
  example: the eval prefers B, and the game clears 0.331 squares per set more
  from A.
* **That a distinction is missing entirely.** On `diversify-and-set-up-combo`
  the two boards produce identical values for all thirteen features, and none
  of 20,000 random weight vectors separates them. The cause is structural — the
  eval is exactly transpose-invariant, so orientation is not a concept it has.
  That pair cannot be fixed by tuning. It is a request for a feature.

### What a pair cannot tell you

* **How to set a weight.** Zeroing each weight in turn and quadrupling three
  gives engines whose mean game length runs from 4,270 to 58,726 moves. Sixteen
  of those seventeen evals scored *identically* on a corpus, and the one that
  scored better played 4.4× worse. Rank correlation between pass rate and mean
  length came out at −0.31. A pass rate is not a fitness proxy; use `fitness`
  and [`../../docs/evaluating-changes.md`](../../docs/evaluating-changes.md).
* **How much the preference is worth.** A pair is ordinal. For sparse boards
  the entire difference is worth about 0.03% of a game.
* **Anything, if validated by survival or score.** Two boards two squares apart
  diverge to full independence within ~25 moves, and that window holds about
  3×10⁻⁴ expected deaths. There is no horizon at which playing them out works.

### Validating a new pair

`golden_measure` does this. It is a short paired rollout from both boards on
the **same piece stream**, reading **squares cleared** rather than the eval:

```bash
make golden_measure -j && ./golden_measure
```

Steps 2 to 4 below are what it runs; the rest is what to do with the answer.

1. **Equal occupancy on both sides**, and no full row, column or cube. The
   editor enforces both.
2. **Compute the exhaustive next-set numbers** — expected squares cleared, and
   `P(no fit)`. Every hand the game can deal, so the numbers are exact.
3. **If `P(no fit)` > 0 the board can die, and deaths are the measure.**
   Otherwise roll forward with common random numbers and read clears. Crowded
   boards clear more easily whatever else is true of them, so on a board that
   can die the clearing number will happily point at the worse side.
4. **Accept if A leads over sets 3 to 6.** Not at one set: a pair that sets up
   a combo declines a clear now to take a bigger one later, and
   `combo-potential-1` does exactly that — behind by 0.258 squares at one move,
   ahead from three moves on. Above N ≈ 8 the signal decays as the boards mix.
5. **Drop or flip a pair the tool reports as `DROP or FLIP`.**
   `clearing-planning-1` was removed this way: B won on the exhaustive measure,
   on clears at every horizon, and on the eval.
6. **Never condition a death away.** Count deaths across all trials, not only
   the ones a side survived — dropping them is what makes a board that wins by
   dying look good. A side that scores better *and* dies more is contradictory,
   not passing.

Which measure to trust depends on how full the boards are:

| occupied squares | what happens there | measure |
|---|---|---|
| 2–20 | `P(no fit)` = 0; death is unreachable in the window | squares cleared |
| 23–35 | risk becomes measurable, 32× baseline by 24 squares | clears **and** deaths |
| 36–44 | 1,000× baseline; 17% chance of dying within 4 moves | `P(no fit)`, exhaustively |

Every pair in the file today sits in the first row, so `P(no fit)` is 0.00000
on all sixteen boards and no death signal exists to find.

### Reading the eval after N moves

Tempting, and half-trustworthy. Rolling forward and reading the eval has far
more signal than reading clears — but it is circular, and measurably so: on an
earlier placeholder corpus the rollout verdict agreed with the static verdict
twelve times out of twelve, because it mostly carries the starting difference
forward. Trust it in two cases only, where circularity cannot be the
explanation:

* the static difference is **zero**, so there is nothing to carry forward;
* the rollout **contradicts** the static eval.

Everywhere else, squares cleared is the verdict and the eval is commentary.

## Tools

Built from `engine/cpp/CMakeLists.txt`:

```bash
# after cmake configure
make golden golden_measure -j
./golden               # check eval vs human, quick
./golden --verbose     # show evals + boards
./golden --json        # machine-readable
./golden --strict      # exit 1 if any pair fails (CI)

# what the GAME does with each board -- the check that decides whether to keep
# a pair. Run this on a new pair before committing it.
./golden_measure
./golden_measure --window 5 8       # a pair whose payoff lands later
./golden_measure --max-trials 64000 # spend longer on a close one
```

`golden_measure` is the one to run on a new pair. It reports, per pair:

* **cleared** — expected squares cleared by the next set, A minus B, computed
  over *every* hand the game can deal rather than a sample of them, so it
  carries no error bar at all. Positive favours A.
* **P(no fit)** — the chance the board ends the game on the very next set, also
  exact. Zero means the pair is decided on clearing; above zero the pair is
  decided on which side dies less, and the verdict says which.
* **window / t** — squares cleared over sets 3 to 6 of real play, both boards
  on one shared piece stream. Not circular: the same pieces land on both, so
  fewer squares held means more squares cleared, and the eval never scores its
  own test.
* **deaths A/B** — counted over every trial, including the ones a side did not
  survive. Conditioning those away is what makes a board that wins by dying
  look good.
* **eval now / eval later / self-consistent** — the same window read on the
  eval's own scale. See below.

### Does the eval agree with itself?

A good eval should predict its own future: if it scores A below B, then after a
few sets of play under the policy it induces, A's descendants should still
score below B's. `eval now` is what `golden` compares; `eval later` is that same
difference after the window. The column says whether the second still says what
the first said:

* **REVERSES** — the sign flipped. The eval contradicts itself.
* **BREAKS TIE** — it could not separate the boards at all, then separated them
  decisively one move later.
* **fades to N%** — it was confident and now has almost no opinion. Loudness
  that does not survive a move.
* **holds N%** — consistent.

This is circular by construction and **cannot say which side is right**. What it
can say is that the eval is internally inconsistent, and inconsistency is a
defect whichever end of it is wrong. It is also the cheap half: the eval is a
dense readout where cleared squares are a sparse one, so it separates pairs at
`t` in the tens where clearing manages single digits, and it needs no human
label at all — which is what would make it minable automatically from real play.

The limit is worth stating plainly, because it decides how much to trust it.
Self-consistency is a fixed-point property: any eval that is a fixed point of
its own policy satisfies it, including a wrong one. A bias applied uniformly at
every step is inherited by the rolled-out boards, which then dutifully confirm
the original verdict. `2x2-prefer-middle` is exactly that — `holds 37%`, an eval
perfectly consistent with itself and, by the clearing measure, backwards. It is
the same reason value iteration needs a reward and not only a Bellman residual.

So: **necessary, not sufficient.** Use it to decide where to look; use cleared
squares to decide who is right. On the corpus today it flags five pairs and
catches four of the five the eval gets wrong, missing only the uniformly biased
one — and it additionally flags `aligned-on-cube-edge`, which the eval passes by
56,242 and which turns out to be worth nothing at all.

It stops each pair as soon as the answer is clear, so a decisive pair costs a
thousand rollouts and only a close one costs sixteen thousand. The whole
eight-pair corpus takes about a minute on 32 cores.

There was a third tool, `golden_verify`, which compared the human against a
Monte-Carlo playout and against a sampled triple-fit probe. It has been
removed. Its playout column measured survival from a starting board, which the
mixing result above says is not measurable at any horizon, and in practice the
column reported the horizon cap rather than survival — nearly every rollout
reached the cap, so one unlucky death moved a pair's verdict. Its probe column
measured `P(no fit)` by sampling, which `golden_measure` now computes exactly
over every hand, and it scored a 0-vs-0 tie as a disagreement, so on a corpus
of safe boards it reported 0% agreement no matter what the pairs said.

## Editor

`engine/golden/editor.html` draws the two boards, and scores them with the
same WASM the game plays with — `engine/wasm/blokie-solver.js`, whose
`evaluate()` is `GameState::simpleEval` under the default weights. That is the
number `golden.cpp` compares, so a pair marked PASS in the editor passes
`./golden` without a build:

* Each board's eval sits in its header, and the pair gets `EVAL PASS` /
  `EVAL FAIL` as you draw.
* Every pair in the corpus carries the same badge, with a count of how many
  pass at the top of the list.
* A board holding a full row, column or cube gets no verdict. The game clears a
  line the instant it is made, so that board is not a position the game can be
  in and its eval does not describe anything.

It needs to be served over http — both the fetch of `./golden.json` and the
import of the solver are blocked under `file://`:

```bash
python3 -m http.server 8000    # from the repo root
# open http://localhost:8000/engine/golden/editor.html
```

Serve the repo root, not this folder. The editor imports the solver as
`../wasm/blokie-solver.js`, and a server rooted here clamps that `..` to
`/wasm/blokie-solver.js`, which is outside what it can reach — so the page
loads, the engine does not, and every pair shows no verdict. Any root that
contains `engine/` works: the repo root, `engine/` itself, or the folder above
the repo.

## Adding a pair

1. Pick a parent board and a piece, place it two ways, clear lines, and copy
   the two resulting boards as 9×9 blocks. The boards differ by exactly that
   piece’s placement (plus any lines it cleared).
2. Decide which resulting board you’d rather play from.
3. Append to `golden.json` (one string per row, keep the array vertical):

```json
{
  "id": "my-new-pair",
  "description": "One-sentence why a is better.",
  "a": [
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    "........."
  ],
  "b": [
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    ".........",
    "........."
  ]
}
```

`a` is the board you prefer. Keep each board string exactly 9 chars.

4. Run `./golden_measure` to check the pair is real, and `./golden` to see what
   the eval makes of it. It’s fine if a pair doesn’t pass `golden` today — that
   is the interesting case. It is not fine if `golden_measure` says B.

## Evaluating an eval change

```bash
cmake -S engine/cpp -B /tmp/blokie && make -C /tmp/blokie golden -j
/tmp/blokie/golden --file engine/golden/golden.json
```

Compare `main` vs your branch to see if a weight or feature moves the needle before paying for a full `fitness` run.
