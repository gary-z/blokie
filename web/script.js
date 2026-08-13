"use strict";
import { blokie, bits, init } from "../engine/js/blokie.js";
import { saveGameState, loadGameState, saveAssistSetting, loadAssistSetting } from "./storage.js";
import { initSfx, playSfx } from "./sfx.js";
import { registerServiceWorker } from "./pwa.js";

/** @typedef {import('../engine/js/blokie.js').Game} Game */
/** @typedef {import('../engine/js/blokie.js').Hand} Hand */
/** @typedef {import('../engine/js/blokie.js').Piece} Piece */
/** @typedef {import('../engine/js/blokie.js').Move} Move */
/** @typedef {import('../engine/js/blokie.js').Placement} Placement */
/** @typedef {import('../engine/js/blokie.js').BitBoard} BitBoard */
/** @typedef {import('../engine/js/blokie.js').PieceBounds} PieceBounds */
/** @typedef {import('./storage.js').GameState} GameState */

/**
 * An element index.html is known to define. Throws rather than handing back
 * null, so an id that was renamed on one side and not the other fails at the
 * point it is looked up instead of somewhere further down as a null
 * dereference.
 * @type {(id: string) => HTMLElement}
 */
function element(id) {
    const found = document.getElementById(id);
    if (found === null) {
        throw new Error(`index.html has no element with id "${id}"`);
    }
    return found;
}

/**
 * The same, for a selector inside an element already in hand.
 * @type {(root: ParentNode, selector: string) => HTMLElement}
 */
function child(root, selector) {
    const found = root.querySelector(selector);
    if (found === null) {
        throw new Error(`no element matching "${selector}"`);
    }
    return /** @type {HTMLElement} */ (found);
}


// You play the game by dragging pieces onto the board. The AI assist can play
// for you instead, at a speed chosen in the bottom bar, until you switch it off
// or take a turn yourself.
function getNewGameState() {
    return {
        game: blokie.newGame(),
        piece_set: blokie.deal(),
        game_over: false,  // true once nothing in hand fits anywhere
        // How many moves in a row have cleared, counting the one just played.
        // The engine only knows whether the move before this one cleared, which
        // is all the score needs; a card that says how long the run has been
        // needs the run's length, so it is counted here.
        clear_streak: 0,
    };
}

/**
 * Everything a render reads. The two bitboards are written on every drag and
 * cleared when it ends, so they are named here rather than left to be inferred
 * from the nulls they start as.
 * @typedef {object} AppState
 * @property {GameState} game_state
 * @property {number} assist_request_id
 * @property {BitBoard | null} drag_shadow Shadow cells on the board.
 * @property {BitBoard | null} clear_preview Squares a valid manual placement
 *   would clear.
 * @property {number} piece_in_hand_index Hand slot whose piece is out of
 *   hand: being dragged, or flying to the board (-1 = none).
 */

// In `state` rather than beside it so JSON.stringify change detection triggers
// a re-render when any of it moves.
/** @type {AppState} */
let state = {
    game_state: getNewGameState(),
    assist_request_id: 0,
    drag_shadow: null,
    clear_preview: null,
    piece_in_hand_index: -1,
};

/**
 * A drag in progress. `active` is false until the pointer has moved far enough
 * to count as a drag rather than a tap, which is what the threshold below is
 * measured against.
 * @typedef {object} DragInfo
 * @property {number} pieceIndex
 * @property {Piece} piece
 * @property {PieceBounds} bounds
 * @property {number} startX
 * @property {number} startY
 * @property {boolean} active
 */

// Drag state kept outside `state`: it holds DOM refs, which do not serialize.
/** @type {DragInfo | null} */
let drag_info = null;
/** @type {HTMLElement | null} */
let drag_floating_el = null;

// Rendering is scheduled before the saved game is read back, so nothing is
// written until it has been: a frame drawn in between would otherwise save the
// empty game this file starts with over the one waiting in the cookie.
let ready_to_save = false;

function saveGame() {
    if (ready_to_save) {
        saveGameState(state.game_state);
    }
}

const DRAG_THRESHOLD = 8;
const FINGER_CLEARANCE = 30;  // px of clearance above the touch point

