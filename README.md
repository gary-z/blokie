Latest version hosted at https://gary-z.github.io/blokie/ .

Blokie is a powerful AI/engine/solver for [Blockudoku](https://play.google.com/store/apps/details?id=com.easybrain.block.puzzle.games) and [Woodoku](https://play.google.com/store/apps/details?id=com.tripledot.woodoku&hl=en_CA&gl=US) puzzle games. It can achieve 300,000 points (roughly 20,000 sets of 3 pieces) half the time.

<img style="width: 25%; height: 15%" src="/preview.gif?raw=true"/>


## What can I learn from Blokie to improve at the game?
- Blokie will clear blocks almost every round. If your board is clean, there is almost always a way to clear, so look *very* hard before deciding to let blocks stack up.
- Blokie sees placing each set of 3 pieces as one move, rather than 3 individual moves. This lets it plan tricky clearing patterns and leave a clean board state. Try to visualize where you will place all 3 pieces before you place your first piece.
- Blokie's most critical component is its "board cleaniness" measurement. Roughly speaking in decreasing importance:
  - Minimize the number of blocks on the board.
  - Minimize the total perimeter of blocks.
  - Avoid jagged edges of blocks.
  - Avoid leaving a single empty space between two blocks.
  - Keep as many 3x3 cubes free as possible.

## Implementation details
Blokie looks at all possible board states resulting from places the 3 pieces and chooses the the move that results in the best "board cleaniness" score.

Internally, the board state is represented as a bitboard using three 32-bit integers.

The board state evaluation weights were trained using a genetic learning algorithm written in C++. JavaScript is too slow for training.

## Can I have the computer play for me?
No.
