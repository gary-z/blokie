"use strict";

// The bitboard layer the rest of the engine is built out of: three 27-bit
// words standing in for 81 squares, and the rows, columns, cubes, shifts and
// piece shapes read off them. None of it is interesting on its own, and all of
// it is wrong in ways that are hard to see from the board -- a shift that drops
// a square at a word boundary, a cube mask off by three -- so it is checked
// here a piece at a time.
//
// These checks used to run as console.assert calls at the top level of
// engine/js/blokie.js, which meant every page load and every worker start ran
// them before the board could be drawn. They are the same checks; they just
// run where tests run now.

import { blokie, _internals } from '../../engine/js/blokie.js';

const {
    bitboard, getEmpty, getFull, EMPTY, FULL, USED_BITS, ROW_0, TOP_LEFT_CUBE,
    _popcount, count, equal, any, is_empty, not, and, or, diff, is_disjoint,
    bit, at, row, column, cube,
    shift_left, shift_right, shift_up, shift_down,
    PIECES, perform_clears, can_place_piece, has_valid_move,
    get_combo_magnitude, center_piece, left_top_justify_piece, get_piece_bounds,
    place_nearest, new_game,
} = _internals;

let failures = 0;
/** @type {(condition: boolean, description: string) => void} */
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return;
    }
    console.log("ok - %s", description);
}

/**
 * One assertion inside a group: what has to hold, and what to call it if it
 * does not. Named because every `want` in this file is one of these, and
 * annotating checkAll is what gives all of them their types.
 * @typedef {(condition: boolean, detail: string) => void} Want
 */

// A whole group of assertions reported as one line, so a passing run stays
// readable while a failure still says which case broke.
/** @type {(description: string, body: (want: Want) => void) => void} */
function checkAll(description, body) {
    /** @type {string[]} */
    const broken = [];
    body((condition, detail) => {
        if (!condition) {
            broken.push(detail);
        }
    });
    if (broken.length === 0) {
        check(true, description);
        return;
    }
    check(false, `${description} (${broken.length} failed, first: ${broken[0]})`);
}

// === words and counting ===

checkAll("popcount counts the bits of a word", (want) => {
    want(_popcount(USED_BITS) === 27, "USED_BITS");
    want(_popcount(ROW_0) === 9, "ROW_0");
    want(_popcount(TOP_LEFT_CUBE) === 9, "TOP_LEFT_CUBE");
});

checkAll("count adds the three words up", (want) => {
    want(count(bitboard(1, 3, 7)) === 6, "1,3,7");
    want(count(FULL) === 81, "full");
    want(count(EMPTY) === 0, "empty");
});

checkAll("equal compares every word", (want) => {
    want(equal(EMPTY, EMPTY), "empty equals empty");
    want(equal(FULL, FULL), "full equals full");
    want(!equal(EMPTY, FULL), "empty differs from full");
    want(!equal(EMPTY, bitboard(1, 0, 0)), "first word");
    want(!equal(EMPTY, bitboard(0, 1, 0)), "second word");
    want(!equal(EMPTY, bitboard(0, 0, 1)), "third word");
});

checkAll("is_empty sees a bit in any word", (want) => {
    want(is_empty(EMPTY), "empty");
    want(!is_empty(FULL), "full");
    want(!is_empty(bitboard(1, 0, 0)), "first word");
    want(!is_empty(bitboard(0, 1, 0)), "second word");
    want(!is_empty(bitboard(0, 0, 1)), "third word");
});

checkAll("not stays inside the 27 used bits", (want) => {
    want(equal(not(FULL), EMPTY), "not full");
    want(equal(not(EMPTY), FULL), "not empty");
    want(count(not(bitboard(1, 1, 1))) === 78, "not three corners");
});

// A returned constant is a fresh object, so a caller that writes to one cannot
// reach the constant the next caller reads.
checkAll("the empty and full boards are handed out by value", (want) => {
    const mine = getEmpty();
    mine.a = 5;
    want(is_empty(getEmpty()), "empty is unchanged");
    const full = getFull();
    full.a = 0;
    want(count(getFull()) === 81, "full is unchanged");
});

// === rows, columns and cubes ===

checkAll("every row is nine squares", (want) => {
    for (let r = 0; r < 9; ++r) {
        want(!is_empty(row(r)), `row ${r} is not empty`);
        want(count(row(r)) === 9, `row ${r} has nine squares`);
    }
});

checkAll("rows and columns cross exactly once", (want) => {
    for (let c = 0; c < 9; ++c) {
        want(count(column(c)) === 9, `column ${c} has nine squares`);
        for (let r = 0; r < 9; ++r) {
            want(count(and(column(c), row(r))) === 1, `${r},${c} crossing`);
            want(count(or(column(c), row(r))) === 17, `${r},${c} union`);
            want(count(diff(column(c), row(r))) === 8, `${r},${c} difference`);
            want(at(FULL, r, c), `${r},${c} set on a full board`);
            want(!at(EMPTY, r, c), `${r},${c} clear on an empty board`);
        }
    }
});