document.addEventListener("DOMContentLoaded", function (event) {
    // Restore who was playing before anything reads the picker.
    const ai_assist = /** @type {HTMLSelectElement} */ (element('ai-assist'));
    const saved_assist = loadAssistSetting();
    if (saved_assist !== null && [...ai_assist.options].some(o => o.value === saved_assist)) {
        ai_assist.value = saved_assist;
    }

    // Switching between manual play and an assist speed restarts the assist
    // from wherever the game currently stands.
    ai_assist.addEventListener('change', () => {
        saveAssistSetting(ai_assist.value);
        showWhoIsPlaying();
        onGameStateChanged();
    });
    showWhoIsPlaying();  // the picker may be carrying a restored selection

    initSettings();
    initSfx(element('sound'));
    registerServiceWorker();

    document.addEventListener('mouseup', (event) => {
        handleDragEnd(event.clientX, event.clientY);
    });
    document.addEventListener('touchend', (event) => {
        if (drag_info) {
            const touch = event.changedTouches[0];
            handleDragEnd(touch.clientX, touch.clientY);
        }
    });

    // Document-level mouse/touch move for drag tracking
    document.addEventListener('mousemove', (event) => {
        if (drag_info) {
            handleDragMove(event.clientX, event.clientY);
        }
    });
    document.addEventListener('touchmove', (event) => {
        if (drag_info) {
            const touch = event.touches[0];
            handleDragMove(touch.clientX, touch.clientY);
            if (drag_info.active) {
                event.preventDefault();
            }
        }
    }, { passive: false });

    // The board itself is not interactive: pieces only land on it by drag.
    element('new-game').addEventListener('click', onNewGame);
    initRestartButton(element('restart'));

    const pieces_in_hand_container = element('pieces-in-hand-container');
    pieces_in_hand_container.addEventListener('touchstart', (event) => {
        if (!gameIsActive()) return;
        const cell = /** @type {HTMLElement} */ (event.target);
        if (cell.nodeName !== 'TD') return;
        const table = cell.closest('table');
        if (table === null || table.className !== 'pieces-in-hand') return;

        const pieceIndex = parseInt(table.id.slice(-1));
        const piece = state.game_state.piece_set[pieceIndex];
        if (bits.isEmpty(piece)) return;

        const touch = event.touches[0];
        drag_info = {
            pieceIndex,
            piece,
            bounds: bits.bounds(piece),
            startX: touch.clientX,
            startY: touch.clientY,
            active: false,
        };
        event.preventDefault();
    });
    pieces_in_hand_container.addEventListener('mousedown', (event) => {
        if (!gameIsActive()) return;
        const cell = /** @type {HTMLElement} */ (event.target);
        if (cell.nodeName !== 'TD') return;
        const table = cell.closest('table');
        if (table === null || table.className !== 'pieces-in-hand') return;

        const pieceIndex = parseInt(table.id.slice(-1));
        const piece = state.game_state.piece_set[pieceIndex];
        if (bits.isEmpty(piece)) return;

        drag_info = {
            pieceIndex,
            piece,
            bounds: bits.bounds(piece),
            startX: event.clientX,
            startY: event.clientY,
            active: false,
        };
        event.preventDefault();
    });

    // If a native drag somehow starts, cancel it and clean up our drag state
    document.addEventListener('dragend', () => {
        if (drag_info) {
            cleanupDrag();
            onGameStateChanged();
        }
    });

    // A hidden tab stops rendering, and with it saving, while the assist keeps
    // playing. Put the game where it actually stands before the page can go.
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'hidden') {
            saveGame();
        }
    });
    window.addEventListener('pagehide', saveGame);

    // Pick the last game back up, or start a fresh one when there is nothing
    // saved to pick up. Saving starts once that is settled, either way.
    const saved_game_state = loadGameState();
    ready_to_save = true;
    if (saved_game_state !== null) {
        state.game_state = saved_game_state;
        onGameStateChanged();
    } else {
        onNewGame();
    }
});

function gameIsActive() {
    return !state.game_state.game_over;
}

// Sound and the Github link live behind the gear in the corner, so the bar
// under the board is only ever about who is playing.
function initSettings() {
    const settings = element('settings');
    const button = element('settings-button');
    const menu = element('settings-menu');

    /** @type {(open: boolean) => void} */
    function setMenuOpen(open) {
        menu.hidden = !open;
        button.setAttribute('aria-expanded', String(open));
    }
    closeSettingsMenu = () => setMenuOpen(false);

    button.addEventListener('click', () => setMenuOpen(menu.hidden));

    // Toggling sound leaves the menu up, so the button can be seen changing
    // and heard again; following the link out is done with it.
    //
    // Anything outside the gear puts the menu away, and it goes on the way down
    // rather than on the click: the menu hangs over the board, so a pointer
    // landing under it is a move being started, not a menu still wanted. Touch
    // is the case that needs the distinction -- the hand cancels the click its
    // touchstart would have produced, so waiting for one leaves the menu open
    // over the board for the whole drag. Click is kept for keyboard presses,
    // which reach a control without a pointer ever going down.
    /** @type {(event: Event) => void} */
    const closeIfOutside = (event) => {
        if (!menu.hidden
            && !settings.contains(/** @type {Node | null} */ (event.target))) {
            setMenuOpen(false);
        }
    };
    document.addEventListener('pointerdown', closeIfOutside);
    document.addEventListener('click', closeIfOutside);
    child(menu, 'a').addEventListener('click', () => setMenuOpen(false));

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && !menu.hidden) {
            setMenuOpen(false);
            button.focus();
        }
    });
}

// === Drag and drop ===

