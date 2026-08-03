"use strict";
import { blokie } from "../engine/js/blokie.js";

// The game lives in cookies so it survives a refresh: the board, score, and
// pieces on deck go in one cookie, who is playing goes in another. Both are
// written whenever they change and read once, on load.

const GAME_COOKIE = 'blokie_game';
const ASSIST_COOKIE = 'blokie_assist';
const SFX_COOKIE = 'blokie_sfx';
const COOKIE_MAX_AGE_S = 60 * 60 * 24 * 365;  // a year

// Bump when the field layout changes, so an older cookie is ignored rather
// than read as garbage.
const SAVE_VERSION = 1;
// version, board (3), score, streak flag, then 3 pieces of 3 fields each.
const SAVE_FIELDS = 15;

// Each field of a bitboard carries 27 of the board's 81 squares.
const MAX_BITBOARD_FIELD = 0x7FFFFFF;

function setCookie(name, value) {
    // No path attribute: the cookie is then scoped to the directory the app is
    // served from instead of the whole origin. Secure is skipped off https,
    // where it would drop the cookie (local development, mainly).
    const secure = window.location.protocol === 'https:' ? '; Secure' : '';
    try {
        document.cookie = `${name}=${value}; max-age=${COOKIE_MAX_AGE_S}; SameSite=Lax${secure}`;
    } catch (e) {
        // Cookies can be blocked outright. The game plays fine, it just won't
        // be there after a refresh.
    }
}

function getCookie(name) {
    let jar;
    try {
        jar = document.cookie;
    } catch (e) {
        return null;
    }
    for (const entry of jar.split(';')) {
        const separator = entry.indexOf('=');
        if (separator < 0) continue;
        if (entry.slice(0, separator).trim() === name) {
            return entry.slice(separator + 1).trim();
        }
    }
    return null;
}

// A save is a flat list of numbers in a fixed order, joined by '.'. Nothing in
// it needs escaping, so the cookie value stays short and legible.
function encodeGameState(game_state) {
    const game = game_state.game;
    const fields = [
        SAVE_VERSION,
        game.board.a, game.board.b, game.board.c,
        game.score,
        game.previous_move_was_clear ? 1 : 0,
    ];
    for (const piece of game_state.piece_set) {
        fields.push(piece.a, piece.b, piece.c);
    }
    return fields.join('.');
}

function isBitboardField(n) {
    return Number.isInteger(n) && n >= 0 && n <= MAX_BITBOARD_FIELD;
}

// Returns a game state shaped like the one a new game starts with, or null if
// the save is from another version, truncated, or otherwise not usable.
function decodeGameState(saved) {
    const fields = saved.split('.').map(Number);
    if (fields.length !== SAVE_FIELDS || fields[0] !== SAVE_VERSION) return null;

    const [, board_a, board_b, board_c, score, was_clear] = fields;
    if (![board_a, board_b, board_c].every(isBitboardField)) return null;
    if (!Number.isInteger(score) || score < 0) return null;
    if (was_clear !== 0 && was_clear !== 1) return null;

    const piece_fields = fields.slice(6);
    if (!piece_fields.every(isBitboardField)) return null;
    // Every slot gets its own object, so emptying one can never empty another
    // that happens to be holding the same shape.
    const piece_set = [0, 1, 2].map(i => ({
        a: piece_fields[i * 3],
        b: piece_fields[i * 3 + 1],
        c: piece_fields[i * 3 + 2],
    }));

    return {
        // The move that got here is not saved, only where it left the game.
        game: {
            board: { a: board_a, b: board_b, c: board_c },
            previous_piece_placement: blokie.getEmptyPiece(),
            previous_piece: blokie.getEmptyPiece(),
            previous_move_was_clear: was_clear === 1,
            score: score,
        },
        piece_set: piece_set,
        game_over: false,  // recomputed from the board and the deck
    };
}

let last_saved_game = null;

// Called on every rendered frame that changed something. The assist can move
// the game on many times between frames and every write reserializes the whole
// cookie, so only an actual change to the saved fields reaches the jar.
function saveGameState(game_state) {
    const encoded = encodeGameState(game_state);
    if (encoded === last_saved_game) return;
    last_saved_game = encoded;
    setCookie(GAME_COOKIE, encoded);
}

function loadGameState() {
    const saved = getCookie(GAME_COOKIE);
    if (saved === null) return null;
    const game_state = decodeGameState(saved);
    if (game_state !== null) {
        last_saved_game = saved;
    }
    return game_state;
}

// Who plays: an assist speed in milliseconds, or 'off' for manual. Stored as
// written and checked against the picker's options on the way back in, so a
// stale or edited value can't select something that isn't offered.
function saveAssistSetting(value) {
    setCookie(ASSIST_COOKIE, value);
}

function loadAssistSetting() {
    return getCookie(ASSIST_COOKIE);
}

// Whether sound is on, as 'on' or 'off'. Anything else, including no cookie at
// all, reads as off: a page that starts making noise on its own is a page
// people close, so it stays quiet until someone asks for it.
function saveSfxSetting(on) {
    setCookie(SFX_COOKIE, on ? 'on' : 'off');
}

function loadSfxSetting() {
    return getCookie(SFX_COOKIE) === 'on';
}

export {
    saveGameState,
    loadGameState,
    saveAssistSetting,
    loadAssistSetting,
    saveSfxSetting,
    loadSfxSetting,
    encodeGameState,
    decodeGameState,
};
