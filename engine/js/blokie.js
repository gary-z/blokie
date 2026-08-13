"use strict";

// The engine, as the rest of the app sees it.
//
// The move search itself is C++ compiled to WASM, under engine/wasm. What lives
// here is everything around it: the bitboard the board is kept in, the rules for
// what a piece may cover and what that clears, and what a move scores. Those are
// written once, here, and a move made by hand and a move chosen by the search
// both go down the same path -- so the two can never come to disagree about what
// a move was worth.
//
// Two namespaces come out of this file. `bits` is the board as three words of
// squares, with no notion of a game attached; `blokie` is the game played on it.
// Anything that talks about a score, a hand or a move is in the second one.
//
// Types are JSDoc, checked by `npm run typecheck` and erased by nothing, since
// nothing compiles this file. See tsconfig.json for why it is arranged that way.

// === TYPES ===

/**
 * A 9x9 grid of squares as three 27-bit words: `a` holds rows 0-2, `b` rows
 * 3-5, `c` rows 6-8. Every square-shaped thing in the engine is one of these --
 * the board, a piece, the squares a piece covers, a row mask -- which is why
 * `Piece` and `Placement` below are names for this and not types of their own.
 * What one of them means is decided by where it came from.
 * @typedef {{a: number, b: number, c: number}} BitBoard
 */

/**
 * A piece, as the squares it fills. Left/top justified unless it came from
 * `bits.center`, which moves it for display and is the only thing that does.
 * @typedef {BitBoard} Piece
 */

/**
 * The squares a piece covers on the board it is being put on -- the piece, moved
 * to where it is going.
 * @typedef {BitBoard} Placement
 */

/**
 * The three pieces in hand. A slot that has been played holds an empty piece
 * until all three have been, and the hand is refilled.
 * @typedef {[Piece, Piece, Piece]} Hand
 */

/**
 * A position, and the one thing about the move before it that scoring needs.
 * Nothing else about how it was reached is kept: a `Move` says what the move
 * that produced it did, and nothing asks a game where it has been.
 * @typedef {object} Game
 * @property {BitBoard} board
 * @property {boolean} previous_move_was_clear Whether the move before this one
 *   cleared. Two clears in a row pay a streak bonus, which is the whole of why
 *   it is remembered.
 * @property {number} score
 */

/**
 * One piece landing: the squares it covered, and the game it left behind.
 * @typedef {{placement: Placement, new_game: Game}} Move
 */

/**
 * Where the search wants a piece to go: which slot of the hand to play, and the
 * squares it should cover.
 * @typedef {{piece_index: number, placement: Placement}} PlannedMove
 */

/**
 * What the search came back with.
 * @typedef {object} AIMove
 * @property {boolean} found Whether it found a way to place every piece it was
 *   given. When false, `moves` is empty and `game` is the game it was handed.
 * @property {number} evaluation What the board it settled on is worth. Lower is
 *   tidier.
 * @property {PlannedMove[]} moves In the order they should be played. Playing
 *   them in that order is always legal; other orders need not be.
 * @property {Game} game The game after all of them have been played.
 */

/**
 * How many rows and columns of the board a piece covers.
 * @typedef {{rows: number, cols: number}} PieceBounds
 */

/** @typedef {() => number} Random A source of numbers in [0, 1), like Math.random. */

// === THE WASM SOLVER ===

/** @type {import('../wasm/blokie-solver.js').BlokieSolver | null} */
let solver = null;
/** @type {Promise<void> | null} */
let _init_promise = null;