// The piece drawn at board scale rather than hand scale, since the board is
// where it is headed and where it has to be lined up by eye.
/** @type {(piece: Piece, bounds: PieceBounds) => HTMLElement} */
function createFloatingPiece(piece, bounds) {
    const board = getBoardGeometry();

    const el = document.createElement('div');
    el.className = 'floating-piece';
    el.style.position = 'fixed';
    el.style.pointerEvents = 'none';
    el.style.zIndex = '1000';

    const table = document.createElement('table');
    table.style.borderCollapse = 'collapse';

    const p = bits.justify(piece);
    for (let r = 0; r < bounds.rows; r++) {
        const tr = document.createElement('tr');
        for (let c = 0; c < bounds.cols; c++) {
            const td = document.createElement('td');
            td.style.width = board.cellW + 'px';
            td.style.height = board.cellH + 'px';
            td.style.padding = '0';
            td.style.border = '0';
            if (bits.at(p, r, c)) {
                td.style.background = 'rgb(54, 112, 232)';
                td.style.border = '1px solid rgba(0,0,0,0.5)';
            }
            tr.appendChild(td);
        }
        table.appendChild(tr);
    }

    el.appendChild(table);
    document.body.appendChild(el);
    return el;
}

// The board on screen, and the size of one of its squares. Read fresh every
// time: the board is sized off the viewport, so it moves with the window.
/**
 * The board as the drag reads it: where it is on screen, and how big a square
 * is. Measured per drag move rather than cached, since a rotation or a resize
 * moves it under the finger.
 * @typedef {{rect: DOMRect, cellW: number, cellH: number}} BoardGeometry
 */

/** @type {() => BoardGeometry} */
function getBoardGeometry() {
    const rect = element('game-board').getBoundingClientRect();
    return { rect: rect, cellW: rect.width / 9, cellH: rect.height / 9 };
}

// Where the piece being dragged is drawn, in screen pixels: centered on the
// finger and lifted clear of it. The one place this is worked out, so the piece
// you see, the shadow under it and the square it lands in can't disagree.
/**
 * @type {(clientX: number, clientY: number, bounds: PieceBounds)
 *     => {left: number, top: number, width: number, height: number,
 *         board: BoardGeometry}}
 */
function getFloatingPieceRect(clientX, clientY, bounds) {
    const board = getBoardGeometry();
    const width = bounds.cols * board.cellW;
    const height = bounds.rows * board.cellH;
    return {
        left: clientX - width / 2,
        top: clientY - FINGER_CLEARANCE - height,
        width: width,
        height: height,
        board: board,
    };
}

/**
 * @type {(el: HTMLElement, clientX: number, clientY: number,
 *         bounds: PieceBounds) => void}
 */
function updateFloatingPosition(el, clientX, clientY, bounds) {
    const piece_rect = getFloatingPieceRect(clientX, clientY, bounds);
    el.style.left = piece_rect.left + 'px';
    el.style.top = piece_rect.top + 'px';
}

// How far, in board squares, a piece may be pulled from where it is being held
// to reach a square it fits in. Nothing is pulled while the piece is over a
// square it fits in, so this is slack for the times it isn't: the piece
// overlapping a block by a corner, or hanging off the edge of the board. Enough
// to forgive a square's worth of aim, and short enough that the piece always
// lands somewhere you were pointing.
const SNAP_RADIUS_SQUARES = 1.5;

// The placement the drag is asking for: the one nearest to where the piece is
// drawn, which is the same square an exact reading would give whenever the
// piece fits there. Null when it is not being held near anywhere it fits.
/**
 * @type {(clientX: number, clientY: number, piece: Piece,
 *         bounds: PieceBounds) => Move | null}
 */
function calcShadowPlacement(clientX, clientY, piece, bounds) {
    const piece_rect = getFloatingPieceRect(clientX, clientY, bounds);
    const board = piece_rect.board;
    return blokie.placeNearest(
        state.game_state.game,
        piece,
        (piece_rect.top - board.rect.top) / board.cellH,
        (piece_rect.left - board.rect.left) / board.cellW,
        SNAP_RADIUS_SQUARES,
    );
}

// Return every occupied square which would disappear if this placement were
// committed. Asking the engine for the prospective game keeps the preview in
// lockstep with the actual row, column and box clearing rules.
/** @type {(piece: Piece, placement: Placement) => BitBoard | null} */
function calcClearPreview(piece, placement) {
    const result = blokie.place(state.game_state.game, placement);
    if (result === null || !result.new_game.previous_move_was_clear) return null;

    const before_clear = bits.union(state.game_state.game.board, placement);
    let preview = bits.empty();
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (bits.at(before_clear, r, c) && !bits.at(result.new_game.board, r, c)) {
                preview = bits.toggle(preview, r, c);
            }
        }
    }
    return preview;
}

/** @type {(clientX: number, clientY: number) => void} */
function handleDragMove(clientX, clientY) {
    if (!drag_info) return;

    if (!drag_info.active) {
        const dx = clientX - drag_info.startX;
        const dy = clientY - drag_info.startY;
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) return;

        // Activate drag and pause AI. Stopping the assist first, since it puts
        // down whatever piece it had in the air, and this one is taking its
        // place.
        drag_info.active = true;
        stopAI();
        drag_floating_el = createFloatingPiece(drag_info.piece, drag_info.bounds);
        state.piece_in_hand_index = drag_info.pieceIndex;
        // Here rather than on mousedown: a tap that never crosses the
        // threshold is deliberately nothing, and should sound like nothing.
        playSfx('pickup');
    }

    // Created the moment the drag went active, either just above or on an
    // earlier move; an inactive drag has already returned by here.
    if (drag_floating_el === null) return;

    updateFloatingPosition(drag_floating_el, clientX, clientY, drag_info.bounds);

    const shadow = calcShadowPlacement(clientX, clientY, drag_info.piece, drag_info.bounds);
    state.drag_shadow = shadow === null ? null : shadow.placement;
    state.clear_preview = shadow === null
        ? null
        : calcClearPreview(drag_info.piece, shadow.placement);
    drag_floating_el.classList.toggle('clear-preview', state.clear_preview !== null);
}