checkAll("every cube is three rows by three columns", (want) => {
    for (let i = 0; i < 9; ++i) {
        want(count(cube(i)) === 9, `cube ${i} has nine squares`);
        let rows_spanned = 0;
        let cols_spanned = 0;
        for (let j = 0; j < 9; ++j) {
            if (any(and(cube(i), row(j)))) {
                rows_spanned++;
            }
            if (any(and(cube(i), column(j)))) {
                cols_spanned++;
            }
        }
        want(rows_spanned === 3, `cube ${i} spans three rows`);
        want(cols_spanned === 3, `cube ${i} spans three columns`);
    }
});

// === shifts ===

checkAll("a shift drops the column or row it pushes off the board", (want) => {
    want(count(shift_right(FULL)) === 72, "right off a full board");
    want(count(shift_left(FULL)) === 72, "left off a full board");
});

checkAll("shifts move a line to the next one and back", (want) => {
    for (let i = 0; i < 8; ++i) {
        want(equal(shift_down(row(i)), row(i + 1)), `row ${i} down`);
        want(equal(shift_up(shift_down(row(i))), row(i)), `row ${i} down and up`);
        want(equal(shift_right(column(i)), column(i + 1)), `column ${i} right`);
        want(equal(shift_left(shift_right(column(i))), column(i)),
            `column ${i} right and left`);
    }
});

// === pieces ===

checkAll("the deck always deals three real pieces", (want) => {
    for (let i = 0; i < 100; ++i) {
        const deck = blokie.deal();
        want(deck.length === 3, `draw ${i} has three slots`);
        for (const p of deck) {
            want(any(p), `draw ${i} dealt something`);
            // What the search is handed, and what two slots holding the same
            // shape are compared as, so a deal that skipped this would be a
            // deck the engine quietly re-justifies on every call.
            want(equal(p, left_top_justify_piece(p)), `draw ${i} is justified`);
        }
    }
});

// A harness needs a repeatable deal; the game itself never passes one.
checkAll("a supplied generator makes the deal repeatable", (want) => {
    /** @type {(values: number[]) => () => number} */
    const fixed = (values) => {
        let i = 0;
        return () => values[i++ % values.length];
    };
    const a = blokie.deal(fixed([0, 0.5, 0.99]));
    const b = blokie.deal(fixed([0, 0.5, 0.99]));
    for (let slot = 0; slot < 3; ++slot) {
        want(equal(a[slot], b[slot]), `slot ${slot} repeats`);
    }
    want(equal(a[0], PIECES[0]), 'the first draw is the first piece');
    want(equal(a[2], PIECES[PIECES.length - 1]), 'the last draw is the last piece');
});

checkAll("every piece is one to five squares in the top left", (want) => {
    for (const p of PIECES) {
        want(count(p) >= 1 && count(p) <= 5, `piece size ${count(p)}`);
        for (let i = 5; i < 9; ++i) {
            want(is_empty(and(row(i), p)), `piece clear of row ${i}`);
            want(is_empty(and(column(i), p)), `piece clear of column ${i}`);
        }

        // Pieces are left-top justified, so shifting either way off the corner
        // loses a square.
        want(count(shift_left(p)) !== count(p), "justified against column 0");
        want(count(shift_up(p)) !== count(p), "justified against row 0");
        want(!equal(p, shift_right(p)), "shifting right moves it");
        want(!equal(p, shift_down(p)), "shifting down moves it");
        want(equal(p, shift_left(shift_right(p))), "right and back");
        want(equal(p, shift_up(shift_down(p))), "down and back");
    }
});

checkAll("a piece keeps its shape and its box wherever it is drawn", (want) => {
    for (const p of PIECES) {
        const centered = center_piece(p);
        want(count(p) === count(centered), "centering keeps the squares");
        want(equal(p, left_top_justify_piece(p)), "the table is justified");
        want(equal(p, left_top_justify_piece(centered)), "centering undoes");

        // The box is the smallest one the piece fits in, whether the piece
        // comes in justified or centered on deck.
        const bounds = get_piece_bounds(p);
        want(bounds.rows === get_piece_bounds(centered).rows, "same height");
        want(bounds.cols === get_piece_bounds(centered).cols, "same width");
        want(any(and(p, row(bounds.rows - 1)))
            && is_empty(and(p, row(bounds.rows))), "height is tight");
        want(any(and(p, column(bounds.cols - 1)))
            && is_empty(and(p, column(bounds.cols))), "width is tight");
    }
});

// === clearing ===

