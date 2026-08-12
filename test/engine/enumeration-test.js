"use strict";

// The solver's move search. It plays the deck in sorted order and then, when a
// clear is on the table, walks the other orderings too -- skipping any line of
// play whose board it argues some earlier ordering already reached. Those skips
// are the fiddliest part of the engine and the easiest to quietly break, so
// what is checked here is the property they claim: that the search still sees
// every board the deck can reach, and picks the best of them.
//
// The check is a brute force over every ordering and every placement, built out
// of the same public API the app plays moves with. It is far too slow to be the
// engine, but it cannot skip anything.

import { blokie, bits, init } from '../../engine/js/blokie.js';

await init();

let failures = 0;
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return false;
    }
    console.log("ok - %s", description);
    return true;
}

const EMPTY = bits.empty();

function gameWithBoard(board, previous_move_was_clear = false) {
    return {
        board: board,
        previous_move_was_clear: previous_move_was_clear,
        score: 0,
    };
}

const key = (bb) => `${bb.a},${bb.b},${bb.c}`;

// The solver's board evaluation.
function evaluate(board) {
    return blokie.evaluate(board);
}

// Every legal placement of one piece, as the move it would produce.
function placementsOf(game, piece) {
    if (bits.isEmpty(piece)) {
        return [];
    }
    const bounds = bits.bounds(piece);
    const moves = [];
    for (let r = 0; r + bounds.rows <= 9; ++r) {
        for (let c = 0; c + bounds.cols <= 9; ++c) {
            const move = blokie.placeAt(game, piece, r, c);
            if (move !== null) {
                moves.push(move);
            }
        }
    }
    return moves;
}

// Every board reachable by playing the whole deck, in any order, and the best
// score each of them can be reached with. Blank slots are not moves, so they
// are simply left out.
function reachableBoards(game, deck) {
    const best = new Map();
    const walk = (state, remaining) => {
        if (remaining.length === 0) {
            const k = key(state.board);
            const seen = best.get(k);
            if (seen === undefined || state.score > seen.score) {
                best.set(k, { board: state.board, score: state.score });
            }
            return;
        }
        for (const slot of remaining) {
            const rest = remaining.filter((other) => other !== slot);
            for (const move of placementsOf(state, deck[slot])) {
                walk(move.new_game, rest);
            }
        }
    };
    walk(game, deck.map((_, i) => i).filter((i) => !bits.isEmpty(deck[i])));
    return best;
}

// Replays what the solver came back with, as the app would: every placement has
// to be legal, every slot it names has to hold the piece it placed, and the
// boards it reports have to be the ones that come out.
function replay(game, deck, result) {
    const used = new Set();
    let state = game;
    for (const planned of result.moves) {
        if (bits.isEmpty(planned.placement) || used.has(planned.piece_index)) {
            return null;
        }
        used.add(planned.piece_index);
        // The placement has to be the piece in the slot it names, moved onto
        // the board, and not some other shape the search preferred. A placement
        // carries no record of which piece it came from, so this is the only
        // thing tying the two halves of a planned move together.
        if (!bits.equals(bits.justify(planned.placement),
            bits.justify(deck[planned.piece_index]))) {
            return null;
        }
        const move = blokie.place(state, planned.placement);
        if (move === null) {
            return null;
        }
        state = move.new_game;
    }
    // Replaying the moves has to land on the game it reported. That is the
    // whole of what the app relies on: the worker plans, the main thread
    // replays the plan down the same path a dropped piece takes, and the two
    // have to agree about where that leaves the game.
    if (key(state.board) !== key(result.game.board)
        || state.score !== result.game.score) {
        return null;
    }
    return { board: state.board, score: state.score, pieces_played: used.size };
}

// One position, checked to the hilt.
function checkPosition(name, board, deck, previous_move_was_clear = false) {
    const game = gameWithBoard(board, previous_move_was_clear);
    const reachable = reachableBoards(game, deck);
    const result = blokie.makeMove(game, deck);
    const played = replay(game, deck, result);
    const held = deck.filter((p) => !bits.isEmpty(p)).length;

    if (reachable.size === 0) {
        // There is no way to place every piece. The solver says so by coming
        // back empty handed; blokie.plan is what turns that into a smaller move.
        check(!result.found && result.moves.length === 0,
            `${name}: places nothing when the whole deck does not fit`);
        return;
    }

    if (!check(played !== null && played.pieces_played === held,
        `${name}: the move it returns is a legal play of the whole deck`)) {
        return;
    }

    let best_evaluation = Infinity;
    for (const { board: candidate } of reachable.values()) {
        best_evaluation = Math.min(best_evaluation, evaluate(candidate));
    }
    check(result.evaluation === best_evaluation,
        `${name}: settles on the best of the ${reachable.size} boards the deck can reach`
        + ` (${result.evaluation} vs ${best_evaluation})`);

    const landed = reachable.get(key(played.board));
    check(landed !== undefined && evaluate(played.board) === result.evaluation,
        `${name}: the board it played to is the one it scored`);
    check(landed !== undefined && played.score === landed.score,
        `${name}: takes the highest scoring way of reaching that board`
        + ` (${played.score} vs ${landed && landed.score})`);
}