/**
 * Initialize the WASM solver. Must be called (and awaited) before `evaluate`,
 * `plan` or `makeMove`; everything else works without it.
 *
 *   - locateFile(filename): a function that returns the URL/path for a
 *     given filename (e.g. "blokie-solver.wasm"). Useful when the .wasm
 *     file is served from a CDN or custom asset directory.
 *
 *   - wasmBinary: an ArrayBuffer containing the pre-fetched .wasm binary.
 *     When provided, the loader skips its own fetch entirely.
 *
 * Examples:
 *   // Default - resolve .wasm relative to blokie-solver.js
 *   await init();
 *
 *   // Custom URL
 *   await init({ locateFile: () => '/assets/blokie-solver.wasm' });
 *
 *   // Pre-fetched binary
 *   const buf = await fetch('/assets/blokie-solver.wasm').then(r => r.arrayBuffer());
 *   await init({ wasmBinary: buf });
 *
 * @param {import('../wasm/blokie-solver.js').BlokieSolverOptions} [options]
 * @returns {Promise<void>}
 */
async function init(options = {}) {
    if (_init_promise) return _init_promise;

    _init_promise = (async () => {
        // Dynamic import so the module path is resolved relative to this file,
        // and so consumers who only use non-AI helpers can skip loading WASM.
        const { default: createBlokieSolver } = await import('../wasm/blokie-solver.js');

        /** @type {import('../wasm/blokie-solver.js').BlokieSolverOptions} */
        const module_config = {};
        if (options.locateFile) {
            module_config.locateFile = options.locateFile;
        }
        if (options.wasmBinary) {
            module_config.wasmBinary = options.wasmBinary;
        }

        solver = await createBlokieSolver(module_config);
    })();

    return _init_promise;
}

// The solver, or a readable error instead of a null dereference three frames
// down. Forgetting to await init() is the one mistake this API invites.
/** @returns {import('../wasm/blokie-solver.js').BlokieSolver} */
function requireSolver() {
    if (solver === null) {
        throw new Error('blokie: await init() before asking the engine for a move');
    }
    return solver;
}

// === BITBOARDS ===

const USED_BITS = 0x7FFFFFF;
const ROW_0 = 0x1FF;
const ROW_2 = ROW_0 << 18;
const LEFT_BITS = 1 | (1 << 9) | (1 << 18);
const RIGHT_BITS = LEFT_BITS << 8;
const TOP_LEFT_CUBE = 0x7 | (0x7 << 9) | (0x7 << 18);

/** @type {(a: number, b: number, c: number) => BitBoard} */
function bitboard(a, b, c) {
    return { a: a, b: b, c: c };
}

// Used when returning values so clients can't change out consts.
/** @type {() => BitBoard} */
function getEmpty() {
    return bitboard(0, 0, 0)
}
/** @type {() => BitBoard} */
function getFull() {
    return bitboard(USED_BITS, USED_BITS, USED_BITS);
}

const EMPTY = getEmpty();
const FULL = getFull();

