"use strict";

// WASM module - initialized via init().
let solver = null;
let _initPromise = null;

/**
 * Initialize the WASM solver. Must be called (and awaited) before using
 * any AI functions. Accepts an optional config object:
 *
 *   - locateFile(filename): a function that returns the URL/path for a
 *     given filename (e.g. "blokie-solver.wasm"). Useful when the .wasm
 *     file is served from a CDN or custom asset directory.
 *
 *   - wasmBinary: an ArrayBuffer containing the pre-fetched .wasm binary.
 *     When provided, the loader skips its own fetch entirely.
 *
 * Examples:
 *   // Default – resolve .wasm relative to blokie-solver.js
 *   await init();
 *
 *   // Custom URL
 *   await init({ locateFile: () => '/assets/blokie-solver.wasm' });
 *
 *   // Pre-fetched binary
 *   const buf = await fetch('/assets/blokie-solver.wasm').then(r => r.arrayBuffer());
 *   await init({ wasmBinary: buf });
 */
async function init(options = {}) {
    if (_initPromise) return _initPromise;

    _initPromise = (async () => {
        // Dynamic import so the module path is resolved relative to this file,
        // and so consumers who only use non-AI helpers can skip loading WASM.
        const { default: createBlokieSolver } = await import('../wasm/blokie-solver.js');

        const moduleConfig = {};
        if (options.locateFile) {
            moduleConfig.locateFile = options.locateFile;
        }
        if (options.wasmBinary) {
            moduleConfig.wasmBinary = options.wasmBinary;
        }

        solver = await createBlokieSolver(moduleConfig);
    })();

    return _initPromise;
}

// === BITBOARD FUNCTIONS
const USED_BITS = 0x7FFFFFF;
const ROW_0 = 0x1FF;
const ROW_2 = ROW_0 << 18;
const LEFT_BITS = 1 | (1 << 9) | (1 << 18);
const RIGHT_BITS = LEFT_BITS << 8;
const TOP_LEFT_CUBE = 0x7 | (0x7 << 9) | (0x7 << 18);

function bitboard(a, b, c) {
    return { a: a, b: b, c: c };
}

// Used when returning values so clients can't change out consts.
function getEmpty() {
    return bitboard(0, 0, 0)
}
function getFull() {
    return bitboard(USED_BITS, USED_BITS, USED_BITS);
}
function copy(bb) {
    return bitboard(bb.a, bb.b, bb.c);
}

const EMPTY = getEmpty();
const FULL = getFull();

function _popcount(x) {
    x -= x >> 1 & 0x55555555
    x = (x & 0x33333333) + (x >> 2 & 0x33333333)
    x = x + (x >> 4) & 0x0f0f0f0f
    x += x >> 8
    x += x >> 16
    return x & 0x7f
}
function count(bb) {
    return _popcount(bb.a) + _popcount(bb.b) + _popcount(bb.c);
}
function equal(a, b) {
    return a.a === b.a && a.b === b.b && a.c === b.c;
}
function any(bb) {
    return bb.a + bb.b + bb.c !== 0;
}
function is_empty(bb) {
    return !any(bb);
}
function not(bb) {
    return bitboard(~bb.a & USED_BITS, ~bb.b & USED_BITS, ~bb.c & USED_BITS);
}
function and(a, b) {
    return bitboard(a.a & b.a, a.b & b.b, a.c & b.c);
}
function is_disjoint(a, b) {
    return (a.a & b.a) === 0 && (a.b & b.b) === 0 && (a.c & b.c) === 0;
}
function or(a, b) {
    return bitboard(a.a | b.a, a.b | b.b, a.c | b.c);
}
function xor(a, b) {
    return and(or(a, b), not(and(a, b)));
}
function diff(a, b) {
    return bitboard(a.a & ~b.a, a.b & ~b.b, a.c & ~b.c);
}
function is_subset(a/*superset*/, b) {
    return (b.a & ~a.a) === 0 && (b.b & ~a.b) === 0 && (b.c & ~a.c) === 0;
}
function bit(r, c) {
    return and(row(r), column(c));
}
function at(bb, r, c) {
    return !is_empty(and(bit(r, c), bb));
}

function _row(r) {
    const result = [0, 0, 0];
    const m = r % 3;
    result[(r - m) / 3] = ROW_0 << (m * 9);
    return bitboard(...result);
}
const ROWS = Array.from({ length: 10 }, (_, i) => _row(i));
function row(r) {
    return ROWS[r];
}

function _column(c) {
    return bitboard(LEFT_BITS << c, LEFT_BITS << c, LEFT_BITS << c);
}
const COLS = Array.from({ length: 10 }, (_, i) => _column(i));
function column(c) {
    return COLS[c];
}