checkAll("a completed line clears and nothing else does", (want) => {
    want(is_empty(perform_clears(FULL)), "a full board clears completely");
    want(is_empty(perform_clears(EMPTY)), "an empty board stays empty");
    for (let i = 0; i < 9; ++i) {
        want(is_empty(perform_clears(row(i))), `row ${i}`);
        want(is_empty(perform_clears(column(i))), `column ${i}`);
        want(is_empty(perform_clears(cube(i))), `cube ${i}`);
    }
});

// A piece is at most five squares, so no single placement of one can finish a
// line: whatever it lands on is still there afterwards.
checkAll("one piece on an empty board never clears", (want) => {
    const game = new_game();
    for (const p of PIECES) {
        const bounds = get_piece_bounds(p);
        for (let r = 0; r + bounds.rows <= 9; ++r) {
            for (let c = 0; c + bounds.cols <= 9; ++c) {
                let placement = p;
                for (let i = 0; i < c; ++i) placement = shift_right(placement);
                for (let i = 0; i < r; ++i) placement = shift_down(placement);
                want(equal(placement, perform_clears(placement)),
                    `piece at ${r},${c}`);
                want(equal(placement, or(game.board, placement)),
                    `piece at ${r},${c} lands on an empty board`);
            }
        }
    }
});

// === where a piece fits ===

checkAll("a piece fits an empty board and not a full one", (want) => {
    want(!can_place_piece(EMPTY, EMPTY), "nothing fits nowhere");
    want(!has_valid_move(EMPTY, [EMPTY, EMPTY, EMPTY]), "an empty deck is over");
    for (const p of PIECES) {
        want(can_place_piece(EMPTY, p), "fits an empty board");
        want(can_place_piece(EMPTY, center_piece(p)), "fits when centered");
        want(!can_place_piece(FULL, p), "does not fit a full board");
        want(can_place_piece(diff(FULL, p), p), "fits the hole it left");
        want(has_valid_move(EMPTY, [EMPTY, p, EMPTY]), "one piece is a move");
        want(!has_valid_move(FULL, [p, p, p]), "a full board is over");
    }
});

// === combos ===

checkAll("a combo counts the lines a board completes", (want) => {
    want(get_combo_magnitude(EMPTY) === 0, "an empty board completes nothing");
    want(get_combo_magnitude(FULL) === 9 * 3, "a full board completes all 27");
    for (const p of PIECES) {
        want(get_combo_magnitude(p) === 0, "a piece completes nothing");
    }
    for (let i = 0; i < 9; ++i) {
        want(get_combo_magnitude(row(i)) === 1, `row ${i}`);
        want(get_combo_magnitude(column(i)) === 1, `column ${i}`);
        want(get_combo_magnitude(cube(i)) === 1, `cube ${i}`);
        for (let j = 0; j < 9; ++j) {
            want(get_combo_magnitude(or(row(i), column(j))) === 2,
                `row ${i} and column ${j}`);
            if (i !== j) {
                want(get_combo_magnitude(or(row(i), row(j))) === 2,
                    `rows ${i} and ${j}`);
                want(get_combo_magnitude(or(column(i), column(j))) === 2,
                    `columns ${i} and ${j}`);
            }
            for (let k = 0; k < 9; ++k) {
                want(get_combo_magnitude(or(cube(k), or(row(i), column(j)))) === 3,
                    `row ${i}, column ${j} and cube ${k}`);
            }
        }
        for (let k = 0; k < 9; ++k) {
            want(get_combo_magnitude(or(row(i), cube(k))) === 2,
                `row ${i} and cube ${k}`);
            want(get_combo_magnitude(or(column(i), cube(k))) === 2,
                `column ${i} and cube ${k}`);
        }
    }
});

// === a piece under a finger ===

// Held over a square it fits in, a piece goes in that square. Held over one it
// does not, it is nudged into the nearest square it does. Held nowhere near a
// square it fits in, it goes nowhere. test/engine/placement-test.js goes
// through this properly; these are the cases that used to sit in blokie.js.
checkAll("a dragged piece lands where it is held, or nearest to it", (want) => {
    want(equal(place_nearest(new_game(), PIECES[0], 4.1, 3.9, 1.5)
        .placement, bit(4, 4)), "rounds to the square under it");
    want(is_disjoint(place_nearest(
        { ...new_game(), board: bit(4, 4) }, PIECES[0], 4, 4, 1.5).placement,
        bit(4, 4)), "moves off an occupied square");
    want(equal(place_nearest(new_game(), PIECES[0], -0.8, 0, 1.5)
        .placement, bit(0, 0)), "pulls back onto the board");
    want(place_nearest(new_game(), PIECES[0], 4, 12, 1.5) === null,
        "gives up when held too far away");
    want(place_nearest(new_game(), getEmpty(), 4, 4, 1.5) === null,
        "an empty piece goes nowhere");
});

if (failures > 0) {
    console.error("%d check(s) failed", failures);
    process.exit(1);
}
console.log("all bitboard checks passed");