/** @type {(x: number) => number} */
function _popcount(x) {
    x -= x >> 1 & 0x55555555
    x = (x & 0x33333333) + (x >> 2 & 0x33333333)
    x = x + (x >> 4) & 0x0f0f0f0f
    x += x >> 8
    x += x >> 16
    return x & 0x7f
}
/** @type {(bb: BitBoard) => number} */
function count(bb) {
    return _popcount(bb.a) + _popcount(bb.b) + _popcount(bb.c);
}
/** @type {(a: BitBoard, b: BitBoard) => boolean} */
function equal(a, b) {
    return a.a === b.a && a.b === b.b && a.c === b.c;
}
/** @type {(bb: BitBoard) => boolean} */
function any(bb) {
    return bb.a + bb.b + bb.c !== 0;
}
/** @type {(bb: BitBoard) => boolean} */
function is_empty(bb) {
    return !any(bb);
}
/** @type {(bb: BitBoard) => BitBoard} */
function not(bb) {
    return bitboard(~bb.a & USED_BITS, ~bb.b & USED_BITS, ~bb.c & USED_BITS);
}
/** @type {(a: BitBoard, b: BitBoard) => BitBoard} */
function and(a, b) {
    return bitboard(a.a & b.a, a.b & b.b, a.c & b.c);
}
/** @type {(a: BitBoard, b: BitBoard) => boolean} */
function is_disjoint(a, b) {
    return (a.a & b.a) === 0 && (a.b & b.b) === 0 && (a.c & b.c) === 0;
}
/** @type {(a: BitBoard, b: BitBoard) => BitBoard} */
function or(a, b) {
    return bitboard(a.a | b.a, a.b | b.b, a.c | b.c);
}
/** @type {(a: BitBoard, b: BitBoard) => BitBoard} */
function xor(a, b) {
    return and(or(a, b), not(and(a, b)));
}
/** @type {(a: BitBoard, b: BitBoard) => BitBoard} */
function diff(a, b) {
    return bitboard(a.a & ~b.a, a.b & ~b.b, a.c & ~b.c);
}
/** @type {(superset: BitBoard, b: BitBoard) => boolean} */
function is_subset(superset, b) {
    return (b.a & ~superset.a) === 0 && (b.b & ~superset.b) === 0
        && (b.c & ~superset.c) === 0;
}
/** @type {(r: number, c: number) => BitBoard} */
function bit(r, c) {
    return and(row(r), column(c));
}
/** @type {(bb: BitBoard, r: number, c: number) => boolean} */
function at(bb, r, c) {
    return !is_empty(and(bit(r, c), bb));
}
/** @type {(bb: BitBoard, r: number, c: number) => BitBoard} */
function toggle(bb, r, c) {
    return xor(bb, bit(r, c));
}

/** @type {(r: number) => BitBoard} */
function _row(r) {
    const result = [0, 0, 0];
    const m = r % 3;
    result[(r - m) / 3] = ROW_0 << (m * 9);
    return bitboard(result[0], result[1], result[2]);
}
const ROWS = Array.from({ length: 10 }, (_, i) => _row(i));
/** @type {(r: number) => BitBoard} */
function row(r) {
    return ROWS[r];
}

/** @type {(c: number) => BitBoard} */
function _column(c) {
    return bitboard(LEFT_BITS << c, LEFT_BITS << c, LEFT_BITS << c);
}
const COLS = Array.from({ length: 10 }, (_, i) => _column(i));
/** @type {(c: number) => BitBoard} */
function column(c) {
    return COLS[c];
}

/** @type {(i: number) => BitBoard} */
function _cube(i) {
    const result = [0, 0, 0];
    result[Math.floor(i / 3)] = TOP_LEFT_CUBE << ((i % 3) * 3);
    return bitboard(result[0], result[1], result[2]);
}
const CUBES = Array.from({ length: 10 }, (_, i) => _cube(i));
/** @type {(i: number) => BitBoard} */
function cube(i) {
    return CUBES[i];
}

/** @type {(bb: BitBoard) => BitBoard} */
function shift_right(bb) {
    return bitboard((bb.a & ~RIGHT_BITS) << 1, (bb.b & ~RIGHT_BITS) << 1, (bb.c & ~RIGHT_BITS) << 1);
}
/** @type {(bb: BitBoard) => BitBoard} */
function shift_left(bb) {
    return bitboard((bb.a & ~LEFT_BITS) >> 1, (bb.b & ~LEFT_BITS) >> 1, (bb.c & ~LEFT_BITS) >> 1);
}
/** @type {(bb: BitBoard) => BitBoard} */
function shift_down(bb) {
    return bitboard(
        (bb.a << 9) & USED_BITS,
        ((bb.b << 9) | ((bb.a & ROW_2) >> 18)) & USED_BITS,
        ((bb.c << 9) | ((bb.b & ROW_2) >> 18)) & USED_BITS,
    );
}
/** @type {(bb: BitBoard) => BitBoard} */
function shift_up(bb) {
    return bitboard(
        (bb.a >> 9) | ((bb.b & ROW_0) << 18),
        (bb.b >> 9) | ((bb.c & ROW_0) << 18),
        bb.c >> 9,
    );
}