function _cube(i) {
    const result = [0, 0, 0];
    result[Math.floor(i / 3)] = TOP_LEFT_CUBE << ((i % 3) * 3);
    return bitboard(...result);
}
const CUBES = Array.from({ length: 10 }, (_, i) => _cube(i));
function cube(i) {
    return CUBES[i];
}

function shift_right(bb) {
    return bitboard((bb.a & ~RIGHT_BITS) << 1, (bb.b & ~RIGHT_BITS) << 1, (bb.c & ~RIGHT_BITS) << 1);
}
function shift_left(bb) {
    return bitboard((bb.a & ~LEFT_BITS) >> 1, (bb.b & ~LEFT_BITS) >> 1, (bb.c & ~LEFT_BITS) >> 1);
}
function shift_down(bb) {
    return bitboard(
        (bb.a << 9) & USED_BITS,
        ((bb.b << 9) | ((bb.a & ROW_2) >> 18)) & USED_BITS,
        ((bb.c << 9) | ((bb.b & ROW_2) >> 18)) & USED_BITS,
    );
}
function shift_up(bb) {
    return bitboard(
        (bb.a >> 9) | ((bb.b & ROW_0) << 18),
        (bb.b >> 9) | ((bb.c & ROW_0) << 18),
        bb.c >> 9,
    );
}
// === PIECES
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
function get_random_piece() {
    return copy(PIECES[Math.floor(Math.random() * PIECES.length)]);
}
function get_random_piece_set() {
    return [get_random_piece(), get_random_piece(), get_random_piece()];
}

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

// A human player's game is over once none of the pieces on deck fit.
function has_valid_move(board, piece_set) {
    return piece_set.some(p => can_place_piece(board, p));
}

// === SCORING AND PLACEMENT ===

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

function get_placement_combo_magnitude(prev, placement) {
    return get_combo_magnitude(or(prev, placement));
}

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


// Lands `piece` on the board, where `placement` is the squares it covers in
// board coordinates. The one place a move's board, score and clear are worked
// out, whether a hand or the AI chose it. Returns null if the placement does
// not fit, which is how a stale plan and a misdropped piece both read.
function place_piece(game, piece, placement) {
    if (is_empty(placement)) return null;
    if (!is_disjoint(game.board, placement)) return null;

    const new_board = perform_clears(or(game.board, placement));
    const move_score = get_move_score(
        game.previous_move_was_clear, game.board, placement, new_board);
    return {
        placement: placement,
        newGame: {
            board: new_board,
            previous_piece_placement: placement,
            previous_piece: piece,
            previous_move_was_clear:
                count(new_board) < count(game.board) + count(placement),
            score: game.score + move_score,
        }
    };
}

