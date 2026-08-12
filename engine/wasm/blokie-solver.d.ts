// What the WASM module next to this file exports, written by hand.
//
// blokie-solver.js is Emscripten's generated loader and blokie-solver.wasm is
// the compiled solver; both are built by engine/build-wasm.sh and committed,
// and check-wasm.yml rebuilds them to prove the committed copies match the C++.
// This file is not generated with them -- the build copies only the .js and the
// .wasm, so it is left alone -- and it has to be updated by hand when the
// bindings in engine/cpp/bindings.cpp change.
//
// It exists for two reasons. TypeScript prefers a declaration file to the .js
// beside it, which keeps a hundred kilobytes of generated loader out of the
// checked program. And it is the one place the boundary between the two
// languages is written down in a form something checks: the search runs in
// WASM, the rules of the game run in JS, and these three functions are the
// whole of what passes between them.

/**
 * A 9x9 board as three 27-bit words, matching the BitBoard the engine's JS
 * side passes around: `a` holds rows 0-2, `b` rows 3-5, `c` rows 6-8.
 */
export interface SolverBitBoard {
  a: number;
  b: number;
  c: number;
}

/**
 * Where the search put the three pieces, and what the board it settled on is
 * worth. `placements` is in the order the search played them, which is what
 * makes replaying it in that order legal -- see the comment on aiMakeMove in
 * engine/cpp/bindings.cpp. Blank slots come back as empty placements.
 */
export interface SolverMoveResult {
  /** False when no line of play places every piece it was given. */
  found: boolean;
  evaluation: number;
  /** Always three entries, one per slot of the deck. */
  placements: SolverBitBoard[];
}

export interface BlokieSolver {
  /**
   * What the search thinks of a board with nothing placed on it. Lower is
   * tidier, and it is the whole of how the search picks between boards.
   */
  evaluate(board_a: number, board_b: number, board_c: number): number;

  /** The pieces must already be left/top justified. */
  aiMakeMove(
    board_a: number, board_b: number, board_c: number,
    p0_a: number, p0_b: number, p0_c: number,
    p1_a: number, p1_b: number, p1_c: number,
    p2_a: number, p2_b: number, p2_c: number,
  ): SolverMoveResult;
}

/**
 * The subset of Emscripten's module configuration engine/js/blokie.js passes
 * on. See the comment on init() there for what each one is for.
 */
export interface BlokieSolverOptions {
  locateFile?: (filename: string) => string;
  wasmBinary?: ArrayBuffer;
}

export default function createBlokieSolver(
  options?: BlokieSolverOptions,
): Promise<BlokieSolver>;