/** @type {(clientX: number, clientY: number) => void} */
function handleDragEnd(clientX, clientY) {
    if (!drag_info) return;

    if (drag_info.active) {
        // If the assist modified the piece set while the drag was starting
        // (between mousedown and the drag threshold), the piece we captured
        // may no longer be in its original slot. Cancel the drag in that case.
        if (state.game_state.piece_set[drag_info.pieceIndex] !== drag_info.piece) {
            cleanupDrag();
            onGameStateChanged();
            return;
        }

        // Final position update
        handleDragMove(clientX, clientY);

        // The shadow is what the piece was promised, so it is what gets played,
        // rather than anything worked out again from where the finger ended up.
        if (state.drag_shadow) {
            const result = blokie.place(state.game_state.game, state.drag_shadow);
            if (result) {
                commitMove(drag_info.pieceIndex, result);
                cleanupDrag();
                onGameStateChanged({ after_manual_move: true });
                return;
            }
        }
        // Every lift that doesn't become a move ends the same way, whether the
        // piece was refused by the board or taken back to the hand. The pickup
        // has already sounded by this point, so anything else leaves it hanging.
        playSfx('reject');
        cleanupDrag();
        // The player still took control long enough to stop the assist. Give
        // them the same full pacing interval when they return the piece to
        // the hand (or reject a drop) as when they complete a move.
        onGameStateChanged({ after_manual_move: true });
    } else {
        // Drag never activated - a tap on a piece does nothing.
        cleanupDrag();
    }
}

function cleanupDrag() {
    if (drag_floating_el) {
        drag_floating_el.remove();
        drag_floating_el = null;
    }
    drag_info = null;
    state.drag_shadow = null;
    state.clear_preview = null;
    state.piece_in_hand_index = -1;
}

// === End drag and drop ===

async function onNewGame() {
    clearRestartConfirm();
    // Whichever of the two started it, the menu has no more business open: the
    // board behind it is the thing to look at now.
    closeSettingsMenu();
    state.game_state = getNewGameState();
    pending_new_hand = true;
    onGameStateChanged();
}

const RESTART_CONFIRM_MS = 3000;
/** @type {ReturnType<typeof setTimeout> | null} */
let restart_confirm_timer = null;
// Replaced once the menu holds them; no-ops until then, since the first game is
// started before anything has been opened.
let clearRestartConfirm = () => { };
let closeSettingsMenu = () => { };

// Starting over throws a game away, and the row above it in the menu is one
// people open the menu to reach, so mid-game it asks before doing it. The
// question withdraws itself if it goes unanswered, and the menu stays up to be
// answered, the same way it does while sound is being toggled. Once the game is
// over there is nothing left to lose and the press goes straight through.
/** @type {(button: HTMLElement) => void} */
function initRestartButton(button) {
    const label = element('restart-label');
    const asked = label.innerText;

    /** @type {(text: string, title: string) => void} */
    const setLabel = (text, title) => {
        label.innerText = text;
        button.title = title;
    };

    clearRestartConfirm = () => {
        if (restart_confirm_timer === null) return;
        clearTimeout(restart_confirm_timer);
        restart_confirm_timer = null;
        setLabel(asked, 'Start a new game');
    };

    button.addEventListener('click', () => {
        if (restart_confirm_timer !== null || !gameIsActive()) {
            onNewGame();
            return;
        }
        setLabel('Start over?', 'Start a new game? Press again to confirm.');
        restart_confirm_timer = setTimeout(clearRestartConfirm, RESTART_CONFIRM_MS);
    });
}

// A piece lands on the board. The only place the board, the hand, the score and
// the sounds move on, whoever put the piece there: a drop ends here, and so
// does the end of the assist's fly animation. `result` is what
// blokie.place gave back for the move.
//
// `silent` is for the assist at Max, where the moves land faster than the
// sounds could be heard as anything but noise.
/**
 * @type {(piece_index: number, result: Move,
 *         options?: {silent?: boolean}) => void}
 */
