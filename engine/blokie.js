"use strict";

import createBlokieSolver from './wasm/blokie-solver.js';

// WASM module - initialized asynchronously.
let solver = null;
const initPromise = createBlokieSolver().then(module => {
    solver = module;
});

// === BITBOARD FUNCTIONS
const USED_BITS = 0x7FFFFFF;
const ROW_0 = 0x1FF;
const ROW_2 = ROW_0 << 18;
const LEFT_BITS = 1 | (1 << 9) | (1 << 18);
const RIGHT_BITS = LEFT_BITS << 8;
const TOP_LEFT_CUBE = 0x7 | (0x7 << 9) | (0x7 << 18);

const INF_SCORE = 9999999;
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
console.assert(_popcount(USED_BITS) === 27);
console.assert(_popcount(ROW_0) === 9);
console.assert(_popcount(TOP_LEFT_CUBE) === 9);

function count(bb) {
    return _popcount(bb.a) + _popcount(bb.b) + _popcount(bb.c);
}
console.assert(count(bitboard(1, 3, 7)) === 6);
console.assert(count(FULL) === 81);
console.assert(count(EMPTY) === 0);

function compare(a, b) {
    return a.a - b.a || a.b - b.b || a.c - b.c;
}

function equal(a, b) {
    return a.a === b.a && a.b === b.b && a.c === b.c;
}
console.assert(equal(EMPTY, EMPTY));
console.assert(equal(FULL, FULL));
console.assert(!equal(EMPTY, FULL));
console.assert(!equal(EMPTY, bitboard(1, 0, 0)));
console.assert(!equal(EMPTY, bitboard(0, 1, 0)));
console.assert(!equal(EMPTY, bitboard(0, 0, 1)));
function any(bb) {
    return bb.a + bb.b + bb.c !== 0;
}
function is_empty(bb) {
    return !any(bb);
}
console.assert(is_empty(EMPTY));
console.assert(!is_empty(FULL));
console.assert(!is_empty(bitboard(1, 0, 0)));
console.assert(!is_empty(bitboard(0, 1, 0)));
console.assert(!is_empty(bitboard(0, 0, 1)));

function not(bb) {
    return bitboard(~bb.a & USED_BITS, ~bb.b & USED_BITS, ~bb.c & USED_BITS);
}
console.assert(equal(not(FULL), EMPTY));
console.assert(equal(not(EMPTY), FULL));
console.assert(count(not(bitboard(1, 1, 1))) === 78);

function and(a, b) {
    return bitboard(a.a & b.a, a.b & b.b, a.c & b.c);
}
function is_disjoint(a, b) {
    return (a.a & b.a) === 0 && (a.b & b.b) === 0 && (a.c & b.c) === 0;
}
function count_intersection(a, b) {
    return _popcount(a.a & b.a) + _popcount(a.b & b.b) + _popcount(a.c & b.c);
}
function count_diff(a, b) {
    return _popcount(a.a & ~b.a) + _popcount(a.b & ~b.b) + _popcount(a.c & ~b.c);
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

for (let r = 0; r < 9; ++r) {
    console.assert(!is_empty(row(r)));
    console.assert(count(row(r)) === 9);
}

function _column(c) {
    return bitboard(LEFT_BITS << c, LEFT_BITS << c, LEFT_BITS << c);
}
const COLS = Array.from({ length: 10 }, (_, i) => _column(i));
function column(c) {
    return COLS[c];
}

for (let c = 0; c < 9; ++c) {
    console.assert(!is_empty(row(c)));
    console.assert(count(row(c)) === 9);
    for (let r = 0; r < 9; ++r) {
        console.assert(count(and(column(c), row(r))) === 1);
        console.assert(count(or(column(c), row(r))) === 17);
        console.assert(count(diff(column(c), row(r))) === 8);
        console.assert(at(FULL, r, c));
        console.assert(!at(EMPTY, r, c));
    }
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

for (let i = 0; i < 9; ++i) {
    console.assert(count(cube(i)) === 9);
    let num_cols_spanned = 0;
    let num_rows_spanned = 0;
    for (let j = 0; j < 9; ++j) {
        if (any(and(cube(i), row(j)))) {
            num_rows_spanned++;
        }
        if (any(and(cube(i), column(j)))) {
            num_cols_spanned++;
        }
    }
    console.assert(num_cols_spanned === 3);
    console.assert(num_rows_spanned === 3);
}


function shift_right(bb) {
    return bitboard((bb.a & ~RIGHT_BITS) << 1, (bb.b & ~RIGHT_BITS) << 1, (bb.c & ~RIGHT_BITS) << 1);
}
console.assert(count(shift_right(FULL)) === 72);

function shift_left(bb) {
    return bitboard((bb.a & ~LEFT_BITS) >> 1, (bb.b & ~LEFT_BITS) >> 1, (bb.c & ~LEFT_BITS) >> 1);
}
console.assert(count(shift_left(FULL)) === 72);

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

function str(bb) {
    let result = "";
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            result += at(bb, r, c) ? '#' : '.';
        }
        result += "\n";
    }
    return result;
}