// Pushed up and left until it touches both edges, which is the form every piece
// is compared and searched in. Two pieces are the same shape exactly when their
// justified bits are equal.
/** @type {(p: Piece) => Piece} */
function left_top_justify_piece(p) {
    if (is_empty(p)) {
        return p;
    }
    while (is_empty(and(p, row(0)))) {
        p = shift_up(p);
    }
    while (is_empty(and(p, column(0)))) {
        p = shift_left(p);
    }
    return p;
}

// Where a piece sits in the 5x5 box it is drawn in while in hand. Justifies first, so
// centering an already-centered piece leaves it where it is.
/** @type {(p: Piece) => Piece} */
function center_piece(p) {
    p = left_top_justify_piece(p);
    const bounds = get_piece_bounds(p);
    for (let i = 0; i < (5 - bounds.cols) / 2; ++i) {
        p = shift_right(p);
    }
    for (let i = 0; i < (5 - bounds.rows) / 2; ++i) {
        p = shift_down(p);
    }
    return p;
}

// How many rows and columns of the board a piece covers, wherever it is drawn
// in the 5x5 box a piece in hand is held in.
/** @type {(piece: Piece) => PieceBounds} */
function get_piece_bounds(piece) {
    const p = left_top_justify_piece(piece);
    let rows = 0;
    let cols = 0;
    for (let i = 0; i < 9; ++i) {
        if (any(and(p, row(i)))) {
            rows = i + 1;
        }
        if (any(and(p, column(i)))) {
            cols = i + 1;
        }
    }
    return { rows: rows, cols: cols };
}

// === PIECES ===

/** @type {Piece[]} */
const PIECES = [
    bitboard(1, 0, 0),
    bitboard(3, 0, 0),
    bitboard(513, 0, 0),
    bitboard(1025, 0, 0),
    bitboard(514, 0, 0),
    bitboard(7, 0, 0),
    bitboard(262657, 0, 0),
    bitboard(1049601, 0, 0),
    bitboard(263172, 0, 0),
    bitboard(515, 0, 0),
    bitboard(1537, 0, 0),
    bitboard(1538, 0, 0),
    bitboard(1027, 0, 0),
    bitboard(15, 0, 0),
    bitboard(262657, 1, 0),
    bitboard(1539, 0, 0),
    bitboard(786945, 0, 0),
    bitboard(3588, 0, 0),
    bitboard(525315, 0, 0),
    bitboard(519, 0, 0),
    bitboard(262659, 0, 0),
    bitboard(2055, 0, 0),
    bitboard(787458, 0, 0),
    bitboard(3585, 0, 0),
    bitboard(3586, 0, 0),
    bitboard(263681, 0, 0),
    bitboard(525826, 0, 0),
    bitboard(1031, 0, 0),
    bitboard(3075, 0, 0),
    bitboard(263682, 0, 0),
    bitboard(525825, 0, 0),
    bitboard(1542, 0, 0),
    bitboard(31, 0, 0),
    bitboard(262657, 513, 0),
    bitboard(265729, 0, 0),
    bitboard(525319, 0, 0),
    bitboard(1836034, 0, 0),
    bitboard(1052164, 0, 0),
    bitboard(2567, 0, 0),
    bitboard(787459, 0, 0),
    bitboard(786947, 0, 0),
    bitboard(3589, 0, 0),
    bitboard(262663, 0, 0),
    bitboard(1050631, 0, 0),
    bitboard(1837060, 0, 0),
    bitboard(1835521, 0, 0),
    bitboard(527874, 0, 0),
];

// A fresh set of three, justified. `random` is there so a harness can deal a
// repeatable sequence; the game itself has no reason to pass one.
//
// Where the pieces sit inside the 5x5 box they are drawn in is a question for
// whoever is drawing them -- see bits.center -- and not one the engine answers.
/** @type {(random?: Random) => Hand} */
function deal(random = Math.random) {
    const pick = () => PIECES[Math.floor(random() * PIECES.length)];
    return [pick(), pick(), pick()];
}

