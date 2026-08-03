"use strict";

// Where a dragged piece is taken to be going. A piece under a finger sits
// between squares, and the reading of it used to be the square it sat closest
// to and nothing else: overlap a block by a corner, or hang a row off the edge
// of the board, and the piece went nowhere. What is checked here is that the
// reading still lands on that square whenever the piece fits there, and finds
// the closest square it does fit in when it doesn't.

import { blokie } from '../../engine/blokie.js';

let failures = 0;
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return;
    }
    console.log("ok - %s", description);
}

function sameBitboard(a, b) {
    return a.a === b.a && a.b === b.b && a.c === b.c;
}

function gameWithBoard(board) {
    return {
        board: board,
        previous_piece_placement: blokie.getEmptyPiece(),
        previous_piece: blokie.getEmptyPiece(),
        previous_move_was_clear: false,
        score: 0,
    };
}

// The corner a placement puts the piece in, which is what the drag is aiming
// at: the top left of the box the placed squares fill.
function placementCorner(placement) {
    let row = 9;
    let col = 9;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (blokie.at(placement, r, c)) {
                row = Math.min(row, r);
                col = Math.min(col, c);
            }
        }
    }
    return { row: row, col: col };
}

function distance(corner, row, col) {
    return Math.hypot(corner.row - row, corner.col - col);
}

// Every placement of the piece the board has room for, found without going
// through the code under test: the drag reading has to agree with this.
function everyLegalCorner(game, piece) {
    const corners = [];
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (blokie.tryPlacePiece(game, piece, r, c) !== null) {
                corners.push({ row: r, col: c });
            }
        }
    }
    return corners;
}

const RADIUS = 1.5;
const SINGLE = { a: 1, b: 0, c: 0 };  // one square, so its corner is itself

// === Held over a square it fits in ===

const empty_game = gameWithBoard(blokie.getEmptyPiece());

check(sameBitboard(
    blokie.nearestValidPlacement(empty_game, SINGLE, 4.2, 3.8, RADIUS).placement,
    blokie.tryPlacePiece(empty_game, SINGLE, 4, 4).placement),
    "a piece over a square it fits in goes in that square");

check(blokie.nearestValidPlacement(empty_game, SINGLE, 0, 0, RADIUS) !== null
    && blokie.nearestValidPlacement(empty_game, SINGLE, 8, 8, RADIUS) !== null,
    "the corners of the board are reachable");

// === Held over a square it does not fit in ===

// A single square held dead centre on a block. Every neighbour is a square
// away, so which one it picks is a coin toss; that it picks one is the point.
const blocked_middle = gameWithBoard(blokie.tryPlacePiece(empty_game, SINGLE, 4, 4).placement);
const nudged = blokie.nearestValidPlacement(blocked_middle, SINGLE, 4, 4, RADIUS);
check(nudged !== null, "a piece over an occupied square still finds a placement");
check(nudged !== null && distance(placementCorner(nudged.placement), 4, 4) === 1,
    "it is nudged to a square that touches the one it was held over");
check(nudged !== null
    && blokie.placePiece(blocked_middle, SINGLE, nudged.placement) !== null,
    "the placement it settles on is legal");

// Off the edge of the board, which the exact reading used to refuse outright.
check(sameBitboard(
    blokie.nearestValidPlacement(empty_game, SINGLE, -0.9, 4, RADIUS).placement,
    blokie.tryPlacePiece(empty_game, SINGLE, 0, 4).placement),
    "a piece hanging off the top of the board is pulled back onto it");
check(sameBitboard(
    blokie.nearestValidPlacement(empty_game, SINGLE, 4, 9.4, RADIUS).placement,
    blokie.tryPlacePiece(empty_game, SINGLE, 4, 8).placement),
    "a piece hanging off the right of the board is pulled back onto it");

// A five long bar has one column of the board to sit in per row, so holding it
// anywhere on that row means the same placement.
const BAR = { a: 31, b: 0, c: 0 };
check(sameBitboard(
    blokie.nearestValidPlacement(empty_game, BAR, 3, 4.3, RADIUS).placement,
    blokie.tryPlacePiece(empty_game, BAR, 3, 4).placement),
    "a piece too wide to be centred lands in the only column it fits in");

// === Held nowhere near a square it fits in ===

