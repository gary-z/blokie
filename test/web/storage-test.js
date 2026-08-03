"use strict";

// Round trips the saved-game cookie value. Cookies themselves need a browser,
// but the encoding either survives a refresh or quietly loses someone's game,
// so it is worth checking here.

import { blokie } from '../../engine/js/blokie.js';
import { encodeGameState, decodeGameState } from '../../web/storage.js';

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

function newGameState() {
    return {
        game: blokie.getNewGame(),
        piece_set: blokie.getRandomPieceSet(),
        game_over: false,
    };
}

// Plays the deck onto the board wherever each piece first fits, so the saved
// state under test has a real board, score, and part-emptied deck.
function playSomePieces(game_state) {
    for (let i = 0; i < game_state.piece_set.length; ++i) {
        const piece = game_state.piece_set[i];
        for (let r = 0; r < 9; ++r) {
            for (let c = 0; c < 9; ++c) {
                const result = blokie.tryPlacePiece(game_state.game, piece, r, c);
                if (result) {
                    game_state.game = result.newGame;
                    game_state.piece_set[i] = blokie.getEmptyPiece();
                    r = c = 9;
                }
            }
        }
    }
    return game_state;
}

// A hundred random decks cover empty slots, repeated shapes, and cleared rows.
let all_round_tripped = true;
let saw_empty_slot = false;
let saw_score = false;
for (let i = 0; i < 100; ++i) {
    // Leave the last piece on deck so both a played and an unplayed slot are covered.
    const original = newGameState();
    const kept_piece = original.piece_set[2];
    playSomePieces(original);
    original.piece_set[2] = kept_piece;

    const restored = decodeGameState(encodeGameState(original));
    if (restored === null) {
        all_round_tripped = false;
        break;
    }
    saw_empty_slot = saw_empty_slot || original.piece_set.some(p => blokie.isEmpty(p));
    saw_score = saw_score || original.game.score > 0;

    const matches =
        sameBitboard(restored.game.board, original.game.board) &&
        restored.game.score === original.game.score &&
        restored.game.previous_move_was_clear === original.game.previous_move_was_clear &&
        restored.piece_set.every((p, j) => sameBitboard(p, original.piece_set[j])) &&
        restored.game_over === false;
    if (!matches) {
        all_round_tripped = false;
        break;
    }
}
check(all_round_tripped, "board, score, streak, and deck survive a round trip");
check(saw_empty_slot, "played-out deck slots are covered by the round trip");
check(saw_score, "a scoring game is covered by the round trip");

const fresh = decodeGameState(encodeGameState(newGameState()));
check(fresh !== null && blokie.isEmpty(fresh.game.board) && fresh.game.score === 0,
    "a brand new game round trips");

// The deck is searched by object identity, so slots holding the same shape
// still have to be separate objects after a restore.
const twins = newGameState();
twins.piece_set = [twins.piece_set[0], twins.piece_set[0], twins.piece_set[0]];
const restored_twins = decodeGameState(encodeGameState(twins));
check(restored_twins.piece_set[0] !== restored_twins.piece_set[1]
    && restored_twins.piece_set[1] !== restored_twins.piece_set[2],
    "identical deck slots restore as separate objects");

const big = newGameState();
big.game = { ...big.game, score: 1500000 };
check(decodeGameState(encodeGameState(big)).game.score === 1500000,
    "a million-point score round trips");

const streak = newGameState();
streak.game = { ...streak.game, previous_move_was_clear: true };
check(decodeGameState(encodeGameState(streak)).game.previous_move_was_clear === true,
    "the clear streak flag round trips");

// Anything that isn't a save this version wrote is refused, so a bad cookie
// starts a new game instead of loading a broken one.
const rejected = {
    "empty": "",
    "junk": "hello",
    "another version": "2.0.0.0.0.0.1.0.0.1.0.0.1.0.0",
    "truncated": "1.0.0.0.0.0.1.0.0",
    "over-long": "1.0.0.0.0.0.1.0.0.1.0.0.1.0.0.7",
    "non-numeric field": "1.x.0.0.0.0.1.0.0.1.0.0.1.0.0",
    "out-of-range board": "1.999999999.0.0.0.0.1.0.0.1.0.0.1.0.0",
    "negative field": "1.-1.0.0.0.0.1.0.0.1.0.0.1.0.0",
    "fractional field": "1.0.0.0.1e-3.0.1.0.0.1.0.0.1.0.0",
    "streak flag that isn't a flag": "1.0.0.0.0.5.1.0.0.1.0.0.1.0.0",
};
for (const [description, value] of Object.entries(rejected)) {
    check(decodeGameState(value) === null, `rejects ${description}`);
}

// A cookie value has to stay well inside the ~4KB a browser will keep.
const longest = encodeGameState({
    game: {
        board: { a: 0x7FFFFFF, b: 0x7FFFFFF, c: 0x7FFFFFF },
        score: Number.MAX_SAFE_INTEGER,
        previous_move_was_clear: true,
    },
    piece_set: [0, 1, 2].map(() => ({ a: 0x7FFFFFF, b: 0x7FFFFFF, c: 0x7FFFFFF })),
});
check(longest.length < 200, `the largest possible save is small (${longest.length} bytes)`);

if (failures > 0) {
    console.error("%d check(s) failed", failures);
    process.exit(1);
}
console.log("All storage checks passed.");