// === RULES ===

/** @type {(board: BitBoard) => BitBoard} */
function perform_clears(board) {
    let to_remove = EMPTY;
    for (let i = 0; i < 9; ++i) {
        const c = column(i);
        if (is_subset(board, c)) {
            to_remove = or(to_remove, c);
        }
        const r = row(i);
        if (is_subset(board, r)) {
            to_remove = or(to_remove, r);
        }
        const cb = cube(i);
        if (is_subset(board, cb)) {
            to_remove = or(to_remove, cb);
        }
    }
    if (is_empty(to_remove)) {
        return board;
    }

    return diff(board, to_remove);
}

// Returns true if `piece` fits anywhere on `board`. Walks the piece across the
// board the way the solver's enumeration does, but stops at the first fit
// instead of building every resulting board. The one thing over here that has
// to agree with the search: it is what says the game is over.
/** @type {(board: BitBoard, piece: Piece) => boolean} */
function can_place_piece(board, piece) {
    let p = left_top_justify_piece(piece);
    if (is_empty(p)) {
        return false;
    }

    let left = p;
    const col8 = column(8);
    const row8 = row(8);
    while (true) {
        if (is_disjoint(board, p)) {
            return true;
        }
        if (!is_disjoint(p, col8)) {
            if (!is_disjoint(left, row8)) {
                return false;
            }
            left = shift_down(left);
            p = left;
        } else {
            p = shift_right(p);
        }
    }
}

// A human player's game is over once none of the pieces in hand fit.
/** @type {(board: BitBoard, hand: readonly Piece[]) => boolean} */
function has_valid_move(board, hand) {
    return hand.some(p => can_place_piece(board, p));
}

// Where the fitness and performance harnesses stop: they deal a fresh hand every
// move, so the only thing that can end them is running out of board. A played
// game ends earlier and for a different reason -- see hasValidMove.
/** @type {(game: Game) => boolean} */
function is_over(game) {
    return equal(game.board, FULL);
}

// === SCORING AND PLACEMENT ===

/** @type {(mid_clear: BitBoard) => number} */
function get_combo_magnitude(mid_clear) {
    let result = 0;
    for (let i = 0; i < 9; ++i) {
        if (equal(row(i), and(row(i), mid_clear))) {
            result += 1;
        }
        if (equal(column(i), and(column(i), mid_clear))) {
            result += 1;
        }
        if (equal(cube(i), and(cube(i), mid_clear))) {
            result += 1;
        }
    }

    return result;
}

// How many rows, columns and cubes a placement completes at once.
/** @type {(board: BitBoard, placement: Placement) => number} */
function get_placement_combo_magnitude(board, placement) {
    return get_combo_magnitude(or(board, placement));
}

/** @type {(previous_was_clear: boolean, prev: BitBoard, placement: Placement, after: BitBoard) => number} */
function get_move_score(previous_was_clear, prev, placement, after) {
    // 1 point for each block placed that was not cleared.
    let result = count(diff(after, prev));
    const combo = get_placement_combo_magnitude(prev, placement);
    if (combo === 0) {
        return result;
    }

    // Streak
    if (previous_was_clear) {
        result += 9;
    }

    if (combo <= 2) {
        result += 18 * combo;
    } else if (combo <= 4) {
        result += 36 * combo;
    } else if (combo <= 7) {
        result += 54 * combo;
        // Not sure if 7x combo is correct.
    } else {
        result += 72 * combo;
    }

    return result;
}

// Lands a piece on the board, where `placement` is the squares it covers in
// board coordinates. The one place a move's board, score and clear are worked
// out, whether a hand or the AI chose it. Returns null if the placement does
// not fit, which is how a stale plan and a misdropped piece both read.
/** @type {(game: Game, placement: Placement) => Move | null} */
function place(game, placement) {
    if (is_empty(placement)) return null;
    if (!is_disjoint(game.board, placement)) return null;

    const new_board = perform_clears(or(game.board, placement));
    const move_score = get_move_score(
        game.previous_move_was_clear, game.board, placement, new_board);
    return {
        placement: placement,
        new_game: {
            board: new_board,
            previous_move_was_clear:
                count(new_board) < count(game.board) + count(placement),
            score: game.score + move_score,
        },
    };
}

