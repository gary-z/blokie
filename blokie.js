"use strict";

// === BITBOARD FUNCTIONS
const USED_BITS = 0x7FFFFFF;
const ROW_0 = 0x1FF;
const ROW_2 = ROW_0 << 18;
const LEFT_BITS = 1 | (1 << 9) | (1 << 18);
const RIGHT_BITS = LEFT_BITS << 8;
const TOP_LEFT_CUBE = 0x7 | (0x7 << 9) | (0x7 << 18);

const EMPTY = [0, 0, 0];
const FULL = [USED_BITS, USED_BITS, USED_BITS];

// Used when returning values so clients can't change out consts.
function getEmpty() {
    return [...EMPTY];
}
function getFull() {
    return [...FULL];
}

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
    return _popcount(bb[0]) + _popcount(bb[1]) + _popcount(bb[2]);
}
console.assert(count([1, 3, 7]) === 6);
console.assert(count(FULL) === 81);
console.assert(count(EMPTY) === 0);

function compare(a, b) {
    return a[0] - b[0] || a[1] - b[1] || a[2] - b[2];
}

function equal(a, b) {
    return a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
}
console.assert(equal(EMPTY, EMPTY));
console.assert(equal(FULL, FULL));
console.assert(!equal(EMPTY, FULL));
console.assert(!equal(EMPTY, [1, 0, 0]));
console.assert(!equal(EMPTY, [0, 1, 0]));
console.assert(!equal(EMPTY, [0, 0, 1]));
function any(bb) {
    return bb[0] + bb[1] + bb[2] !== 0;
}
function is_empty(bb) {
    return !any(bb);
}
console.assert(is_empty(EMPTY));
console.assert(!is_empty(FULL));
console.assert(!is_empty([1, 0, 0]));
console.assert(!is_empty([0, 1, 0]));
console.assert(!is_empty([0, 0, 1]));

function not(bb) {
    return [~bb[0] & USED_BITS, ~bb[1] & USED_BITS, ~bb[2] & USED_BITS];
}
console.assert(equal(not(FULL), EMPTY));
console.assert(equal(not(EMPTY), FULL));
console.assert(count(not([1, 1, 1])) === 78);

function and(a, b) {
    return [a[0] & b[0], a[1] & b[1], a[2] & b[2]];
}
function is_disjoint(a, b) {
    return (a[0] & b[0]) === 0 && (a[1] & b[1]) === 0 && (a[2] & b[2]) === 0;
}
function count_intersection(a, b) {
    return _popcount(a[0] & b[0]) + _popcount(a[1] & b[1]) + _popcount(a[2] & b[2]);
}
function count_diff(a, b) {
    return _popcount(a[0] &~ b[0]) + _popcount(a[1] &~ b[1]) + _popcount(a[2] &~ b[2]);
}
function or(a, b) {
    return [a[0] | b[0], a[1] | b[1], a[2] | b[2]];
}
function xor(a, b) {
    return and(or(a, b), not(and(a, b)));
}
function diff(a, b) {
    return [a[0] & ~b[0], a[1] & ~b[1], a[2] & ~b[2]];
}
function is_subset(a/*superset*/, b) {
    return (b[0] & ~a[0]) === 0 && (b[1] & ~a[1]) === 0 && (b[2] & ~a[2]) === 0;
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
    return result;
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
    return [LEFT_BITS << c, LEFT_BITS << c, LEFT_BITS << c];
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
    return result;
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
    return [(bb[0] & ~RIGHT_BITS) << 1, (bb[1] & ~RIGHT_BITS) << 1, (bb[2] & ~RIGHT_BITS) << 1];
}
console.assert(count(shift_right(FULL)) === 72);

function shift_left(bb) {
    return [(bb[0] & ~LEFT_BITS) >> 1, (bb[1] & ~LEFT_BITS) >> 1, (bb[2] & ~LEFT_BITS) >> 1];
}
console.assert(count(shift_left(FULL)) === 72);