// === Positions where the ordering of the deck is load bearing ===

// Found by searching for positions whose best board no ordering of the deck
// reaches except a re-ordered one. Play these decks in the order the solver
// sorts them into and the answers below are out of reach.

// Three pieces, and the only way to fit them all is to clear a line partway
// through. In sorted order the deck does not fit at all, so a search that
// stopped at the first ordering would report that there is no move here.
checkPosition("a clear partway through is the only way the deck fits",
    // ..####...  .........  .#.##....  ###.###.#  ##.###..#  .........
    // #.#.##..#  ##.#.#...  ##.####.#
    { a: 6815804, b: 161655, c: 99374901 },
    [
        // ##.  .#.  ##.
        { a: 787459, b: 0, c: 0 },
        // ###  .#.  .#.
        { a: 525319, b: 0, c: 0 },
        // ..#  ..#  ###
        { a: 1837060, b: 0, c: 0 },
    ]);

// The deck fits in sorted order, but a tidier board is only reachable if the
// pieces go down in a different one.
checkPosition("a re-ordered deck reaches a tidier board",
    // ..#.##.#.  #..####..  #.#...#.#  ##.....#.  ###.....#  ###....##
    // #.###....  .#.#.##.#  ...####..
    { a: 85258932, b: 102633091, c: 31642653 },
    [
        // .#.  #..
        { a: 514, b: 0, c: 0 },
        // #  #  #
        { a: 262657, b: 0, c: 0 },
        // ####
        { a: 15, b: 0, c: 0 },
    ]);

// Every ordering here lands on the same board, but they do not all score the
// same, and two of the three pieces are the same shape.
checkPosition("the best scoring order of a deck holding two of a kind",
    // #.#..#...  ##..##..#  ####.#...  ##.##....  ###..#..#  ####.#...
    // .##.##..#  #...##..#  ######...
    { a: 12477989, b: 12471835, c: 16671542 },
    [
        // ##.  .#.  .#.
        { a: 525315, b: 0, c: 0 },
        // ###  #..  #..
        { a: 262663, b: 0, c: 0 },
        // ##.  .#.  .#.
        { a: 525315, b: 0, c: 0 },
    ],
    /*previous_move_was_clear=*/true);

// The search walks the orderings with the piece that has the most placements
// first, and takes the first of them as having already seen every board no
// clear can move -- so the first ordering is the one that must not skip
// anything. It used to get that for free by walking the deck in sorted order.
// Sorting by how many placements a piece has does not keep the first two
// pieces sorted, and once the first ordering started skipping the half of its
// pairs that were back to front, it took the best board here down with it.
checkPosition("the first ordering is the flexible one, not the sorted one",
    // ...###...  ....##...  ...##....  #........  .........  ##.......
    // .........  .........  ##.......
    { a: 6316088, b: 786433, c: 786432 },
    [
        // ..#  ###  ..#
        { a: 1052164, b: 0, c: 0 },
        // ..#  ###  ..#
        { a: 1052164, b: 0, c: 0 },
        // ##  ##
        { a: 1539, b: 0, c: 0 },
    ]);

// === A sweep of random positions ===

// The 47 pieces, recovered through the public API and put in a fixed order so
// the sweep below deals the same decks every run.
const PIECES = [];
{
    const seen = new Set();
    for (let i = 0; i < 5000 && PIECES.length < 47; ++i) {
        for (const piece of blokie.deal()) {
            const justified = bits.justify(piece);
            if (!seen.has(key(justified))) {
                seen.add(key(justified));
                PIECES.push(justified);
            }
        }
    }
    PIECES.sort((a, b) => a.a - b.a || a.b - b.b || a.c - b.c);
}
check(PIECES.length === 47, "the deck deals all 47 pieces");