// The same thing by where the piece's top left corner lands, which is what a
// drag knows. Returns null if the piece would hang off the board.
/** @type {(game: Game, piece: Piece, row: number, col: number) => Move | null} */
function place_at(game, piece, row, col) {
    if (row < 0 || col < 0) return null;
    let p = left_top_justify_piece(piece);
    const original_count = count(p);
    if (original_count === 0) return null;
    for (let i = 0; i < col; i++) p = shift_right(p);
    for (let i = 0; i < row; i++) p = shift_down(p);
    if (count(p) !== original_count) return null;
    return place(game, p);
}

// What a piece under a finger should be taken to mean. `row` and `col` say
// where the piece's top left square is being held, measured in board squares
// down and right of the board's top left corner, and are fractional because a
// piece being dragged sits between squares rather than on one.
//
// Answers with the legal placement whose corner is nearest to that, so a piece
// held slightly over an occupied square, or slightly off the edge of the board,
// lands in the closest square it does fit in instead of nowhere at all. Only
// the square directly under the piece would be an exact reading of the drag,
// and it is the one this picks whenever the piece fits there; the rest of the
// time an exact reading is a placement the player cannot have meant.
//
// `max_distance` is how far, in squares, the piece may be pulled to reach a
// placement. Past it the drag is somewhere else entirely and any placement
// would be a guess, so the answer is null -- the same as it is for a piece that
// fits nowhere. Otherwise this returns what place gives back for the square it
// settled on, exactly as placeAt does for a named one.
/** @type {(game: Game, piece: Piece, row: number, col: number, max_distance: number) => Move | null} */
function place_nearest(game, piece, row, col, max_distance) {
    const justified = left_top_justify_piece(piece);
    if (is_empty(justified)) {
        return null;
    }
    const bounds = get_piece_bounds(justified);

    // Squared throughout, so the walk below compares distances without paying
    // for a square root at every square of the board.
    /** @type {Placement | null} */
    let best = null;
    let best_distance_squared = max_distance * max_distance;

    // Every placement of the piece that is on the board at all, which is every
    // corner it can sit in without hanging off an edge.
    let row_start = justified;
    for (let r = 0; r + bounds.rows <= 9; ++r) {
        const dr = r - row;
        let p = row_start;
        for (let c = 0; c + bounds.cols <= 9; ++c) {
            const dc = c - col;
            const distance_squared = dr * dr + dc * dc;
            if (distance_squared <= best_distance_squared && is_disjoint(game.board, p)) {
                best = p;
                best_distance_squared = distance_squared;
            }
            p = shift_right(p);
        }
        row_start = shift_down(row_start);
    }

    return best === null ? null : place(game, best);
}

// === THE AI ===

// What the solver thinks of a board with nothing placed on it: lower is
// tidier, and it is the whole of how the search picks between boards.
/** @type {(board: BitBoard) => number} */
function evaluate(board) {
    return requireSolver().evaluate(board.a, board.b, board.c);
}

// The six orders three slots can be played in, walked in this order. Orderings
// that score the same are settled by the last one seen, so the order here is
// part of which move comes back.
const _SLOT_ORDERS = [
    [0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0],
];

