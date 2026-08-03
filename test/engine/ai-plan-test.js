"use strict";

// The assist's move planning. The solver underneath only ever plans moves that
// place every piece it is given, so what is checked here is what happens when
// no such move exists: the assist has to keep playing what does fit, right up
// to the point the game is genuinely over.

import { blokie, init } from '../../engine/js/blokie.js';

await init();

let failures = 0;
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return;
    }
    console.log("ok - %s", description);
}

function gameWithBoard(board, score = 0) {
    return {
        board: board,
        previous_piece_placement: blokie.getEmptyPiece(),
        previous_piece: blokie.getEmptyPiece(),
        previous_move_was_clear: false,
        score: score,
    };
}

// A real position, reached by playing pieces at random until the solver gave
// up. Two of the three slots hold something, the first of them fits nowhere,
// and the last one fits in exactly one place.
//
// .........  ..#######  ###.#####  .######.#  ..#.##...
// ###.#.#..  .##.#####  ....#.###  ....#....
const STUCK_BOARD = { a: 132118528, b: 22833534, c: 4432374 };
const STUCK_DECK = [
    { a: 1054720, b: 12, c: 0 },
    { a: 0, b: 0, c: 0 },
    { a: 3670016, b: 4, c: 0 },
];

check(blokie.hasValidMove(STUCK_BOARD, STUCK_DECK),
    "the fixture is a position the game is not over in");
check(!blokie.canPlacePiece(STUCK_BOARD, STUCK_DECK[0])
    && blokie.canPlacePiece(STUCK_BOARD, STUCK_DECK[2]),
    "the fixture holds one piece that fits and one that does not");

const stuck_game = gameWithBoard(STUCK_BOARD);

// What the bug looked like: asked for the whole deck, the solver answers with a
// filled board and nothing placed anywhere.
const whole_deck = blokie.getAIMove(stuck_game, STUCK_DECK);
check(whole_deck.new_game_states.every(s => blokie.isEmpty(s.previous_piece_placement)),
    "the solver places nothing when it cannot place every piece");

const plan = blokie.getAIPlan(stuck_game, STUCK_DECK);
check(plan.length === 1, "the plan falls back to the one piece that does fit");
check(plan.length === 1 && plan[0].piece_index === 2,
    "the plan names the slot the piece came out of");
check(plan.length === 1
    && blokie.placePiece(stuck_game, STUCK_DECK[plan[0].piece_index], plan[0].placement) !== null,
    "the planned placement is legal");

// A plan for a board with room on it still places all three, in three slots.
const open_deck = blokie.getRandomPieceSet();
const open_plan = blokie.getAIPlan(blokie.getNewGame(), open_deck);
check(open_plan.length === 3, "an open board is still planned three pieces at a time");
check(new Set(open_plan.map(m => m.piece_index)).size === open_plan.length,
    "a plan uses each deck slot at most once");

// Playing the fixture out: the assist has to reach a position where nothing on
// deck fits, rather than stalling on the first deck it cannot fully place. The
// deck is never refilled here, so this is a handful of moves either way.
let game = stuck_game;
let deck = STUCK_DECK.map(p => ({ ...p }));
let moves = 0;
let every_placement_legal = true;
for (;;) {
    const next = blokie.getAIPlan(game, deck);
    if (next.length === 0) {
        break;
    }
    for (const move of next) {
        const result = blokie.placePiece(game, deck[move.piece_index], move.placement);
        if (result === null) {
            every_placement_legal = false;
            break;
        }
        deck[move.piece_index] = blokie.getEmptyPiece();
        game = result.newGame;
        moves++;
    }
    if (!every_placement_legal || moves > 10) {
        break;
    }
}
check(moves > 0, "the assist plays on from a position it used to give up in");
check(every_placement_legal, "every placement it played was legal");
check(!blokie.hasValidMove(game.board, deck),
    "it plays until nothing on deck fits, which is the game being over");

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