function mulberry32(seed) {
    return function () {
        seed = (seed + 0x6D2B79F5) | 0;
        let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

// The rows, columns and boxes that are completely filled. A board in play never
// has one -- it is cleared the moment it is made -- and the engine counts on
// that, so a board dealt here must not have one either.
function completedLines(board) {
    const lines = [];
    for (let i = 0; i < 9; ++i) {
        const row = [], column = [], box = [];
        for (let j = 0; j < 9; ++j) {
            row.push([i, j]);
            column.push([j, i]);
            box.push([Math.floor(i / 3) * 3 + Math.floor(j / 3), (i % 3) * 3 + (j % 3)]);
        }
        for (const line of [row, column, box]) {
            if (line.every(([r, c]) => bits.at(board, r, c))) {
                lines.push(line);
            }
        }
    }
    return lines;
}

// Busy boards, so the brute force stays cheap and clears stay likely -- an
// empty board has thousands of placements and nothing to clear, which is the
// case the search has the least to think about.
function randomBoard(random, fullness) {
    let board = blokie.newGame().board;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (random() < fullness) {
                board = bits.toggle(board, r, c);
            }
        }
    }
    // Fill a line and punch holes back in it, so a piece or two can complete it.
    const row = Math.floor(random() * 9);
    for (let c = 0; c < 9; ++c) {
        if (!bits.at(board, row, c)) {
            board = bits.toggle(board, row, c);
        }
    }
    for (let i = 0; i < 2 + Math.floor(random() * 3); ++i) {
        const c = Math.floor(random() * 9);
        if (bits.at(board, row, c)) {
            board = bits.toggle(board, row, c);
        }
    }
    for (;;) {
        const lines = completedLines(board);
        if (lines.length === 0) {
            return board;
        }
        const [r, c] = lines[0][Math.floor(random() * 9)];
        board = bits.toggle(board, r, c);
    }
}

const random = mulberry32(20240817);
let swept = 0;
let with_a_move = 0;
const before_sweep = failures;
for (let trial = 0; trial < 1500; ++trial) {
    const board = randomBoard(random, 0.4 + 0.3 * random());
    const deck = [0, 1, 2].map(() => PIECES[Math.floor(random() * PIECES.length)]);
    // Decks that repeat a shape, and decks with slots already played out of,
    // both take different routes through the search.
    if (trial % 4 === 0) {
        deck[1] = deck[0];
    }
    if (trial % 7 === 0) {
        deck[2] = EMPTY;
    }
    if (trial % 13 === 0) {
        deck[1] = EMPTY;
    }

    const game = gameWithBoard(board, trial % 2 === 0);
    const reachable = reachableBoards(game, deck);
    const result = blokie.makeMove(game, deck);
    const played = replay(game, deck, result);
    const held = deck.filter((p) => !bits.isEmpty(p)).length;
    swept++;

    if (reachable.size === 0) {
        if (result.found || result.moves.length !== 0) {
            check(false, `sweep trial ${trial}: placed something it could not place`);
        }
        continue;
    }
    with_a_move++;

    if (played === null || played.pieces_played !== held) {
        check(false, `sweep trial ${trial}: returned a move that cannot be played`);
        continue;
    }

    let best_evaluation = Infinity;
    for (const { board: candidate } of reachable.values()) {
        best_evaluation = Math.min(best_evaluation, evaluate(candidate));
    }
    if (result.evaluation !== best_evaluation) {
        check(false, `sweep trial ${trial}: missed a better board`
            + ` (${result.evaluation} vs ${best_evaluation}); board ${key(board)},`
            + ` deck ${deck.map(key).join(" / ")}`);
        continue;
    }

    const landed = reachable.get(key(played.board));
    if (landed === undefined || evaluate(played.board) !== result.evaluation) {
        check(false, `sweep trial ${trial}: played to a board it did not score`);
        continue;
    }
    if (played.score !== landed.score) {
        check(false, `sweep trial ${trial}: left ${landed.score - played.score} points behind`
            + `; board ${key(board)}, deck ${deck.map(key).join(" / ")}`);
    }
}
check(failures === before_sweep,
    `${swept} random positions (${with_a_move} with a move) match the brute force`);

// The plan the app actually asks for has to be playable too, on the same
// positions the search above was checked on.
{
    const board = { a: 6815804, b: 161655, c: 99374901 };
    const deck = [
        { a: 787459, b: 0, c: 0 },
        { a: 525319, b: 0, c: 0 },
        { a: 1837060, b: 0, c: 0 },
    ];
    let game = gameWithBoard(board);
    const plan = blokie.plan(game, deck);
    let legal = plan.length > 0;
    for (const move of plan) {
        const result = blokie.place(game, move.placement);
        if (result === null) {
            legal = false;
            break;
        }
        game = result.new_game;
    }
    check(plan.length === 3, "the plan for that position places all three pieces");
    check(legal, "every placement in the plan is legal");
}

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