function commitMove(piece_index, result, { silent = false } = {}) {
    if (!silent) {
        playSfx('place');
        if (result.new_game.previous_move_was_clear) {
            playSfx('clear');
        }
    }

    const game_state = state.game_state;
    // A move that clears carries the run on; one that does not ends it.
    const clear_streak = result.new_game.previous_move_was_clear
        ? game_state.clear_streak + 1
        : 0;
    // What the move paid, read before the new game replaces the old one below.
    // Held to the same bar the sounds are: at Max the moves land faster than a
    // card could be read, and the cards would only stack up over the board.
    if (!silent && result.new_game.previous_move_was_clear) {
        const combo = blokie.comboMagnitude(game_state.game.board, result.placement);
        const bonuses = [];
        if (combo > 1) {
            bonuses.push(combo.toLocaleString() + 'x Combo!');
        }
        // Like the combo, the first one is not a streak yet: it takes a second
        // clear in a row for the run to be worth naming, and from there the
        // number says how long it has been going.
        if (clear_streak > 1) {
            bonuses.push(clear_streak.toLocaleString() + 'x Streak!');
        }
        showMoveScoreCard(result.new_game.score - game_state.game.score, result.placement, bonuses);
    }
    // The engine returns the board after completed rows, columns and boxes have
    // already been removed. Remember this placement until the next render so
    // squares which were placed into a clear can join the shrink animation
    // instead of appearing never to have landed.
    pending_clear_placement = result.new_game.previous_move_was_clear
        ? result.placement
        : null;
    game_state.piece_set[piece_index] = bits.empty();
    if (game_state.piece_set.every(p => bits.isEmpty(p))) {
        game_state.piece_set = blokie.deal();
        assist_hand_is_new = true;
        // Held to the same bar the sounds and the card above are: at Max the
        // hands come out faster than a fade could read as anything but flicker.
        pending_new_hand = !silent;
    }
    game_state.game = result.new_game;
    game_state.clear_streak = clear_streak;
    refreshGameOver(game_state);
}

// A one-render bridge between the engine's already-cleared board and the DOM.
// This deliberately stays outside saved state: restoring a game should not
// replay the last move's animation.
/** @type {Placement | null} */
let pending_clear_placement = null;

// The same kind of bridge for a set of pieces that has just been dealt, spent
// by the render that first draws it. Outside saved state for the same reason:
// the hand a game is picked back up with is the one it was left with, not a
// new one, and it should be there the moment the page is.
let pending_new_hand = false;

/** @returns {HTMLSelectElement} */
function assistPicker() {
    return /** @type {HTMLSelectElement} */ (element('ai-assist'));
}

function assistIsOn() {
    return assistPicker().value !== 'off';
}

// The delay between assist moves, or null while you are playing by hand.
function getAssistDelayMs() {
    return assistIsOn() ? parseInt(assistPicker().value) : null;
}

// Tint the picker while the assist is the one playing.
function showWhoIsPlaying() {
    assistPicker().classList.toggle('assist-on', assistIsOn());
}

// Whether the assist is playing slowly enough to animate a move, and so to
// sound it. At Max the moves land faster than either is worth doing.
function assistShowsMoves() {
    const delay_ms = getAssistDelayMs();
    return delay_ms !== null && delay_ms >= FLY_ANIM_MS;
}

// The game ends when nothing in hand fits anywhere, whoever is placing.
/** @type {(game_state: GameState) => GameState} */
function refreshGameOver(game_state) {
    game_state.game_over = !blokie.hasValidMove(game_state.game.board, game_state.piece_set);
    return game_state;
}

// The worker plans; everything below plays what it comes back with. It is kept
// alive across plans so the solver is only instantiated once, and thrown away
// whenever the game moves on without it.
/** @type {Worker | null} */
let ai_worker = null;

// What the assist has left to play of the hand the worker last looked at, as
// `{ piece_index, placement }` in the order it wants to play them.
/** @type {import('../engine/js/blokie.js').PlannedMove[]} */
let assist_plan = [];
/** @type {ReturnType<typeof setTimeout> | null} */
let assist_move_timer = null;   // the next move
/** @type {ReturnType<typeof setTimeout> | null} */
let assist_fly_timer = null;    // the piece currently in the air landing
let assist_hand_is_new = false; // a fresh hand gets a beat to be looked at
let assist_is_starting = false; // the first move starts as soon as it is planned

function stopAI() {
    if (ai_worker != null) {
        ai_worker.terminate();
        ai_worker = null;
    }
    // Bumping the id makes any plan already in flight from the terminated
    // worker get ignored, so it can't be played onto a board that has moved on.
    state.assist_request_id++;
    if (assist_move_timer !== null) clearTimeout(assist_move_timer);
    assist_move_timer = null;
    if (assist_fly_timer !== null) clearTimeout(assist_fly_timer);
    assist_fly_timer = null;
    assist_plan = [];
    assist_hand_is_new = false;
    assist_is_starting = false;
    cleanupFlyAnim();
    if (drag_info === null) {
        state.piece_in_hand_index = -1;
    }
}

// Called whenever the game moves on: a placement, a new game, or a change to
// the assist setting. The assist picks up from the new state, if it is on.
//
// `after_manual_move` marks a hand-off from an active manual drag, whether the
// piece was played or returned to the hand. Taking over the instant the piece
// lands reads as the move being snatched out of your hand, so there the assist
// waits out a full pacing interval like it does between its own moves.
function onGameStateChanged({ after_manual_move = false } = {}) {
    stopAI();
    refreshGameOver(state.game_state);

    if (!assistIsOn() || state.game_state.game_over) {
        return;
    }

    assist_is_starting = !after_manual_move;
    ai_worker = new Worker(new URL('./ai-worker.js', import.meta.url), { type: 'module' });
    ai_worker.onmessage = (e) => {
        if (e.data.id != state.assist_request_id) {
            return;
        }
        assist_plan = e.data.plan;
        continueAssist();
    };
    continueAssist();
}

