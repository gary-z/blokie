"use strict";
import { blokie, init } from "../engine/blokie.js";
import { saveGameState, loadGameState, saveAssistSetting, loadAssistSetting } from "./storage.js";
import { initSfx, playSfx } from "./sfx.js";


// You play the game by dragging pieces onto the board. The AI assist can play
// for you instead, at a speed chosen in the bottom bar, until you switch it off
// or take a turn yourself.
function getNewGameState() {
    return {
        previous_game_state: blokie.getNewGame(),
        game: blokie.getNewGame(),
        queued_game_states: [],
        piece_set: blokie.getRandomPieceSet(),
        game_over: false,  // true once nothing on deck fits anywhere
    };
}

let state = {
    game_state: getNewGameState(),

    // UI state
    active_worker_id: 0,

    // Drag rendering state (in state so JSON.stringify change detection triggers re-render)
    drag_shadow: null,         // bitboard or null - shadow cells on board
    dragging_piece_index: -1,  // which piece deck slot is being dragged (-1 = none)
};

// Drag state kept outside `state` (contains DOM refs, not serializable)
let drag_info = null;       // { pieceIndex, piece, bounds, startX, startY, active, targetRow, targetCol }
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
    const ai_assist = document.getElementById('ai-assist');
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
    initSfx(document.getElementById('sound'));

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

    // The board itself is not interactive: pieces land on it by drag, and the
    // only click it answers is the one that starts a new game after a loss.
    document.getElementById('game-board').addEventListener("click", () => {
        if (!gameIsActive()) {
            onNewGame();
        }
    });

    const pieces_on_deck_container = document.getElementById('pieces-on-deck-container');
    pieces_on_deck_container.addEventListener('touchstart', (event) => {
        if (!gameIsActive()) return;
        const cell = event.target;
        if (cell.nodeName !== 'TD') return;
        const table = cell.closest('table');
        if (table.className !== 'pieces-on-deck') return;

        const pieceIndex = parseInt(table.id.slice(-1));
        const piece = state.game_state.piece_set[pieceIndex];
        if (blokie.isEmpty(piece)) return;

        const touch = event.touches[0];
        drag_info = {
            pieceIndex,
            piece,
            bounds: blokie.getPieceBounds(piece),
            startX: touch.clientX,
            startY: touch.clientY,
            active: false,
            targetRow: -1,
            targetCol: -1,
        };
        event.preventDefault();
    });
    pieces_on_deck_container.addEventListener('mousedown', (event) => {
        if (!gameIsActive()) return;
        const cell = event.target;
        if (cell.nodeName !== 'TD') return;
        const table = cell.closest('table');
        if (table.className !== 'pieces-on-deck') return;

        const pieceIndex = parseInt(table.id.slice(-1));
        const piece = state.game_state.piece_set[pieceIndex];
        if (blokie.isEmpty(piece)) return;

        drag_info = {
            pieceIndex,
            piece,
            bounds: blokie.getPieceBounds(piece),
            startX: event.clientX,
            startY: event.clientY,
            active: false,
            targetRow: -1,
            targetCol: -1,
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
    const settings = document.getElementById('settings');
    const button = document.getElementById('settings-button');
    const menu = document.getElementById('settings-menu');

    function setMenuOpen(open) {
        menu.hidden = !open;
        button.setAttribute('aria-expanded', String(open));
    }

    button.addEventListener('click', () => setMenuOpen(menu.hidden));

    // Toggling sound leaves the menu up, so the button can be seen changing
    // and heard again; following the link out is done with it.
    document.addEventListener('click', (event) => {
        if (!menu.hidden && !settings.contains(event.target)) {
            setMenuOpen(false);
        }
    });
    menu.querySelector('a').addEventListener('click', () => setMenuOpen(false));

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && !menu.hidden) {
            setMenuOpen(false);
            button.focus();
        }
    });
}

// === Drag and drop ===