check(blokie.nearestValidPlacement(empty_game, SINGLE, 4, 12, RADIUS) === null,
    "a piece held well off the board is not placed");
check(blokie.nearestValidPlacement(empty_game, SINGLE, 4, 4, 0) === null
    || distance(placementCorner(
        blokie.nearestValidPlacement(empty_game, SINGLE, 4, 4, 0).placement), 4, 4) === 0,
    "no slack means only the square the piece is exactly on");
check(blokie.nearestValidPlacement(gameWithBoard(blokie.getEmptyPiece()), blokie.getEmptyPiece(),
    4, 4, RADIUS) === null,
    "an empty deck slot is not placed");

const full_board = gameWithBoard(
    [...Array(9).keys()].reduce(
        (board, r) => [...Array(9).keys()].reduce((b, c) => blokie.toggleSquare(b, r, c), board),
        blokie.getEmptyPiece()));
check(blokie.nearestValidPlacement(full_board, SINGLE, 4, 4, RADIUS) === null,
    "a piece that fits nowhere is not placed");

// === Against every placement the board has room for ===

// Boards, pieces and grips at random, checked against the placements found by
// hand above. Seeded, so a failure here is a failure that can be looked at.
function sfc32(a, b, c, d) {
    return function () {
        a |= 0; b |= 0; c |= 0; d |= 0;
        const t = (a + b | 0) + d | 0;
        d = d + 1 | 0;
        a = b ^ b >>> 9;
        b = c + (c << 3) | 0;
        c = c << 21 | c >>> 11;
        c = c + t | 0;
        return (t >>> 0) / 4294967296;
    };
}
const random = sfc32(0x9e3779b9, 0x243f6a88, 0xb7e15162, 42);

let always_legal = true;
let always_nearest = true;
let never_pulled_too_far = true;
let never_missed = true;
let matches_exact_reading = true;
let nudges_seen = 0;

for (let trial = 0; trial < 3000; ++trial) {
    // A board with a scattering of blocks on it, from nearly clear to nearly
    // full, so pieces get held over room and over blocks in similar numbers.
    let board = blokie.getEmptyPiece();
    const fullness = random();
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (random() < fullness) {
                board = blokie.toggleSquare(board, r, c);
            }
        }
    }
    const game = gameWithBoard(board);
    const piece = blokie.getRandomPieceSet()[0];

    // Held anywhere from a little off the top left of the board to a little off
    // the bottom right, in quarter squares so exact halves come up often.
    const row = Math.round((random() * 13 - 2) * 4) / 4;
    const col = Math.round((random() * 13 - 2) * 4) / 4;

    const result = blokie.nearestValidPlacement(game, piece, row, col, RADIUS);

    const legal = everyLegalCorner(game, piece);
    const reachable = legal.filter(corner => distance(corner, row, col) <= RADIUS);
    const nearest = reachable.reduce(
        (best, corner) => Math.min(best, distance(corner, row, col)), Infinity);

    if (result === null) {
        if (reachable.length > 0) {
            never_missed = false;
        }
        continue;
    }

    if (blokie.placePiece(game, piece, result.placement) === null
        || blokie.isEmpty(result.placement)) {
        always_legal = false;
    }

    const corner = placementCorner(result.placement);
    const chosen = distance(corner, row, col);
    if (chosen > RADIUS) {
        never_pulled_too_far = false;
    }
    if (chosen > nearest) {
        always_nearest = false;
    }
    if (chosen > 0) {
        nudges_seen++;
    }

    // The reading this replaced: the square the piece sits closest to, taken
    // only when the piece fits there. Everywhere that used to answer, this
    // still answers the same, so nothing that worked before moved.
    const exact = blokie.tryPlacePiece(game, piece, Math.round(row), Math.round(col));
    if (exact !== null && !sameBitboard(exact.placement, result.placement)) {
        matches_exact_reading = false;
    }
}

check(nudges_seen > 0, "the fuzz reached placements the piece had to be nudged into");
check(always_legal, "every placement it answered with was legal");
check(never_pulled_too_far, "it never pulled a piece further than it was allowed to");
check(always_nearest, "it always answered with the nearest placement it could have");
check(never_missed, "it never refused a piece there was a placement in reach for");
check(matches_exact_reading,
    "it agrees with the exact reading wherever the exact reading had an answer");

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