for (let c = 0; c < 8; ++c) {
    console.assert(equal(shift_down(row(c)), row(c + 1)));
    console.assert(equal(shift_up(shift_down(row(c))), row(c)));
    console.assert(equal(shift_right(column(c)), column(c + 1)));
    console.assert(equal(shift_left(shift_right(column(c))), column(c)));
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
for (let i = 0; i < 100; ++i) {
    console.assert(any(get_random_piece()));
}

for (const p of PIECES) {
    console.assert(count(p) >= 1);
    console.assert(count(p) <= 5);
    for (let i = 5; i < 9; ++i) {
        console.assert(is_empty(and(row(i), p)));
        console.assert(is_empty(and(column(i), p)));
    }

    // Pieces are left-top justified;
    console.assert(count(shift_left(p)) !== count(p));
    console.assert(count(shift_up(p)) !== count(p));

    console.assert(!equal(p, shift_right(p)));
    console.assert(!equal(p, shift_down(p)));

    console.assert(equal(p, shift_left(shift_right(p))));
    console.assert(equal(p, shift_up(shift_down(p))));

}

function rotate(bb) {
    let result = EMPTY;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (at(bb, r, c)) {
                const rotated_r = c;
                const rotated_c = 8 - r;
                result = or(result, bit(rotated_r, rotated_c));
            }
        }
    }
    return result;
}

console.assert(equal(rotate(EMPTY), EMPTY));
console.assert(equal(rotate(FULL), FULL));
console.assert(equal(rotate(bit(0, 0)), bit(0, 8)));
console.assert(equal(rotate(rotate(bit(0, 0))), bit(8, 8)));
console.assert(equal(rotate(rotate(rotate(bit(0, 0)))), bit(8, 0)));

for (const test_piece of PIECES) {
    console.assert(count(rotate(test_piece)) === count(test_piece));
    console.assert(equal(rotate(rotate(rotate(rotate(test_piece)))), test_piece));

    const top_right = rotate(test_piece);
    console.assert(count(top_right) !== count(shift_right(top_right)));
    console.assert(count(top_right) !== count(shift_up(top_right)));
    console.assert(equal(top_right, shift_right(shift_left(top_right))));
    console.assert(equal(top_right, shift_up(shift_down(top_right))));

    const bottom_right = rotate(top_right);
    console.assert(count(bottom_right) !== count(shift_right(bottom_right)));
    console.assert(count(bottom_right) !== count(shift_down(bottom_right)));
    console.assert(equal(bottom_right, shift_right(shift_left(bottom_right))));
    console.assert(equal(bottom_right, shift_down(shift_up(bottom_right))));

    const bottom_left = rotate(bottom_right);
    console.assert(count(bottom_left) !== count(shift_left(bottom_left)));
    console.assert(count(bottom_left) !== count(shift_down(bottom_left)));
    console.assert(equal(bottom_left, shift_left(shift_right(bottom_left))));
    console.assert(equal(bottom_left, shift_down(shift_up(bottom_left))));
}