function createFloatingPiece(piece, bounds) {
    const boardTable = document.getElementById('game-board');
    const cellRect = boardTable.rows[0].cells[0].getBoundingClientRect();

    const el = document.createElement('div');
    el.style.position = 'fixed';
    el.style.pointerEvents = 'none';
    el.style.zIndex = '1000';
    el.style.opacity = '0.8';

    const table = document.createElement('table');
    table.style.borderCollapse = 'collapse';

    const p = blokie.leftTopJustify(piece);
    for (let r = 0; r < bounds.rows; r++) {
        const tr = document.createElement('tr');
        for (let c = 0; c < bounds.cols; c++) {
            const td = document.createElement('td');
            td.style.width = cellRect.width + 'px';
            td.style.height = cellRect.height + 'px';
            td.style.padding = '0';
            td.style.border = '0';
            if (blokie.at(p, r, c)) {
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

function updateFloatingPosition(el, clientX, clientY, bounds) {
    const boardTable = document.getElementById('game-board');
    const cellRect = boardTable.rows[0].cells[0].getBoundingClientRect();
    const pieceW = bounds.cols * cellRect.width;
    const pieceH = bounds.rows * cellRect.height;
    el.style.left = (clientX - pieceW / 2) + 'px';
    el.style.top = (clientY - FINGER_CLEARANCE - pieceH) + 'px';
}

function calcShadowPlacement(clientX, clientY, piece, bounds) {
    const boardTable = document.getElementById('game-board');
    const boardRect = boardTable.getBoundingClientRect();
    const cellW = boardRect.width / 9;
    const cellH = boardRect.height / 9;

    // Center of the floating piece (bottom edge sits FINGER_CLEARANCE above touch)
    const centerX = clientX;
    const pieceH = bounds.rows * cellH;
    const centerY = clientY - FINGER_CLEARANCE - pieceH / 2;

    // Find where the floating piece's top-left cell center falls on the board,
    // matching the continuous centering used by updateFloatingPosition.
    const targetCol = Math.floor((centerX - boardRect.left) / cellW - (bounds.cols - 1) / 2);
    const targetRow = Math.floor((centerY - boardRect.top) / cellH - (bounds.rows - 1) / 2);

    const result = blokie.tryPlacePiece(state.game_state.game, piece, targetRow, targetCol);
    if (!result) return null;
    return { placement: result.placement, targetRow, targetCol };
}

// Whether the piece was over the board when it was let go, using the same
// geometry updateFloatingPosition draws it with. Dropping it anywhere else is
// a change of mind rather than a rejected placement, and buzzing at that would
// punish taking the piece back.
function droppedOverBoard(clientX, clientY, bounds) {
    const boardRect = document.getElementById('game-board').getBoundingClientRect();
    const pieceW = (boardRect.width / 9) * bounds.cols;
    const pieceH = (boardRect.height / 9) * bounds.rows;
    const left = clientX - pieceW / 2;
    const top = clientY - FINGER_CLEARANCE - pieceH;
    return left < boardRect.right && left + pieceW > boardRect.left
        && top < boardRect.bottom && top + pieceH > boardRect.top;
}

function handleDragMove(clientX, clientY) {
    if (!drag_info) return;

    if (!drag_info.active) {
        const dx = clientX - drag_info.startX;
        const dy = clientY - drag_info.startY;
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) return;

        // Activate drag and pause AI
        drag_info.active = true;
        drag_floating_el = createFloatingPiece(drag_info.piece, drag_info.bounds);
        state.dragging_piece_index = drag_info.pieceIndex;
        stopAI();
        // Here rather than on mousedown: a tap that never crosses the
        // threshold is deliberately nothing, and should sound like nothing.
        playSfx('pickup');
    }

    updateFloatingPosition(drag_floating_el, clientX, clientY, drag_info.bounds);

    const shadow = calcShadowPlacement(clientX, clientY, drag_info.piece, drag_info.bounds);
    if (shadow) {
        state.drag_shadow = shadow.placement;
        drag_info.targetRow = shadow.targetRow;
        drag_info.targetCol = shadow.targetCol;
    } else {
        state.drag_shadow = null;
        drag_info.targetRow = -1;
        drag_info.targetCol = -1;
    }
}

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

        if (state.drag_shadow && drag_info.targetRow >= 0) {
            const result = blokie.tryPlacePiece(
                state.game_state.game,
                drag_info.piece,
                drag_info.targetRow,
                drag_info.targetCol
            );
            if (result) {
                playSfx('place');
                if (result.newGame.previous_move_was_clear) {
                    playSfx('clear');
                }
                state.game_state.piece_set[drag_info.pieceIndex] = blokie.getEmptyPiece();
                if (state.game_state.piece_set.every(p => blokie.isEmpty(p))) {
                    state.game_state.piece_set = blokie.getRandomPieceSet();
                }
                state.game_state.previous_game_state = state.game_state.game;
                state.game_state.game = result.newGame;
                cleanupDrag();
                onGameStateChanged();
                return;
            }
        }
        if (droppedOverBoard(clientX, clientY, drag_info.bounds)) {
            playSfx('reject');
        }
        cleanupDrag();
        onGameStateChanged();
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
    state.dragging_piece_index = -1;
}

// === End drag and drop ===

async function onNewGame() {
    state.game_state = getNewGameState();
    onGameStateChanged();
}

let ai_worker = null;

function assistIsOn() {
    return document.getElementById('ai-assist').value !== 'off';
}

// The delay between assist moves, or null while you are playing by hand.
function getAssistDelayMs() {
    return assistIsOn() ? parseInt(document.getElementById('ai-assist').value) : null;
}

// Tint the picker while the assist is the one playing.
function showWhoIsPlaying() {
    document.getElementById('ai-assist').classList.toggle('assist-on', assistIsOn());
}

// Whether the assist is playing slowly enough to animate a move, and so to
// sound it. At Max the moves land faster than either is worth doing.
function assistShowsMoves() {
    const delay_ms = getAssistDelayMs();
    return delay_ms !== null && delay_ms >= FLY_ANIM_MS;
}

// The game ends when nothing on deck fits anywhere, whoever is placing.
function refreshGameOver(game_state) {
    game_state.game_over = !blokie.hasValidMove(game_state.game.board, game_state.piece_set);
    return game_state;
}

function sameBoard(a, b) {
    return a.board.a === b.board.a && a.board.b === b.board.b && a.board.c === b.board.c;
}

function stopAI() {
    if (ai_worker != null) {
        ai_worker.terminate();
        ai_worker = null;
    }
    // Bumping the id makes any message already in flight from the terminated
    // worker get ignored, so it can't clobber the state we just changed.
    state.active_worker_id++;
    state.game_state.queued_game_states = [];
    cleanupFlyAnim();
    _prev_preview_json = null;
    _fly_landed = false;
}

// Called whenever the game moves on: a placement, a new game, or a change to
// the assist setting. The assist picks up from the new state, if it is on.
function onGameStateChanged() {
    stopAI();
    refreshGameOver(state.game_state);

    const delay_ms = getAssistDelayMs();
    if (delay_ms === null || state.game_state.game_over) {
        return;
    }

    ai_worker = new Worker(new URL('./ai-worker.js', import.meta.url), { type: 'module' });
    ai_worker.postMessage({
        delay_ms: delay_ms,
        game_state: state.game_state,
        id: state.active_worker_id,
    });
    ai_worker.onmessage = (e) => {
        if (e.data.id != state.active_worker_id) {
            return;
        }
        const game_state = e.data.game_state;
        // The solver only plans moves that place all three pieces, and reports
        // a full board when it can't. That is the assist running out of ideas,
        // not the end of the game: the player may still have somewhere to put
        // one of these pieces, so drop the sentinel and let them finish.
        if (game_state.queued_game_states.length > 0 && blokie.isOver(game_state.queued_game_states[0])) {
            game_state.queued_game_states = [];
        }
        // The assist posts on every tick, including ones where it only planned
        // ahead. A move actually landed when the board it reports differs from
        // the one already on screen, and that is when the cells shrink out.
        if (assistShowsMoves() && game_state.game.previous_move_was_clear
            && !sameBoard(state.game_state.game, game_state.game)) {
            playSfx('clear');
        }
        state.game_state = refreshGameOver(game_state);
    };
}


let last_rendered_state_json = '';

let _fly_anim = null; // { el, startTime }
const FLY_ANIM_MS = 300;
let _prev_preview_json = null; // tracks queued_game_states[0] to detect new previews
let _fly_landed = false; // true once fly animation finishes for current preview

function startFlyAnimation(pieceIndex, piece, placement) {
    const bounds = blokie.getPieceBounds(piece);
    const el = createFloatingPiece(piece, bounds);

    // Source: center of the on-deck slot
    const deckTable = document.getElementById('piece-on-deck-' + pieceIndex);
    const deckRect = deckTable.getBoundingClientRect();

    // Target: top-left of where the piece lands on the board
    const boardTable = document.getElementById('game-board');
    const boardRect = boardTable.getBoundingClientRect();
    const cellW = boardRect.width / 9;
    const cellH = boardRect.height / 9;

    // Find the top-left occupied cell of the placement
    let minR = 9, minC = 9;
    for (let r = 0; r < 9; r++) {
        for (let c = 0; c < 9; c++) {
            if (blokie.at(placement, r, c)) {
                if (r < minR) minR = r;
                if (c < minC) minC = c;
            }
        }
    }

    const targetX = boardRect.left + minC * cellW;
    const targetY = boardRect.top + minR * cellH;

    // Start at the on-deck slot, centered
    const pieceW = bounds.cols * cellW;
    const pieceH = bounds.rows * cellH;
    const startX = deckRect.left + (deckRect.width - pieceW) / 2;
    const startY = deckRect.top + (deckRect.height - pieceH) / 2;

    el.style.left = startX + 'px';
    el.style.top = startY + 'px';
    el.style.transition = `left ${FLY_ANIM_MS}ms ease-in-out, top ${FLY_ANIM_MS}ms ease-in-out, opacity ${FLY_ANIM_MS}ms ease-in-out`;
    el.style.opacity = '0.8';

    // Force layout before setting target to trigger transition
    el.getBoundingClientRect();

    el.style.left = targetX + 'px';
    el.style.top = targetY + 'px';
    el.style.opacity = '1';

    return {
        el,
        startTime: performance.now(),
    };
}

function cleanupFlyAnim() {
    if (_fly_anim) {
        _fly_anim.el.remove();
        _fly_anim = null;
    }
}

function render() {
    const now = performance.now();
    const state_json = JSON.stringify(state);
    const stateChanged = last_rendered_state_json !== state_json;

    // Clean up finished fly animation and force re-render so blue cells appear immediately
    let flyJustLanded = false;
    if (_fly_anim && (now - _fly_anim.startTime >= FLY_ANIM_MS)) {
        cleanupFlyAnim();
        _fly_landed = true;
        flyJustLanded = true;
        playSfx('place');  // the assist's piece has arrived on the board
    }

    // Detect new preview (queued move shown) and start fly animation.
    // This fires when the red highlight first appears, so the piece flies immediately.
    const gs = state.game_state;
    const nextQueued = gs.queued_game_states.length > 0 ? gs.queued_game_states[0] : null;
    const previewJson = nextQueued ? JSON.stringify(nextQueued.previous_piece_placement) : null;

    if (previewJson && previewJson !== _prev_preview_json && !drag_info && assistShowsMoves()) {
        const pieceIndex = gs.piece_set.findIndex(p => p === nextQueued.previous_piece);
        if (pieceIndex >= 0) {
            cleanupFlyAnim();
            _fly_landed = false;
            _fly_anim = startFlyAnimation(pieceIndex, nextQueued.previous_piece, nextQueued.previous_piece_placement);
            playSfx('pickup');  // the assist has taken a piece off the deck
        }
    }
    _prev_preview_json = previewJson;

    if (stateChanged || flyJustLanded) {
        last_rendered_state_json = state_json;
        renderImpl();
        // Only the committed game is saved, never a move the assist has merely
        // lined up, and only when it differs from what is already in the cookie.
        saveGame();
    }

    window.requestAnimationFrame(render);
}
window.requestAnimationFrame(render);

function renderImpl() {
    let board_table = document.getElementById('game-board');
    let pieces_on_deck_div = document.getElementById('pieces-on-deck-container');

    if (!gameIsActive()) {
        drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, state.game_state.piece_set);
        updateScore("Final score: " + state.game_state.game.score.toString());
        return;
    }

    if (state.game_state.queued_game_states.length === 0) {
        drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, state.game_state.piece_set);
        updateScore(state.game_state.game.score);
        return;
    }

    // The assist has a move lined up: show the piece leaving the deck.
    const next_game_state = state.game_state.queued_game_states[0];
    updateScore(next_game_state.score);
    const piece_set_to_render = state.game_state.piece_set.map(p => p === next_game_state.previous_piece ? blokie.getEmptyPiece() : p);
    if (_fly_landed) {
        // Fly completed — show destination cells as blue (part of the board)
        const boardWithPiece = blokie.or(state.game_state.game.board, next_game_state.previous_piece_placement);
        drawGame(board_table, pieces_on_deck_div, boardWithPiece, piece_set_to_render);
    } else {
        // Fly in progress or no fly — don't highlight destination
        drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, piece_set_to_render);
    }
}