// The same thing by where the piece's top left corner lands, which is what a
// drag knows. Returns null if the piece would hang off the board.
function try_place_piece(game, piece, dr, dc) {
    if (dr < 0 || dc < 0) return null;
    let p = left_top_justify_piece(piece);
    const original_count = count(p);
    if (original_count === 0) return null;
    for (let i = 0; i < dc; i++) p = shift_right(p);
    for (let i = 0; i < dr; i++) p = shift_down(p);
    if (count(p) !== original_count) return null;
    return place_piece(game, piece, p);
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
// fits nowhere. Otherwise this returns what place_piece gives back for the
// square it settled on, exactly as try_place_piece does for a named one.
function nearest_valid_placement(game, piece, row, col, max_distance) {
    const justified = left_top_justify_piece(piece);
    if (is_empty(justified)) {
        return null;
    }
    const bounds = get_piece_bounds(justified);

    // Squared throughout, so the walk below compares distances without paying
    // for a square root at every square of the board.
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

    return best === null ? null : place_piece(game, piece, best);
}

// === AI (powered by WASM) ===

// What the solver thinks of a board with nothing placed on it: lower is
// tidier, and it is the whole of how the search picks between boards.
function evaluate(board) {
    return solver.evaluate(board.a, board.b, board.c);
}

// The six orders three slots can be played in, walked in this order. Orderings
// that score the same are settled by the last one seen, so the order here is
// part of which move comes back.
const _SLOT_ORDERS = [
    [0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0],
];

// Which slot of the deck each of the search's placements was made with. A
// placement is its piece moved onto the board, so justifying it gives the piece
// back. Two slots holding the same shape are interchangeable, so the first one
// still free is as good as either.
//
// Answers with the placement each slot takes, and the slots in the order the
// search played them.
function _match_placements(piece_set, placements) {
    const placement_of_slot = [getEmpty(), getEmpty(), getEmpty()];
    const search_order = [];
    const taken = [false, false, false];
    for (const placement of placements) {
        const shape = left_top_justify_piece(placement);
        for (let slot = 0; slot < 3; ++slot) {
            if (!taken[slot] && equal(piece_set[slot], shape)) {
                taken[slot] = true;
                placement_of_slot[slot] = placement;
                search_order.push(slot);
                break;
            }
        }
    }
    return { placement_of_slot: placement_of_slot, search_order: search_order };
}

// Plays the slots in `order`, each into the placement the search picked for it,
// and answers with the state every move leaves behind. Null when one of them
// does not fit where it is being put, which is how an ordering that only works
// because an earlier placement cleared a line reads when the clear has not
// happened yet.
function _play_placements(game, piece_set, placement_of_slot, order) {
    const states = [];
    let current = game;
    for (const slot of order) {
        const placement = placement_of_slot[slot];
        // A blank slot is not a move: it neither clears nor breaks the run the
        // move before it started. Reading it as a move that failed to clear
        // would cost the streak bonus on the move after it.
        if (is_empty(placement)) {
            states.push({
                board: current.board,
                previous_piece_placement: getEmpty(),
                piece_index: slot,
                previous_move_was_clear: current.previous_move_was_clear,
                score: current.score,
            });
            continue;
        }
        const move = place_piece(current, piece_set[slot], placement);
        if (move === null) {
            return null;
        }
        current = move.newGame;
        states.push({
            board: current.board,
            previous_piece_placement: placement,
            piece_index: slot,
            previous_move_was_clear: current.previous_move_was_clear,
            score: current.score,
        });
    }
    return states;
}

// Nothing the solver was given can be placed. That is a full board and nothing
// played -- and the score the game came in with, since nothing was played to
// change it.
function _no_move(game, original_piece_set, evaluation) {
    return {
        evaluation: evaluation,
        new_game_states: [0, 1, 2].map(i => ({
            board: getFull(),
            previous_piece_placement: getEmpty(),
            piece_index: i,
            previous_piece: original_piece_set[i],
            previous_move_was_clear: false,
            score: game.score,
        })),
    };
}

// The best move the solver can find, as the three states playing it leaves
// behind. The search decides where the pieces go; which piece goes in which of
// those placements, what each move scores and the order they are played in are
// worked out here, down the same path a move made by hand takes -- so the rules
// of the game are written once, in one language.
function ai_make_move(game, original_piece_set) {
    const piece_set = original_piece_set.map(p => left_top_justify_piece(p));
    const board = game.board;

    const result = solver.aiMakeMove(
        board.a, board.b, board.c,
        piece_set[0].a, piece_set[0].b, piece_set[0].c,
        piece_set[1].a, piece_set[1].b, piece_set[1].c,
        piece_set[2].a, piece_set[2].b, piece_set[2].c,
    );
    if (!result.found) {
        return _no_move(game, original_piece_set, result.evaluation);
    }

    const { placement_of_slot, search_order } =
        _match_placements(piece_set, result.placements);
    // Every placement belongs to a piece that was on deck. One that does not
    // means the search answered about a deck it was not asked about, and there
    // is no move here to play.
    const searched = search_order.length === 3
        ? _play_placements(game, piece_set, placement_of_slot, search_order)
        : null;
    if (searched === null) {
        return _no_move(game, original_piece_set, result.evaluation);
    }

    // The order the search played them in always fits, so it settles the board
    // this move ends on. Every other order has to reach that same board to be
    // the same move at all: a clear part way through takes squares a later
    // piece was going to sit on away with it.
    const target = searched[2].board;
    let best = null;
    for (const order of _SLOT_ORDERS) {
        const states = _play_placements(game, piece_set, placement_of_slot, order);
        if (states === null || !equal(states[2].board, target)) {
            continue;
        }
        if (best !== null) {
            if (states[2].score < best[2].score) {
                continue;
            }
            // A tie goes to the order that ends on a clear, which is the one
            // that carries a streak into the move after it.
            if (states[2].score === best[2].score
                && !states[2].previous_move_was_clear) {
                continue;
            }
        }
        best = states;
    }

    return {
        evaluation: result.evaluation,
        new_game_states: best.map(state => ({
            board: state.board,
            previous_piece_placement: state.previous_piece_placement,
            piece_index: state.piece_index,
            previous_piece: original_piece_set[state.piece_index],
            previous_move_was_clear: state.previous_move_was_clear,
            score: state.score,
        })),
    };
}

// The subsets of `indices`, biggest first. Only ever called with the slots of a
// three piece deck, so this is at most seven short lists.
function _subsets_largest_first(indices) {
    const result = [];
    for (let mask = 1; mask < (1 << indices.length); ++mask) {
        result.push(indices.filter((_, i) => mask & (1 << i)));
    }
    return result.sort((a, b) => b.length - a.length);
}

// Where the AI wants to put the pieces on deck, as `{ piece_index, placement }`
// in the order they should be played. Empty when nothing fits at all, which is
// the same thing has_valid_move says about the position.
//
// The solver only ever plans moves that place every piece it is given, and
// answers with a full board and nothing placed when it cannot find one. That is
// not the end of the game -- one or two of the pieces usually still fit -- so
// ask again for a smaller handful. A blanked slot is a no-op the search steps
// straight over, which is how a two- or one-piece move gets planned at all.
function get_ai_plan(game, piece_set) {
    const held = [0, 1, 2].filter(i => !is_empty(piece_set[i]));
    let best = null;

    for (const subset of _subsets_largest_first(held)) {
        // Nothing left to try can place more pieces than we already have.
        if (best !== null && subset.length <= best.moves.length) {
            break;
        }
        const asked = [0, 1, 2].map(i => subset.includes(i) ? piece_set[i] : getEmpty());
        const result = ai_make_move(game, asked);
        const moves = result.new_game_states
            .filter(s => !is_empty(s.previous_piece_placement))
            .map(s => ({ piece_index: s.piece_index, placement: s.previous_piece_placement }));
        if (moves.length === 0) {
            continue;
        }
        // More pieces played beats a tidier board, since the deck only refills
        // once every slot is empty. Between equals, take the tidier board.
        if (best === null || moves.length > best.moves.length
            || (moves.length === best.moves.length && result.evaluation < best.evaluation)) {
            best = { moves: moves, evaluation: result.evaluation };
        }
    }

    return best === null ? [] : best.moves;
}

// === GAME UTILITIES ===

function get_new_game() {
    return {
        board: getEmpty(),
        previous_piece_placement: getEmpty(),
        previous_piece: getEmpty(),
        previous_move_was_clear: false,
        score: 0,

    };
}

function center_piece(p) {
    let height = 0;
    let width = 0;
    for (let i = 0; i < 9; ++i) {
        if (any(and(p, row(i)))) {
            height = i + 1;
        }
        if (any(and(p, column(i)))) {
            width = i + 1;
        }
    }
    for (let i = 0; i < (5 - width) / 2; ++i) {
        p = shift_right(p);
    }
    for (let i = 0; i < (5 - height) / 2; ++i) {
        p = shift_down(p);
    }
    return p;
}

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

// How many rows and columns of the board a piece covers, wherever it is drawn
// in the 5x5 box a piece on deck is held in.
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

// Where the fitness and performance harnesses below stop: they deal a fresh
// deck every move, so the only thing that can end them is running out of board.
// A played game ends earlier and for a different reason -- see has_valid_move.
function is_over(game) {
    return equal(game.board, FULL);
}

function get_fitness_sample() {
    let game = get_new_game();
    let num_moves = 0;
    while (!is_over(game)) {
        num_moves++;
        game = ai_make_move(game, get_random_piece_set()).new_game_states[2];
    }
    return {
        score: game.score,
        num_moves: num_moves,
    };
}

function sfc32(a, b, c, d) {
    return function () {
        a |= 0; b |= 0; c |= 0; d |= 0;
        var t = (a + b | 0) + d | 0;
        d = d + 1 | 0;
        a = b ^ b >>> 9;
        b = c + (c << 3) | 0;
        c = (c << 21 | c >>> 11);
        c = c + t | 0;
        return (t >>> 0) / 4294967296;
    }
}

function get_performance_sample(n) {
    const random = sfc32(1, 2, 3, 4);
    let game = blokie.getNewGame();
    for (let i = 0; i < n; ++i) {
        const piece_set = [];
        for (let j = 0; j < 3; ++j) {
            piece_set.push(PIECES[Math.floor(random() * PIECES.length)]);
        }
        game = blokie.getAIMove(game, piece_set).new_game_states[2/*last state*/];
        if (blokie.isOver(game)) {
            game = blokie.getNewGame();
        }
    }
}

var blokie = {
    getNewGame: get_new_game,
    getRandomPieceSet: () => get_random_piece_set().map(p => center_piece(p)),
    getEmptyPiece: getEmpty,
    getAIMove: ai_make_move,
    getAIPlan: get_ai_plan,
    evaluate: evaluate,
    at: at,
    isOver: is_over,
    canPlacePiece: can_place_piece,
    hasValidMove: has_valid_move,
    toggleSquare: (board, r, c) => xor(board, bit(r, c)),
    isEmpty: is_empty,
    or: or,
    getFitnessSample: get_fitness_sample,
    getPerformanceSample: get_performance_sample,
    leftTopJustify: left_top_justify_piece,
    getPieceBounds: get_piece_bounds,
    getPlacementComboMagnitude: get_placement_combo_magnitude,
    placePiece: place_piece,
    tryPlacePiece: try_place_piece,
    nearestValidPlacement: nearest_valid_placement,
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
    PIECES, get_random_piece, perform_clears, can_place_piece, has_valid_move,
    get_combo_magnitude, center_piece, left_top_justify_piece, get_piece_bounds,
    nearest_valid_placement, get_new_game,
};

export { blokie, init, _internals };