// Which slot of the hand each of the search's placements was made with. A
// placement is its piece moved onto the board, so justifying it gives the piece
// back. Two slots holding the same shape are interchangeable, so the first one
// still free is as good as either.
//
// Answers with the placement each slot takes, and the slots in the order the
// search played them.
/** @type {(hand: Hand, placements: readonly Placement[]) => {placement_of_slot: Placement[], search_order: number[]}} */
function _match_placements(hand, placements) {
    const placement_of_slot = [getEmpty(), getEmpty(), getEmpty()];
    /** @type {number[]} */
    const search_order = [];
    const taken = [false, false, false];
    for (const placement of placements) {
        const shape = left_top_justify_piece(placement);
        for (let slot = 0; slot < 3; ++slot) {
            if (!taken[slot] && equal(hand[slot], shape)) {
                taken[slot] = true;
                placement_of_slot[slot] = placement;
                search_order.push(slot);
                break;
            }
        }
    }
    return { placement_of_slot: placement_of_slot, search_order: search_order };
}

// Plays the slots in `order`, each into the placement the search picked for it.
// Answers with the game they leave and the moves that got there, or null when
// one of them does not fit where it is being put -- which is how an ordering
// that only works because an earlier placement cleared a line reads when the
// clear has not happened yet.
//
// A blank slot is not a move: it neither clears nor breaks the run the move
// before it started, so it is stepped over rather than played. Reading it as a
// move that failed to clear would cost the streak bonus on the move after it.
/** @type {(game: Game, placement_of_slot: readonly Placement[], order: readonly number[]) => {game: Game, moves: PlannedMove[]} | null} */
function _play_order(game, placement_of_slot, order) {
    /** @type {PlannedMove[]} */
    const moves = [];
    let current = game;
    for (const slot of order) {
        const placement = placement_of_slot[slot];
        if (is_empty(placement)) {
            continue;
        }
        const move = place(current, placement);
        if (move === null) {
            return null;
        }
        current = move.new_game;
        moves.push({ piece_index: slot, placement: placement });
    }
    return { game: current, moves: moves };
}

// The best move the solver can find. The search decides where the pieces go;
// which piece goes in which of those placements, what each move scores and the
// order they are played in are worked out here, down the same path a move made
// by hand takes -- so the rules of the game are written once, in one language.
/** @type {(game: Game, hand: Hand) => AIMove} */
function make_move(game, hand) {
    /** @type {Hand} */
    const justified = [
        left_top_justify_piece(hand[0]),
        left_top_justify_piece(hand[1]),
        left_top_justify_piece(hand[2]),
    ];
    const board = game.board;

    const result = requireSolver().aiMakeMove(
        board.a, board.b, board.c,
        justified[0].a, justified[0].b, justified[0].c,
        justified[1].a, justified[1].b, justified[1].c,
        justified[2].a, justified[2].b, justified[2].c,
    );
    /** @type {AIMove} */
    const nothing = {
        found: false, evaluation: result.evaluation, moves: [], game: game,
    };
    if (!result.found) {
        return nothing;
    }

    const { placement_of_slot, search_order } =
        _match_placements(justified, result.placements);
    // Every placement belongs to a piece that was in hand. One that does not
    // means the search answered about a hand it was not asked about, and there
    // is no move here to play.
    const searched = search_order.length === 3
        ? _play_order(game, placement_of_slot, search_order)
        : null;
    if (searched === null) {
        return nothing;
    }

    // The order the search played them in always fits, so it settles the board
    // this move ends on. Every other order has to reach that same board to be
    // the same move at all: a clear part way through takes squares a later
    // piece was going to sit on away with it.
    const target = searched.game.board;
    let best = searched;
    for (const order of _SLOT_ORDERS) {
        const played = _play_order(game, placement_of_slot, order);
        if (played === null || !equal(played.game.board, target)) {
            continue;
        }
        if (played.game.score < best.game.score) {
            continue;
        }
        // A tie goes to the order that ends on a clear, which is the one that
        // carries a streak into the move after it.
        if (played.game.score === best.game.score
            && !played.game.previous_move_was_clear) {
            continue;
        }
        best = played;
    }

    return {
        found: true,
        evaluation: result.evaluation,
        moves: best.moves,
        game: best.game,
    };
}

