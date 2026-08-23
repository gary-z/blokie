Blokie is an AI solver for [Blockudoku](https://play.google.com/store/apps/details?id=com.easybrain.block.puzzle.games), [Woodoku](https://play.google.com/store/apps/details?id=com.tripledot.woodoku&hl=en_CA&gl=US), and [Block Sudoku](https://play.google.com/store/apps/details?id=block.puzzle.sudoku.free.game.classic.offline). The current solver plays about 110,000 sets of three pieces per game on average.

<img style="width: 25%; height: 15%" src="/docs/preview.gif?raw=true"/>

## Playing well

### Plan all three pieces

Blokie treats each set of three pieces as one move and considers every legal
ordering and placement. Before placing the first piece, decide where all three
will go.

### Keep the board clear

A clean board leaves more options for awkward pieces. In decreasing order of
importance:

- Minimize occupied squares.
- Minimize the perimeter and jagged edges of occupied areas.
- Avoid single-square gaps.
- Keep 3×3 regions available.
- Preserve several ways to complete a row, column, or 3×3 region.

## Implementation

The board is a bitboard backed by three 32-bit integers. The solver enumerates
the reachable boards for a set of pieces and chooses the lowest evaluation.
The native C++ engine is also compiled to WebAssembly for the browser.

See [Running evaluation experiments](docs/evaluating-changes.md) to build,
test, benchmark, and compare evaluator changes.

## Can the solver play phone games automatically?

No.

## Why did I build this?

Cause a mermaid was better than me at this game.

## Disclaimer

Blokie is an independent, unofficial project. It is not affiliated with,
endorsed by, or sponsored by Easybrain, Tripledot Studios, or the developers
of Block Sudoku.

Blockudoku is a trademark of Easybrain Ltd. Woodoku is a trademark of Tripledot
Studios Limited. All trademarks belong to their respective owners and are used
only to identify compatible games.