function requestAssistPlan() {
    // Only called from continueAssist, which has already returned if there is
    // no worker to ask.
    if (ai_worker === null) return;
    ai_worker.postMessage({
        id: state.assist_request_id,
        game: state.game_state.game,
        piece_set: state.game_state.piece_set,
    });
}

// The assist's loop, re-entered after every move it makes and every plan that
// comes back. An empty plan means the worker found nowhere to put anything,
// which is the same thing refreshGameOver has already worked out.
function continueAssist() {
    if (ai_worker === null || !assistIsOn() || state.game_state.game_over) {
        return;
    }
    if (assist_plan.length === 0) {
        requestAssistPlan();
        return;
    }

    // At Max there is nothing to watch, so the whole plan goes down in one go
    // and the next one is asked for immediately. Pacing it with timers instead
    // would only be slower than the search it is already waiting on: browsers
    // clamp nested zero-delay timeouts to 4ms.
    if (!assistShowsMoves()) {
        while (assist_plan.length > 0 && !state.game_state.game_over) {
            const planned = assist_plan.shift();
            if (planned === undefined) break;
            const move = resolveAssistMove(planned);
            if (move === null) {
                onGameStateChanged();
                return;
            }
            commitMove(move.piece_index, move.result, { silent: true });
        }
        continueAssist();
        return;
    }

    // Starting the assist already spends time waiting for its first plan. Pick
    // the piece up as soon as that plan arrives rather than adding one full
    // pacing interval before anything happens on the board.
    if (assist_is_starting) {
        assist_is_starting = false;
        playNextAssistMove();
        return;
    }

    const delay_ms = getAssistDelayMs();
    // Not reached with the assist off, since the guard at the top of this
    // function has already returned by then. Checked all the same because the
    // picker is the only thing that says how long to wait, so there is nothing
    // to schedule without it.
    if (delay_ms === null) return;
    // A hand that just came out is worth a look before it starts being played.
    const wait_ms = assist_hand_is_new ? delay_ms * 2 : delay_ms;
    assist_hand_is_new = false;
    assist_move_timer = setTimeout(playNextAssistMove, wait_ms);
}

// Works out what a planned move does to the game as it stands now. Null means
// the plan is stale -- the hand moved under it -- and should be thrown away.
/**
 * @type {(move: import('../engine/js/blokie.js').PlannedMove)
 *     => {piece_index: number, piece: Piece, result: Move} | null}
 */
function resolveAssistMove(move) {
    const piece = state.game_state.piece_set[move.piece_index];
    const result = blokie.place(state.game_state.game, move.placement);
    if (result === null) {
        return null;
    }
    return { piece_index: move.piece_index, piece: piece, result: result };
}

// Shown at a watchable speed, an assist move takes the same three beats a drag
// does: the piece comes off the hand, travels, and lands. Only the landing
// commits it, so the board, the score and the hand all move at the moment the
// piece arrives, exactly as they do under a finger.
function playNextAssistMove() {
    assist_move_timer = null;
    const planned = assist_plan.shift();
    // An empty plan is the same situation as a stale one: ask for another.
    const move = planned === undefined ? null : resolveAssistMove(planned);
    if (move === null) {
        onGameStateChanged();
        return;
    }

    playSfx('pickup');
    state.piece_in_hand_index = move.piece_index;
    _fly_anim = startFlyAnimation(move.piece_index, move.piece, move.result.placement);
    assist_fly_timer = setTimeout(() => {
        assist_fly_timer = null;
        cleanupFlyAnim();
        state.piece_in_hand_index = -1;
        commitMove(move.piece_index, move.result);
        continueAssist();
    }, FLY_ANIM_MS);
}


let last_rendered_state_json = '';

// The assist's piece on its way from the hand to the board. Started and taken
// down by the assist driver above; nothing here decides when a move happens.
/** @type {{el: HTMLElement} | null} */
let _fly_anim = null;
const FLY_ANIM_MS = 300;

/**
 * @type {(pieceIndex: number, piece: Piece, placement: Placement)
 *     => {el: HTMLElement}}
 */