function mirror(bb) {
    let result = EMPTY;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (at(bb, r, c)) {
                result = or(result, bit(r, 8 - c));
            }
        }
    }
    return result;
}
console.assert(equal(mirror(EMPTY), EMPTY));
console.assert(equal(mirror(FULL), FULL));
console.assert(equal(mirror(bit(0, 0)), bit(0, 8)));

for (let test_piece of PIECES) {
    console.assert(count(mirror(test_piece)) === count(test_piece));
    console.assert(equal(rotate(rotate(rotate(rotate(test_piece)))), test_piece));
}

function get_all_transformations(bb) {
    const transformations = [];

    let rotated = bb;
    for (let i = 0; i < 4; ++i) {
        rotated = rotate(rotated);
        const mirrored = mirror(rotated);

        transformations.push(rotated);
        transformations.push(mirrored);
    }

    return transformations;
}
const empty_transformations = get_all_transformations(EMPTY);
console.assert(empty_transformations.length === 8);
console.assert(empty_transformations.every(t => equal(t, EMPTY)));

const full_transformations = get_all_transformations(FULL);
console.assert(full_transformations.length === 8);
console.assert(full_transformations.every(t => equal(t, FULL)));

const test_piece = bit(0, 0);
const piece_transformations = get_all_transformations(test_piece);
console.assert(piece_transformations.length === 8);

function get_random_board(fullness) {
    let result = EMPTY;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (Math.random() < fullness) {
                result = or(result, bit(r, c));
            }
        }
    }
    return result;
}

console.assert(equal(get_random_board(0), EMPTY));
console.assert(equal(get_random_board(1), FULL));