function shift_down(bb) {
    return [
        (bb[0] << 9) & USED_BITS,
        ((bb[1] << 9) | ((bb[0] & ROW_2) >> 18)) & USED_BITS,
        ((bb[2] << 9) | ((bb[1] & ROW_2) >> 18)) & USED_BITS,
    ];
}
function shift_up(bb) {
    return [
        (bb[0] >> 9) | ((bb[1] & ROW_0) << 18),
        (bb[1] >> 9) | ((bb[2] & ROW_0) << 18),
        bb[2] >> 9,
    ];
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
    [1, 0, 0],
    [3, 0, 0],
    [513, 0, 0],
    [1025, 0, 0],
    [514, 0, 0],
    [7, 0, 0],
    [262657, 0, 0],
    [1049601, 0, 0],
    [263172, 0, 0],
    [515, 0, 0],
    [1537, 0, 0],
    [1538, 0, 0],
    [1027, 0, 0],
    [15, 0, 0],
    [262657, 1, 0],
    [1539, 0, 0],
    [786945, 0, 0],
    [3588, 0, 0],
    [525315, 0, 0],
    [519, 0, 0],
    [262659, 0, 0],
    [2055, 0, 0],
    [787458, 0, 0],
    [3585, 0, 0],
    [3586, 0, 0],
    [263681, 0, 0],
    [525826, 0, 0],
    [1031, 0, 0],
    [3075, 0, 0],
    [263682, 0, 0],
    [525825, 0, 0],
    [1542, 0, 0],
    [31, 0, 0],
    [262657, 513, 0],
    [265729, 0, 0],
    [525319, 0, 0],
    [1836034, 0, 0],
    [1052164, 0, 0],
    [2567, 0, 0],
    [787459, 0, 0],
    [786947, 0, 0],
    [3589, 0, 0],
    [262663, 0, 0],
    [1050631, 0, 0],
    [1837060, 0, 0],
    [1835521, 0, 0],
    [527874, 0, 0],
];
function get_random_piece() {
    return PIECES[Math.floor(Math.random() * PIECES.length)];
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
console.assert(equal(rotate(PIECES[0]), bit(0, 8)));
console.assert(equal(rotate(rotate(PIECES[0])), bit(8, 8)));
console.assert(equal(rotate(rotate(rotate(PIECES[0]))), bit(8, 0)));

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
console.assert(!equal(mirror(PIECES[0]), PIECES[0]));

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

const test_piece = PIECES[0];
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


function* get_piece_placements(p) {
    let left = p;
    const col8 = column(8);
    const row8 = row(8);
    while (true) {
        yield p;
        if (any(and(p, col8))) {
            if (any(and(left, row8))) {
                return;
            }
            left = shift_down(left);
            p = left;
        } else {
            p = shift_right(p);
        }
    }
}

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
    let num_placements = 0;
    for (let placement of get_piece_placements(p)) {
        num_placements++;
    }
    console.assert(num_placements === (9 - height + 1) * (9 - width + 1));

    let num_next_boards = 0;
    for (let next_board of get_next_boards(EMPTY, p)) {
        num_next_boards++;
    }
    console.assert(num_next_boards === num_placements);

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

    return diff(board, to_remove);
}
console.assert(is_empty(perform_clears(FULL)));
console.assert(is_empty(perform_clears(EMPTY)));
for (let p of PIECES) {
    for (let placement of get_piece_placements(p)) {
        console.assert(equal(placement, perform_clears(placement)));
    }
}
for (let i = 0; i < 9; ++i) {
    console.assert(is_empty(perform_clears(row(i))));
    console.assert(is_empty(perform_clears(column(i))));
    console.assert(is_empty(perform_clears(cube(i))));
}

function* get_next_boards(board, piece) {
    if (is_empty(piece)) {
        yield [piece, board];
        return;
    }
    for (const placement of get_piece_placements(piece)) {
        if (is_empty(and(board, placement))) {
            yield [placement, perform_clears(or(board, placement))];
        }
    }
}

const EDGES = or(or(column(0),column(8)), or(row(0),row(8)));
const ALIGNED_BLOCKED_UP = or(row(3), row(6));
const ALIGNED_BLOCKED_DOWN = or(row(2), row(5));
const ALIGNED_BLOCKED_LEFT = or(column(3), column(6));
const ALIGNED_BLOCKED_RIGHT = or(column(2), column(5));