function updateScore(score) {
    const score_el = document.getElementById('score');
    score_el.innerText = score;
}

function _setCell(td, cls) {
    const old = td.className;
    if (old === cls) return;
    if (cls === '' && old.startsWith('shrinking-')) return; // let shrink finish

    if (cls === '' && old === 'has-piece') {
        td.className = 'shrinking-piece';
        td.addEventListener('animationend', () => {
            if (td.className.startsWith('shrinking-')) td.className = '';
        }, { once: true });
        return;
    }

    td.className = cls;
}

function drawGame(board_table, pieces_on_deck_div, board, piece_set) {
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            const td = board_table.rows[r].cells[c];
            let cls;
            if (blokie.at(board, r, c)) {
                cls = 'has-piece';
            } else if (state.drag_shadow && blokie.at(state.drag_shadow, r, c)) {
                cls = 'drag-shadow';
            } else {
                cls = '';
            }
            _setCell(td, cls);
        }
    }

    for (let i = 0; i < 3; ++i) {
        const hidePiece = state.dragging_piece_index === i;
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const td = pieces_on_deck_div.children[i].rows[r].cells[c];
                const cls = (!hidePiece && blokie.at(piece_set[i], r, c)) ? 'has-piece' : '';
                if (td.className !== cls) td.className = cls;
            }
        }
    }
}
