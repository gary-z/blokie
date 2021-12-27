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
function or(a, b) {
    return [a[0] | b[0], a[1] | b[1], a[2] | b[2]];
}
function diff(a, b) {
    return [a[0] & ~b[0], a[1] & ~b[1], a[2] & ~b[2]];
}
function at(bb, r, c) {
    return !is_empty(and(and(row(r), column(c)), bb));
}

function row(r) {
    const result = [0, 0, 0];
    const m = r % 3;
    result[(r - m) / 3] = ROW_0 << (m * 9);
    return result;
}
for (let r = 0; r < 9; ++r) {
    console.assert(!is_empty(row(r)));
    console.assert(count(row(r)) === 9);
}

function column(c) {
    return [LEFT_BITS << c, LEFT_BITS << c, LEFT_BITS << c];
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

function cube(r, c) {
    const result = [0, 0, 0];
    result[r] = TOP_LEFT_CUBE << (c * 3);
    return result;
}
for (let r = 0; r < 3; ++r) {
    for (let c = 0; c < 3; ++c) {
        console.assert(count(cube(r, c)) === 9);
        let num_cols_spanned = 0;
        let num_rows_spanned = 0;
        for (let i = 0; i < 9; ++i) {
            if (any(and(cube(r, c), row(i)))) {
                num_rows_spanned++;
            }
            if (any(and(cube(r, c), column(i)))) {
                num_cols_spanned++;
            }
        }
        console.assert(num_cols_spanned === 3);
        console.assert(num_rows_spanned === 3);
    }
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

for (let p of PIECES) {
    console.assert(count(p) >= 1);
    console.assert(count(p) <= 5);
    for (let i = 5; i < 9; ++i) {
        console.assert(is_empty(and(row(i), p)));
        console.assert(is_empty(and(column(i), p)));
    }
}

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
        if (equal(and(c, board), c)) {
            to_remove = or(to_remove, c);
        }
        const r = row(i);
        if (equal(and(r, board), r)) {
            to_remove = or(to_remove, r);
        }
    }

    for (let r = 0; r < 3; ++r) {
        for (let c = 0; c < 3; ++c) {
            const cb = cube(r, c);
            if (equal(and(cb, board), cb)) {
                to_remove = or(to_remove, cb);
            }
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
}

function* get_next_boards(board, piece) {
    for (const placement of get_piece_placements(piece)) {
        if (is_empty(and(board, placement))) {
            yield [placement, perform_clears(or(board, placement))];
        }
    }
}

function get_eval(bb) {
    const OCCUPIED_SQUARE = 10;
    const CUBE = 30;
    const SQUASHED_EMPTY = 10;
    const CORNERED_EMPTY = 10;
    const ALTERNATING = 15;
    const DEADLY_PIECE = 40;

    let result = 0;

    // Occupied squares.
    result += count(bb) * OCCUPIED_SQUARE;

    // Occupied cube.
    for (let r = 0; r < 3; ++r) {
        for (let c = 0; c < 3; ++c) {
            const cb = cube(r, c);
            if (any(and(cube, bb))) {
                result += CUBE;
            }
        }
    }

    const open = not(bb);
    const blocked_up = diff(open, shift_down(open));
    const blocked_left = diff(open, shift_right(open));
    const blocked_right = diff(open, shift_left(open));
    const blocked_down = diff(open, shift_up(open));

    // Perimeter
    result += (count(blocked_up) - 9) * ALTERNATING;
    result += (count(blocked_left) - 9) * ALTERNATING;

    // Empty square between 2 blocked squares.
    result += count(and(blocked_down, blocked_up)) * SQUASHED_EMPTY;
    result += count(and(blocked_left, blocked_right)) * SQUASHED_EMPTY;

    // Empty square cornered between 2 blocked squares.
    result += count(diff(and(blocked_up, blocked_left), and(row(0), column(0)))) * CORNERED_EMPTY;
    result += count(diff(and(blocked_up, blocked_right), and(row(0), column(8)))) * CORNERED_EMPTY;
    result += count(diff(and(blocked_down, blocked_left), and(row(8), column(0)))) * CORNERED_EMPTY;
    result += count(diff(and(blocked_down, blocked_right), and(row(8), column(8)))) * CORNERED_EMPTY;

    return result;
}
console.assert(get_eval(EMPTY) === 0);

function* get_piece_set_permutations(board, piece_set) {
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

function can_clear_with_2_pieces(board, piece_set) {
    for (let i = 0; i < 3; ++i) {
        const p0 = piece_set[0];
        for (let [unused, after_p0] of get_next_boards(board, p0)) {
            for (let j = 0; j < 3; ++j) {
                if (i === j) {
                    continue;
                }
                const p1 = piece_set[1];
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

function ai_make_move(board, piece_set) {
    const result = {
        board: FULL,
        board_score: 999999,
        prev_boards: [FULL, FULL, FULL],
        prev_piece_placements: [EMPTY, EMPTY, EMPTY],
        pieces: [EMPTY, EMPTY, EMPTY],
    };
    for (const [p0, p1, p2] of get_piece_set_permutations(board, piece_set)) {
        for (const [placement_0, after_p0] of get_next_boards(board, p0)) {
            for (const [placement_1, after_p1] of get_next_boards(after_p0, p1)) {
                for (const [placement_2, after_p2] of get_next_boards(after_p1, p2)) {
                    const score = get_eval(after_p2);
                    if (score < result.board_score) {
                        result.board = after_p2;
                        result.board_score = score;
                        result.prev_boards = [board, after_p0, after_p1, after_p2];
                        result.prev_piece_placements = [placement_0, placement_1, placement_2];
                        result.pieces = [p0, p1, p2];
                    }
                }
            }
        }
    }
    return result;
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

var blokie = {
    getNewGame: () => [...EMPTY],
    getRandomPieceSet: get_random_piece_set,
    getAIMove: ai_make_move,
    at: at,
    isOver: (game) => {
        return equal(game, FULL);
    },
    count: count,
    centerPiece: center_piece,
};

export { blokie };