function get_eval(bb) {
    const OCCUPIED_SIDE_SQUARE = 2000;
    const OCCUPIED_CENTER_SQUARE = 1607;
    const OCCUPIED_CORNER_SQUARE = 3067;

    const SIDE_CUBE = 1358;
    const CORNER_CUBE = 908;
    const CENTER_CUBE = 204;

    const SQUASHED_EMPTY_MIDDLE = 524;
    const SQUASHED_EMPTY_EDGE = 3386;

    const ALTERNATING_UNALIGNED = 4450;
    const ALTERNATING_ALIGNED = 1776;

    const CORNERED_EMPTY = 6540;
    const DEADLY_PIECE = 18185;
    const THREE_BAR = 2665;

    let result = 0;

    // Occupied cube.
    for (let i = 0; i < 9; ++i) {
        const cb = and(cube(i), bb);
        if (any(cb)) {
            const cnt = count(cb);
            if (i == 4) {
                result += CENTER_CUBE;
                result += OCCUPIED_CENTER_SQUARE * cnt;
            } else if (i === 1 || i === 3 || i === 5 || i === 7) {
                result += SIDE_CUBE;
                result += OCCUPIED_SIDE_SQUARE * cnt;
            } else {
                result += CORNER_CUBE;
                result += OCCUPIED_CORNER_SQUARE * cnt;
            }
        }
    }

    const open = not(bb);
    const blocked_up = diff(open, shift_down(open));
    const blocked_left = diff(open, shift_right(open));
    const blocked_right = diff(open, shift_left(open));
    const blocked_down = diff(open, shift_up(open));

    // Alternating
    result += (count_diff(blocked_up, ALIGNED_BLOCKED_UP) - 9) * ALTERNATING_UNALIGNED;
    result += count_intersection(blocked_up, ALIGNED_BLOCKED_UP) * ALTERNATING_ALIGNED;

    result += (count_diff(blocked_left, ALIGNED_BLOCKED_LEFT) - 9) * ALTERNATING_UNALIGNED;
    result += count_intersection(blocked_left, ALIGNED_BLOCKED_LEFT) * ALTERNATING_ALIGNED;

    result += (count_diff(blocked_right, ALIGNED_BLOCKED_RIGHT) - 9) * ALTERNATING_UNALIGNED;
    result += count_intersection(blocked_right, ALIGNED_BLOCKED_RIGHT) * ALTERNATING_ALIGNED;

    result += (count_diff(blocked_down, ALIGNED_BLOCKED_DOWN) - 9) * ALTERNATING_UNALIGNED;
    result += count_intersection(blocked_down, ALIGNED_BLOCKED_DOWN) * ALTERNATING_ALIGNED;

    // Empty square between 2 blocked squares.
    const squashed_verticle = and(blocked_down, blocked_up);
    result += count_diff(squashed_verticle, EDGES) * SQUASHED_EMPTY_MIDDLE;
    result += count_intersection(squashed_verticle, EDGES) * SQUASHED_EMPTY_EDGE;

    const squashed_horizontal = and(blocked_left, blocked_right);
    result += count_diff(squashed_horizontal, EDGES) * SQUASHED_EMPTY_MIDDLE;
    result += count_intersection(squashed_horizontal, EDGES) * SQUASHED_EMPTY_EDGE;

    // Empty square cornered between 2 blocked squares.
    result += count_diff(and(blocked_up, blocked_left), or(row(0), column(0))) * CORNERED_EMPTY;
    result += count_diff(and(blocked_up, blocked_right), or(row(0), column(8))) * CORNERED_EMPTY;
    result += count_diff(and(blocked_down, blocked_left), or(row(8), column(0))) * CORNERED_EMPTY;
    result += count_diff(and(blocked_down, blocked_right), or(row(8), column(8))) * CORNERED_EMPTY;

    // 3 BAR
    // Deadly pieces.
    const open_left = shift_right(open);
    const open_2_left = shift_right(open_left);
    const open_right = shift_left(open);
    const open_2_right = shift_left(open_right);
    const open_up = shift_down(open);
    const open_2_up = shift_down(open_up);
    const open_down = shift_up(open);
    const open_2_down = shift_up(open_down);

    const open_up_left = shift_right(open_up);
    const open_down_left = shift_right(open_down);
    const open_up_right = shift_left(open_up);
    const open_down_right = shift_left(open_down);

    const open_and_open_left = and(open, open_left);
    const open_and_open_right = and(open, open_right);
    const open_and_open_up = and(open, open_up);
    const open_and_open_down = and(open, open_down);

    let fillable_by_horizontal_3_bar =
        or(and(open_and_open_left, open_right), or(and(open_and_open_left, open_2_left), and(open_and_open_right, open_2_right)));
    result += count_intersection(open, not(fillable_by_horizontal_3_bar)) * THREE_BAR;

    let fillable_by_verticle_3_bar = or(and(open_and_open_up, open_down), or(and(open_and_open_up, open_2_up), and(open_and_open_down, open_2_down)));
    result += count_intersection(open, not(fillable_by_verticle_3_bar)) * THREE_BAR;

    const open_and_open_2_left = and(open, open_2_left);
    const open_and_open_2_right = and(open, open_2_right);
    const open_and_open_2_up = and(open, open_2_up);
    const open_and_open_2_down = and(open, open_2_down);

    // 5 Bar
    if (is_disjoint(and(open_and_open_left, open_and_open_2_left), and(open_and_open_right, open_and_open_2_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_up, open_and_open_2_up), and(open_and_open_down, open_and_open_2_down))) {
        result += DEADLY_PIECE;
    }

    // L
    if (is_disjoint(and(open_and_open_up, open_and_open_2_up), and(open_and_open_right, open_and_open_2_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_up, open_and_open_2_up), and(open_and_open_left, open_and_open_2_left))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_down, open_and_open_2_down), and(open_and_open_right, open_and_open_2_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_down, open_and_open_2_down), and(open_and_open_left, open_and_open_2_left))) {
        result += DEADLY_PIECE;
    }

    // T
    if (is_disjoint(and(open_and_open_left, open_and_open_right), and(open_and_open_down, open_and_open_2_down))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_left, open_and_open_right), and(open_and_open_up, open_and_open_2_up))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_up, open_and_open_down), and(open_and_open_left, open_and_open_2_left))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_up, open_and_open_down), and(open_and_open_right, open_and_open_2_right))) {
        result += DEADLY_PIECE;
    }

    // +
    if (is_disjoint(and(open_and_open_left, open_and_open_right), and(open_and_open_up, open_and_open_down))) {
        result += DEADLY_PIECE;
    }

    // 3 Stair
    if (is_disjoint(open, and(open_down_left, open_up_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(open, and(open_up_left, open_down_right))) {
        result += DEADLY_PIECE;
    }

    // C
    if (is_disjoint(and(open_and_open_up, open_and_open_down), and(open_up_right, open_down_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_up, open_and_open_down), and(open_up_left, open_down_left))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_left, open_and_open_right), and(open_up_left, open_up_right))) {
        result += DEADLY_PIECE;
    }
    if (is_disjoint(and(open_and_open_left, open_and_open_right), and(open_down_left, open_down_right))) {
        result += DEADLY_PIECE;
    }


    return result;
}
console.assert(get_eval(EMPTY) === 0);