function startFlyAnimation(pieceIndex, piece, placement) {
    const bounds = bits.bounds(piece);
    const el = createFloatingPiece(piece, bounds);

    // Source: center of the in-hand slot
    const handTable = element('piece-in-hand-' + pieceIndex);
    const handRect = handTable.getBoundingClientRect();

    // Target: top-left of where the piece lands on the board
    const board = getBoardGeometry();

    // Find the top-left occupied cell of the placement
    let minR = 9, minC = 9;
    for (let r = 0; r < 9; r++) {
        for (let c = 0; c < 9; c++) {
            if (bits.at(placement, r, c)) {
                if (r < minR) minR = r;
                if (c < minC) minC = c;
            }
        }
    }

    const targetX = board.rect.left + minC * board.cellW;
    const targetY = board.rect.top + minR * board.cellH;

    // Start at the in-hand slot, centered
    const pieceW = bounds.cols * board.cellW;
    const pieceH = bounds.rows * board.cellH;
    const startX = handRect.left + (handRect.width - pieceW) / 2;
    const startY = handRect.top + (handRect.height - pieceH) / 2;

    el.style.left = startX + 'px';
    el.style.top = startY + 'px';
    el.style.transition = `left ${FLY_ANIM_MS}ms ease-in-out, top ${FLY_ANIM_MS}ms ease-in-out, opacity ${FLY_ANIM_MS}ms ease-in-out`;
    el.style.opacity = '0.8';

    // Force layout before setting target to trigger transition
    el.getBoundingClientRect();

    el.style.left = targetX + 'px';
    el.style.top = targetY + 'px';
    el.style.opacity = '1';

    return { el };
}

function cleanupFlyAnim() {
    if (_fly_anim) {
        _fly_anim.el.remove();
        _fly_anim = null;
    }
}

// === Score card ===

// Long enough for the card's animation in the stylesheet to have finished, so
// what comes off the page is only ever something already invisible on it. A
// timer rather than the animation's own end event, which a backgrounded tab may
// never get around to firing.
const SCORE_CARD_MS = 1450;

// How close to the side of the page a card is allowed to sit, once it has been
// pushed back on to it.
const SCORE_CARD_MARGIN_PX = 4;

// Where a card for this move belongs, in screen pixels: the middle of the
// squares the piece covered. Read off the board as it stands, the same way the
// drag and the assist's flight are.
/** @type {(placement: Placement) => {x: number, y: number}} */
function getPlacementCenter(placement) {
    const board = getBoardGeometry();
    let min_r = 9, min_c = 9, max_r = -1, max_c = -1;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (!bits.at(placement, r, c)) continue;
            if (r < min_r) min_r = r;
            if (c < min_c) min_c = c;
            if (r > max_r) max_r = r;
            if (c > max_c) max_c = c;
        }
    }
    return {
        x: board.rect.left + (min_c + max_c + 1) / 2 * board.cellW,
        // A piece landing in the top row would put the card over the score and
        // float it off the top of the page, so cards from up there start a
        // couple of squares down and stay on the board like the rest.
        y: Math.max(
            board.rect.top + 2 * board.cellH,
            board.rect.top + (min_r + max_r + 1) / 2 * board.cellH,
        ),
    };
}

// One card for the whole of what a move paid: the points across the top, and
// under them the bonuses that earned them, in the order they were earned. A
// combo and a streak are the reason the number is as big as it is, so they
// belong beside it rather than following it across the board as cards of their
// own.
/** @type {(points: number, placement: Placement, bonuses?: string[]) => void} */
function showMoveScoreCard(points, placement, bonuses = []) {
    const center = getPlacementCenter(placement);
    const el = document.createElement('div');
    el.className = 'score-card';
    const line = document.createElement('div');
    line.innerText = '+' + points.toLocaleString();
    el.appendChild(line);
    for (const bonus of bonuses) {
        const bonus_line = document.createElement('div');
        bonus_line.className = 'score-card-bonus';
        bonus_line.innerText = bonus;
        el.appendChild(bonus_line);
    }
    el.style.top = center.y + 'px';
    document.body.appendChild(el);
    // A card carrying bonuses is wider than the squares it came from, and one
    // centred on a move at the edge of the board would hang off the side of the
    // page with a word or two of it cut away, so a card that would overhang is
    // pushed back on. How wide it is depends on what is written on it, which is
    // why it is measured here rather than worked out with the rest of the
    // position: it has to be on the page to be measured. `left` is the middle
    // of the card, which the stylesheet pulls back by half its own width.
    const half_width = el.offsetWidth / 2;
    const page_width = document.documentElement.clientWidth;
    el.style.left = Math.min(
        Math.max(center.x, half_width + SCORE_CARD_MARGIN_PX),
        page_width - half_width - SCORE_CARD_MARGIN_PX,
    ) + 'px';
    setTimeout(() => el.remove(), SCORE_CARD_MS);
}

// The board is drawn from the game as it stands, and only when something about
// it has changed. Nothing here works out that a move happened: a piece in the
// air is a piece nobody has played yet, whether a finger or the assist is
// carrying it, and the state it lands in is the state that gets drawn.
function render() {
    const state_json = JSON.stringify(state);
    if (last_rendered_state_json !== state_json) {
        last_rendered_state_json = state_json;
        renderImpl();
        saveGame();
    }

    window.requestAnimationFrame(render);
}
window.requestAnimationFrame(render);