for (let p of PIECES) {
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
    let num_next_boards = 0;
    for (let next_board of get_next_boards(EMPTY, p)) {
        num_next_boards++;
    }

    console.assert(num_next_boards === (9 - height + 1) * (9 - width + 1));

    for (let next_board of get_next_boards(FULL, p)) {
        // Can't place the piece on a full board.
        console.assert(false);
    }
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
console.assert(is_empty(perform_clears(FULL)));
console.assert(is_empty(perform_clears(EMPTY)));
for (let p of PIECES) {
    for (let { placement } of get_next_boards(EMPTY, p)) {
        console.assert(equal(placement, perform_clears(placement)));
    }
}
for (let i = 0; i < 9; ++i) {
    console.assert(is_empty(perform_clears(row(i))));
    console.assert(is_empty(perform_clears(column(i))));
    console.assert(is_empty(perform_clears(cube(i))));
}

function get_next_boards(board, p, clears_first = false) {
    if (is_empty(p)) {
        return [{ placement: p, board: board }];
    }

    let result = [];

    let left = p;
    const col8 = column(8);
    const row8 = row(8);
    while (true) {
        if (is_disjoint(board, p)) {
            result.push({ placement: p, board: perform_clears(or(board, p)) });
        }
        if (!is_disjoint(p, col8)) {
            if (!is_disjoint(left, row8)) {
                break;
            }
            left = shift_down(left);
            p = left;
        } else {
            p = shift_right(p);
        }
    }
    function is_clear(placement_pair) {
        return is_subset(placement_pair.board, placement_pair.placement);
    }
    if (clears_first) {
        result.sort((a, b) => is_clear(b) - is_clear(a));
    }
    return result;
}

// === SCORING (kept for tryPlacePiece) ===

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
console.assert(get_combo_magnitude(EMPTY) === 0);
console.assert(get_combo_magnitude(FULL) === 9 * 3);
for (const piece of PIECES) {
    for (const transformed of get_all_transformations(piece)) {
        console.assert(get_combo_magnitude(transformed) === 0);
    }
}
for (let i = 0; i < 9; ++i) {
    console.assert(get_combo_magnitude(row(i)) === 1);
    console.assert(get_combo_magnitude(column(i)) === 1);
    for (let j = 0; j < 9; ++j) {
        console.assert(get_combo_magnitude(or(row(i), column(j))) === 2);
        if (i !== j) {
            console.assert(get_combo_magnitude(or(row(i), row(j))) === 2);
            console.assert(get_combo_magnitude(or(column(i), column(j))) === 2);
        }

        for (let k = 0; k < 9; ++k) {
            console.assert(get_combo_magnitude(or(cube(k), or(row(i), column(j)))) === 3);
        }
    }
    for (let k = 0; k < 9; ++k) {
        console.assert(get_combo_magnitude(or(row(i), cube(k))) === 2);
        console.assert(get_combo_magnitude(or(column(i), cube(k))) === 2);
    }
}
for (let k = 0; k < 9; ++k) {
    console.assert(get_combo_magnitude(cube(k)) === 1);
}


function get_move_score(previous_was_clear, prev, placement, after) {
    console.assert(is_empty(and(prev, placement)));
    // 1 point for each block placed that was not cleared.
    let result = count(diff(after, prev));
    const combo = get_combo_magnitude(or(prev, placement));
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


// === AI (powered by WASM) ===

function ai_make_move(game, original_piece_set) {
    const piece_set = original_piece_set.map(p => left_top_justify_piece(p));
    const board = game.board;

    const result = solver.aiMakeMove(
        board.a, board.b, board.c,
        game.score,
        game.previous_move_was_clear,
        piece_set[0].a, piece_set[0].b, piece_set[0].c,
        piece_set[1].a, piece_set[1].b, piece_set[1].c,
        piece_set[2].a, piece_set[2].b, piece_set[2].c,
    );

    return {
        evaluation: result.evaluation,
        new_game_states: [0, 1, 2].map(i => {
            const s = result.new_game_states[i];
            return {
                board: s.board,
                previous_piece_placement: s.previous_piece_placement,
                previous_piece: original_piece_set[s.piece_index],
                previous_move_was_clear: s.previous_move_was_clear,
                score: s.score,
            };
        }),
    };
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

for (const p of PIECES) {
    const centered = center_piece(p);
    console.assert(count(p) === count(centered));
    console.assert(equal(p, left_top_justify_piece(p)));
    console.assert(equal(p, left_top_justify_piece(center_piece(p))));
}

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
    at: at,
    isOver: is_over,
    toggleSquare: (board, r, c) => xor(board, bit(r, c)),
    isEmpty: is_empty,
    or: or,
    getFitnessSample: get_fitness_sample,
    getPerformanceSample: get_performance_sample,
    leftTopJustify: left_top_justify_piece,
    getPieceBounds: function(piece) {
        const p = left_top_justify_piece(piece);
        let maxR = 0, maxC = 0;
        for (let r = 0; r < 9; r++) {
            for (let c = 0; c < 9; c++) {
                if (at(p, r, c)) { maxR = Math.max(maxR, r); maxC = Math.max(maxC, c); }
            }
        }
        return { rows: maxR + 1, cols: maxC + 1 };
    },
    tryPlacePiece: function(game, piece, dr, dc) {
        if (dr < 0 || dc < 0) return null;
        let p = left_top_justify_piece(piece);
        const origCount = count(p);
        if (origCount === 0) return null;
        for (let i = 0; i < dc; i++) p = shift_right(p);
        for (let i = 0; i < dr; i++) p = shift_down(p);
        if (count(p) !== origCount) return null;
        if (!is_disjoint(game.board, p)) return null;
        const newBoard = perform_clears(or(game.board, p));
        const moveScore = get_move_score(game.previous_move_was_clear, game.board, p, newBoard);
        const wasClear = count(newBoard) < count(game.board) + origCount;
        return {
            placement: p,
            newGame: {
                board: newBoard,
                previous_piece_placement: p,
                previous_piece: piece,
                previous_move_was_clear: wasClear,
                score: game.score + moveScore,
            }
        };
    },
};

export { blokie, initPromise };