for (let fullness = 0.0; fullness <= 1; fullness += 0.1) {
    for (let i = 0; i < 100; ++i) {
        const board = get_random_board(fullness);
        const score = get_eval(board);
        for (const transformed of get_all_transformations(board)) {
            console.assert(get_eval(transformed) === score);
        }
    }
}

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


function* get_piece_set_permutations_optimized(board, piece_set) {
    piece_set = [...piece_set];
    piece_set.sort(compare);
    yield piece_set;
    if (!can_clear_with_2_pieces(board, piece_set)) {
        return;
    }
    const [a, b, c] = piece_set;
    yield [a, c, b];
    yield [b, a, c];
    yield [b, c, a];
    yield [c, a, b];
    yield [c, b, a];
}

function* get_all_piece_set_permutations(piece_set) {
    const [a, b, c] = piece_set;
    yield [a, b, c];
    yield [a, c, b];
    yield [b, a, c];
    yield [b, c, a];
    yield [c, a, b];
    yield [c, b, a];
}

function can_clear_with_2_pieces(board, piece_set) {
    for (let i = 0; i < 3; ++i) {
        const p0 = piece_set[i];
        for (let [unused, after_p0] of get_next_boards(board, p0)) {
            for (let j = 0; j < 3; ++j) {
                if (i === j) {
                    continue;
                }
                const p1 = piece_set[j];
                for (let [unused, after_p1] of get_next_boards(after_p0, p1)) {
                    if (count(after_p1) < count(board) + count(p0) + count(p1)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

function ai_make_move(game, original_piece_set) {
    const piece_set = original_piece_set.map(p => left_top_justify_piece(p));
    const board = game.board;

    // This call searches for the best possible end state.
    const ai_move_base = ai_make_move_impl(game, piece_set);

    let result = null;
    let best_score = -1;

    // Here we can optimize the score.
    for (const [p0, p1, p2] of get_all_piece_set_permutations(piece_set)) {
        for (const [placement_0, after_p0] of get_next_boards(board, p0)) {
            if (!ai_move_base.new_game_states.some(state => equal(state.previous_piece_placement, placement_0))) {
                continue;
            }
            for (const [placement_1, after_p1] of get_next_boards(after_p0, p1)) {
                if (!ai_move_base.new_game_states.some(state => equal(state.previous_piece_placement, placement_1))) {
                    continue;
                }

                for (const [placement_2, after_p2] of get_next_boards(after_p1, p2)) {
                    if (!ai_move_base.new_game_states.some(state => equal(state.previous_piece_placement, placement_2))) {
                        continue;
                    }

                    const p0_move_was_clear = count(after_p0) < count(board) + count(p0);
                    const p1_move_was_clear = count(after_p1) < count(after_p0) + count(p1);
                    const p2_move_was_clear = count(after_p2) < count(after_p1) + count(p1);
                    const score_after_p0 = game.score + get_move_score(game.previous_move_was_clear, board, placement_0, after_p0);
                    const score_after_p1 = score_after_p0 + get_move_score(p0_move_was_clear, after_p0, placement_1, after_p1);
                    const score_after_p2 = score_after_p1 + get_move_score(p1_move_was_clear, after_p1, placement_2, after_p2);


                    // Maximize score.
                    if (score_after_p2 < best_score) {
                        continue;
                    }

                    // Try to end with a clear to set up another streak next piece set.
                    if (score_after_p2 === best_score && !p2_move_was_clear) {
                        continue;
                    }

                    best_score = score_after_p2;
                    result = {
                        evaluation: ai_move_base.evaluation,
                        new_game_states: [
                            {
                                board: after_p0,
                                previous_piece_placement: placement_0,
                                previous_piece: original_piece_set[piece_set.indexOf(p0)],
                                previous_move_was_clear: p0_move_was_clear,
                                score: score_after_p0,
                            },
                            {
                                board: after_p1,
                                previous_piece_placement: placement_1,
                                previous_piece: original_piece_set[piece_set.indexOf(p1)],
                                previous_move_was_clear: p1_move_was_clear,
                                score: score_after_p1,
                            },
                            {
                                board: after_p2,
                                previous_piece_placement: placement_2,
                                previous_piece: original_piece_set[piece_set.indexOf(p2)],
                                previous_move_was_clear: p2_move_was_clear,
                                score: score_after_p2,
                            },
                        ]
                    };
                }
            }
        }
    }
    if (result === null) {
        console.log('not found?!');
    }
    return result;
}


function ai_make_move_impl(game, piece_set) {
    const board = game.board;
    const result = {
        evaluation: 999999,
        new_game_states: Array(3).fill(
            {
                board: getFull(),
                previous_piece_placement: getEmpty(),
            }
        ),
    };
    const board_count = count(board);
    let is_first_perm = true;
    for (const [p0, p1, p2] of get_piece_set_permutations_optimized(board, piece_set)) {
        const p0_count = count(p0);
        const p1_count = count(p1);
        const p2_count = count(p2);

        for (const [placement_0, after_p0] of get_next_boards(board, p0)) {
            for (const [placement_1, after_p1] of get_next_boards(after_p0, p1)) {
                if (compare(p0, p1) > 0 && count(after_p1) == board_count + p0_count + p1_count) {
                    // We tried this state before.
                    continue;
                }
                for (const [placement_2, after_p2] of get_next_boards(after_p1, p2)) {
                    if (!is_first_perm &&
                        count(after_p2) === board_count + p0_count + p1_count + p2_count) {
                        continue;
                    }
                    const score = get_eval(after_p2);
                    if (score < result.evaluation) {
                        result.evaluation = score;
                        result.new_game_states = [
                            {
                                board: after_p0,
                                previous_piece_placement: placement_0,
                            },
                            {
                                board: after_p1,
                                previous_piece_placement: placement_1,
                            },
                            {
                                board: after_p2,
                                previous_piece_placement: placement_2,
                            },
                        ];
                    }
                }
            }
        }

        is_first_perm = false;
    }
    return result;
}

function get_new_game() {
    return {
        board: getEmpty(),
        previous_piece_placement: getEmpty(),
        previous_piece: getEmpty(),
        previous_move_was_clear: false,
        score: 0,

    };
}

console.assert(ai_make_move(get_new_game(), [EMPTY, EMPTY, EMPTY]).evaluation === 0);

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
        num_moves ++;
        game = ai_make_move(game, get_random_piece_set()).new_game_states[2];
    }
    return {
        score: game.score,
        num_moves: num_moves,
    };
}

var blokie = {
    getNewGame: get_new_game,
    getRandomPieceSet: () => get_random_piece_set().map(p => center_piece([...p])),
    getEmptyPiece: getEmpty,
    getAIMove: ai_make_move,
    at: at,
    isOver: is_over,
    toggleSquare: (board, r, c) => xor(board, bit(r, c)),
    isEmpty: is_empty,
    getFitnessSample: get_fitness_sample,
};

export { blokie };
