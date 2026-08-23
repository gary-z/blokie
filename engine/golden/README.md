# Golden board pairs

`golden.json` contains directional examples for the evaluator. Each entry says
board `a` is preferable to board `b`; lower evaluation is better.

The corpus is useful for finding missing or reversed signals. It is not a
fitness function and should not be used to tune weights. Measure evaluator
changes with [`fitness`](../../docs/evaluating-changes.md).

## Tools

Build the native tools, then run:

```bash
./golden
./golden --verbose
./golden --json
./golden --strict
./golden_measure
```

`golden` evaluates each pair. `golden_measure` enumerates the next dealt set and
runs short paired rollouts to check the label against real play. A pair may
intentionally fail `golden`; that identifies a behavior worth investigating.

## Format

`golden.json` is an array of objects with a unique `id`, a short `description`,
and two boards. A board is nine strings of nine characters: `.` is empty and
`#` is occupied.

```json
{
  "id": "keep-center-open",
  "description": "Keeping the center open leaves room for large pieces.",
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
```

Keep both boards at equal occupancy and below a completed row, column, or 3×3
region. Prefer sibling boards produced by placing the same piece two ways from
one parent. Run `golden_measure` before committing a new pair.

## Editor

Serve the repository root and open the editor:

```bash
python3 -m http.server 8000
```

Open `http://localhost:8000/engine/golden/editor.html`. The editor validates
board shape and occupancy, evaluates both boards with the committed WebAssembly
solver, and exports the JSON corpus.
