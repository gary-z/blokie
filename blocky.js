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


for (let c = 0; c < 8; ++c) {
    console.assert(equal(shift_down(row(c)), row(c + 1)));
    console.assert(equal(shift_up(shift_down(row(c))), row(c)));
    console.assert(equal(shift_right(column(c)), column(c + 1)));
    console.assert(equal(shift_left(shift_right(column(c))), column(c)));
}

var blocky = {
    foo: () => {
        console.log('foo');
    }
};


export { blocky };