// The subsets of `indices`, biggest first. Only ever called with the slots of a
// three piece hand, so this is at most seven short lists.
/** @type {(indices: readonly number[]) => number[][]} */
function _subsets_largest_first(indices) {
    /** @type {number[][]} */
    const result = [];
    for (let mask = 1; mask < (1 << indices.length); ++mask) {
        result.push(indices.filter((_, i) => mask & (1 << i)));
    }
    return result.sort((a, b) => b.length - a.length);
}

// Where the AI wants to put the pieces in hand, in the order they should be
// played. Empty when nothing fits at all, which is the same thing hasValidMove
// says about the position.
//
// The solver only ever plans moves that place every piece it is given, and says
// it found nothing when it cannot. That is not the end of the game -- one or two
// of the pieces usually still fit -- so ask again for a smaller handful. A
// blanked slot is a no-op the search steps straight over, which is how a two- or
// one-piece move gets planned at all.
/** @type {(game: Game, hand: Hand) => PlannedMove[]} */
function plan(game, hand) {
    const held = [0, 1, 2].filter(i => !is_empty(hand[i]));
    /** @type {AIMove | null} */
    let best = null;

    for (const subset of _subsets_largest_first(held)) {
        // Nothing left to try can place more pieces than we already have.
        if (best !== null && subset.length <= best.moves.length) {
            break;
        }
        /** @type {Hand} */
        const asked = [
            subset.includes(0) ? hand[0] : EMPTY,
            subset.includes(1) ? hand[1] : EMPTY,
            subset.includes(2) ? hand[2] : EMPTY,
        ];
        const result = make_move(game, asked);
        if (result.moves.length === 0) {
            continue;
        }
        // More pieces played beats a tidier board, since the hand only refills
        // once every slot is empty. Between equals, take the tidier board.
        if (best === null || result.moves.length > best.moves.length
            || (result.moves.length === best.moves.length
                && result.evaluation < best.evaluation)) {
            best = result;
        }
    }

    return best === null ? [] : best.moves;
}

// === GAMES ===

/** @type {() => Game} */
function new_game() {
    return {
        board: getEmpty(),
        previous_move_was_clear: false,
        score: 0,
    };
}

// === THE PUBLIC API ===

// The board as squares, with no game attached. Everything here is a pure
// function of bitboards, and none of it needs the solver loaded.
const bits = {
    empty: getEmpty,
    full: getFull,
    isEmpty: is_empty,
    equals: equal,
    count: count,
    at: at,
    toggle: toggle,
    union: or,
    justify: left_top_justify_piece,
    center: center_piece,
    bounds: get_piece_bounds,
};

// The game played on it. `evaluate`, `plan` and `makeMove` need init() to have
// been awaited; the rest do not.
const blokie = {
    newGame: new_game,
    deal: deal,
    isOver: is_over,
    canPlace: can_place_piece,
    hasValidMove: has_valid_move,
    place: place,
    placeAt: place_at,
    placeNearest: place_nearest,
    comboMagnitude: get_placement_combo_magnitude,
    evaluate: evaluate,
    plan: plan,
    makeMove: make_move,
};

// The bitboard layer underneath, for test/engine/bitboard-test.js. These used
// to be checked by console.assert calls sitting at the top level of this file,
// which meant every page load and every worker start paid for a run of the
// engine's unit tests before it could draw anything. Nothing outside the tests
// should reach in here.
const _internals = {
    bitboard, getEmpty, getFull, EMPTY, FULL, USED_BITS, ROW_0, TOP_LEFT_CUBE,
    _popcount, count, equal, any, is_empty, not, and, or, xor, diff, is_subset,
    is_disjoint, bit, at, row, column, cube,
    shift_left, shift_right, shift_up, shift_down,
    PIECES, perform_clears, can_place_piece, has_valid_move,
    get_combo_magnitude, center_piece, left_top_justify_piece, get_piece_bounds,
    place_nearest, new_game,
};

export { blokie, bits, init, _internals };