function renderImpl() {
    const board_table = /** @type {HTMLTableElement} */ (element('game-board'));
    const pieces_in_hand_div = element('pieces-in-hand-container');
    const game_state = state.game_state;

    showGameOver(!gameIsActive(), game_state.game.score);
    drawGame(
        board_table,
        pieces_in_hand_div,
        game_state.game.board,
        game_state.piece_set,
        pending_clear_placement,
    );
    pending_clear_placement = null;
    // After the pieces are in the cells, so the first frame of the fade is
    // already the new set rather than the empty hand it replaced.
    if (pending_new_hand) {
        pending_new_hand = false;
        fadeInHand(pieces_in_hand_div);
    }
    updateScore(game_state.game.score);
}

// A set is dealt as one event, so it comes up as one: the three slots fade in
// together. This is the only thing that fades a piece in hand -- the squares
// there are deliberately left out of the board's per-square fade -- so a set
// which lands on top of the one before it comes up evenly, rather than at two
// brightnesses depending on which squares the last set happened to be using.
//
// Run from here rather than a class in the stylesheet because it is the same
// three tables every time, and starting an animation on an element restarts
// it, where re-adding a class it may still be wearing would do nothing.
//
// The tables and not the hand around them, so the fade multiplies with the
// wash the container carries once the game is over instead of overriding it.
//
// Long enough to be watched rather than just noticed, and still well inside
// the beat the assist gives a new hand at its slowest animated speed.
const HAND_FADE_MS = 300;

/** @type {(pieces_in_hand_div: HTMLElement) => void} */
function fadeInHand(pieces_in_hand_div) {
    for (const table of pieces_in_hand_div.children) {
        table.animate([{ opacity: 0 }, { opacity: 1 }], {
            duration: HAND_FADE_MS,
            easing: 'ease-out',
        });
    }
}

// The score line carries the number and nothing else, at every point in the
// game. Saying "final" up there as well used to be the whole of the game-over
// signal; the card says it now, and on a narrow phone the longer line wrapped
// and shoved the board down at the worst possible moment.
/** @type {(score: number) => void} */
function updateScore(score) {
    const score_el = element('score');
    score_el.innerText = score.toLocaleString();
}

let game_over_shown = false;

// The card over the board, the wash behind it and the drained pieces below are
// one signal, raised and lowered together.
/** @type {(over: boolean, score: number) => void} */
function showGameOver(over, score) {
    if (over === game_over_shown) return;
    game_over_shown = over;

    const panel = element('game-over');
    document.body.classList.toggle('game-over', over);
    if (!over) {
        panel.hidden = true;
        return;
    }

    // Revealed before it is filled in: a hidden panel is not in the
    // accessibility tree, and a live region that isn't there announces nothing.
    panel.hidden = false;
    element('final-score').innerText = score.toLocaleString();
    // Puts the way out under the keyboard, and names it for a screen reader.
    element('new-game').focus({ preventScroll: true });
}

/** @type {(td: HTMLElement, cls: string) => void} */
function _setCell(td, cls) {
    const old = td.className;
    if (old === cls) return;
    if (cls === '' && old.startsWith('shrinking-')) return; // let shrink finish

    if ((cls === '' && old === 'has-piece') || cls === 'shrinking-piece') {
        td.className = 'shrinking-piece';
        td.addEventListener('animationend', () => {
            if (td.className.startsWith('shrinking-')) td.className = '';
        }, { once: true });
        return;
    }

    td.className = cls;
}

/**
 * @type {(board_table: HTMLTableElement, pieces_in_hand_div: HTMLElement,
 *         board: BitBoard, piece_set: Hand,
 *         clearing_placement: Placement | null) => void}
 */
function drawGame(board_table, pieces_in_hand_div, board, piece_set, clearing_placement) {
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            const td = board_table.rows[r].cells[c];
            let cls;
            // The landing cells remain the ordinary grey drag shadow even
            // when they participate in a clear. Only squares already on the
            // board get the lighter clear treatment.
            if (state.drag_shadow && bits.at(state.drag_shadow, r, c)) {
                cls = 'drag-shadow';
            } else if (state.clear_preview && bits.at(state.clear_preview, r, c)) {
                cls = 'clear-preview';
            } else if (bits.at(board, r, c)) {
                cls = 'has-piece';
            } else if (clearing_placement && bits.at(clearing_placement, r, c)) {
                // This square was both added and cleared in the same engine
                // update, so it never existed in `board`. Draw it directly in
                // the same outgoing state as the older squares being cleared.
                cls = 'shrinking-piece';
            } else {
                cls = '';
            }
            _setCell(td, cls);
        }
    }

    for (let i = 0; i < 3; ++i) {
        const hidePiece = state.piece_in_hand_index === i;
        // The engine deals pieces justified into the top left corner. Where one
        // sits inside the 5x5 slot it is drawn in is a question about drawing
        // it, so it is answered here rather than carried around in the hand.
        // Centering justifies first, so a hand restored from an older cookie --
        // which saved its pieces already centered -- comes up in the same place.
        const piece = bits.center(piece_set[i]);
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const hand_table = /** @type {HTMLTableElement} */ (
                    pieces_in_hand_div.children[i]);
                const td = hand_table.rows[r].cells[c];
                const cls = (!hidePiece && bits.at(piece, r, c)) ? 'has-piece' : '';
                if (td.className !== cls) td.className = cls;
            }
        }
    }
}
