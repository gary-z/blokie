"use strict";
import { blokie } from "./blokie.js";


function getNewGameState() {
    return {
        previous_game_state: blokie.getNewGame(),
        game: blokie.getNewGame(),
        queued_game_states: [],
        piece_set: blokie.getRandomPieceSet(),
    };
}

let state = {
    game_state: getNewGameState(),

    // UI state
    mouse_down: false,
    last_dragged_board_cell: null,
    active_worker_id: 0,

    // Drag rendering state (in state so JSON.stringify change detection triggers re-render)
    drag_shadow: null,         // bitboard or null - shadow cells on board
    dragging_piece_index: -1,  // which piece deck slot is being dragged (-1 = none)
};

// Drag state kept outside `state` (contains DOM refs, not serializable)
let drag_info = null;       // { pieceIndex, piece, bounds, startX, startY, active, targetRow, targetCol, pendingCell }
let drag_floating_el = null;

const DRAG_THRESHOLD = 8;
const DRAG_OFFSET_Y = 60;

document.addEventListener("DOMContentLoaded", function (event) {
    const slider = document.getElementById('speed');
    slider.addEventListener("input", (event) => {
        resetAIOnHumanInterferance();
    });

    document.addEventListener('mouseup', (event) => {
        handleDragEnd(event.clientX, event.clientY);
        state.last_dragged_board_cell = null;
        state.mouse_down = false;
    });
    document.addEventListener('touchend', (event) => {
        if (drag_info) {
            const touch = event.changedTouches[0];
            handleDragEnd(touch.clientX, touch.clientY);
        }
        state.mouse_down = false;
        state.last_dragged_board_cell = null;
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

    var board_table = document.getElementById('game-board');
    board_table.addEventListener("click", () => {
        if (!gameIsActive()) {
            onNewGame();
        }
    });
    board_table.addEventListener("mouseover", (event) => {
        if (state.mouse_down && !drag_info) {
            onBoardCellClick(event.target);
        }
    });
    board_table.addEventListener("touchmove", (event) => {
        if (!drag_info) {
            processCellDrag(event, onBoardCellClick);
        }
    });
    board_table.addEventListener('mousedown', (event) => {
        if (!drag_info) {
            onBoardCellClick(event.target);
            state.mouse_down = true;
        }
    });
    board_table.addEventListener('touchstart', (event) => {
        if (!gameIsActive()) {
            return;
        }
        if (!drag_info) {
            onBoardCellClick(event.target);
            state.last_dragged_board_cell = event.target;
            state.mouse_down = true;
            event.preventDefault();
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

        if (!blokie.isEmpty(piece)) {
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
                pendingCell: cell,
            };
            event.preventDefault();
        } else {
            onPieceCellClick(cell);
            state.last_dragged_board_cell = cell;
            state.mouse_down = true;
            event.preventDefault();
        }
    });
    pieces_on_deck_container.addEventListener('touchmove', (event) => {
        if (!drag_info) {
            processCellDrag(event, onPieceCellClick);
        }
    });
    pieces_on_deck_container.addEventListener('mousedown', (event) => {
        if (!gameIsActive()) return;
        const cell = event.target;
        if (cell.nodeName !== 'TD') return;
        const table = cell.closest('table');
        if (table.className !== 'pieces-on-deck') return;

        const pieceIndex = parseInt(table.id.slice(-1));
        const piece = state.game_state.piece_set[pieceIndex];

        if (!blokie.isEmpty(piece)) {
            drag_info = {
                pieceIndex,
                piece,
                bounds: blokie.getPieceBounds(piece),
                startX: event.clientX,
                startY: event.clientY,
                active: false,
                targetRow: -1,
                targetCol: -1,
                pendingCell: cell,
            };
            state.mouse_down = true;
        } else {
            onPieceCellClick(cell);
            state.mouse_down = true;
        }
    });
    pieces_on_deck_container.addEventListener('mouseover', (event) => {
        if (state.mouse_down && !drag_info) {
            onPieceCellClick(event.target);
        }
    });
    onNewGame();
});

function gameIsActive() {
    return state.game_state.queued_game_states.length === 0 || !blokie.isOver(state.game_state.queued_game_states[0]);

}

function processCellDrag(event, call) {
    event.preventDefault();
    const location = event.touches[0];
    const cell = document.elementFromPoint(location.clientX, location.clientY);
    if (cell.nodeName !== 'TD') {
        return;
    }
    if (cell === state.last_dragged_board_cell) {
        return;
    }
    state.last_dragged_board_cell = cell;
    if (state.mouse_down) {
        call(cell);
    }
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
    el.style.top = (clientY - DRAG_OFFSET_Y - pieceH / 2) + 'px';
}

function calcShadowPlacement(clientX, clientY, piece, bounds) {
    const boardTable = document.getElementById('game-board');
    const boardRect = boardTable.getBoundingClientRect();
    const cellW = boardRect.width / 9;
    const cellH = boardRect.height / 9;

    // Center of the floating piece (offset above cursor)
    const centerX = clientX;
    const centerY = clientY - DRAG_OFFSET_Y;

    // Which board cell is the piece center over?
    const boardCol = Math.floor((centerX - boardRect.left) / cellW);
    const boardRow = Math.floor((centerY - boardRect.top) / cellH);

    // Position piece so it's centered on that cell
    const targetRow = boardRow - Math.floor((bounds.rows - 1) / 2);
    const targetCol = boardCol - Math.floor((bounds.cols - 1) / 2);

    const result = blokie.tryPlacePiece(state.game_state.game, piece, targetRow, targetCol);
    if (!result) return null;
    return { placement: result.placement, targetRow, targetCol };
}

function handleDragMove(clientX, clientY) {
    if (!drag_info) return;

    if (!drag_info.active) {
        const dx = clientX - drag_info.startX;
        const dy = clientY - drag_info.startY;
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) return;

        // Activate drag
        drag_info.active = true;
        drag_floating_el = createFloatingPiece(drag_info.piece, drag_info.bounds);
        state.dragging_piece_index = drag_info.pieceIndex;
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
                state.game_state.piece_set[drag_info.pieceIndex] = blokie.getEmptyPiece();
                state.game_state.previous_game_state = state.game_state.game;
                state.game_state.game = result.newGame;
                cleanupDrag();
                resetAIOnHumanInterferance();
                return;
            }
        }
        cleanupDrag();
    } else {
        // Drag never activated - treat as click (edit piece)
        const cell = drag_info.pendingCell;
        cleanupDrag();
        onPieceCellClick(cell);
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
    resetAIOnHumanInterferance();
}

function onBoardCellClick(cell) {
    if (!gameIsActive() || cell.nodeName !== 'TD') {
        return;
    }
    const table = cell.closest('table');
    if (table.id !== 'game-board') {
        return;
    }
    state.game_state.game.board = blokie.toggleSquare(state.game_state.game.board, cell.parentNode.rowIndex, cell.cellIndex);
    resetAIOnHumanInterferance();
}
function onPieceCellClick(cell) {
    if (!gameIsActive() || cell.nodeName !== 'TD') {
        return;
    }
    const table = cell.closest('table');
    if (table.className !== 'pieces-on-deck') {
        return;
    }

    const piece_table_id = parseInt(cell.closest('table').id.slice(-1));
    state.game_state.piece_set[piece_table_id] = blokie.toggleSquare(state.game_state.piece_set[piece_table_id], cell.parentNode.rowIndex, cell.cellIndex);
    resetAIOnHumanInterferance();
}

let ai_worker = null;

function resetAIOnHumanInterferance() {
    if (ai_worker != null) {
        ai_worker.terminate();
    }

    state.game_state.queued_game_states = [];
    state.active_worker_id++;
    ai_worker = new Worker('ai-worker.js', { type: 'module' });
    ai_worker.postMessage({
        delay_ms: getDelayMs(),
        game_state: state.game_state,
        id: state.active_worker_id,
    });
    ai_worker.onmessage = (e) => {
        if (e.data.id == state.active_worker_id) {
            state.game_state = e.data.game_state;
        }
    };
}


let last_rendered_state_json = '';
function render() {
    const state_json = JSON.stringify(state);
    if (last_rendered_state_json !== state_json) {
        renderImpl();
    }
    last_rendered_state_json = state_json;
    window.requestAnimationFrame(render);
}
window.requestAnimationFrame(render);

function renderImpl() {
    let board_table = document.getElementById('game-board');
    let pieces_on_deck_div = document.getElementById('pieces-on-deck-container');
    if (gameIsActive()) {
        if (state.game_state.queued_game_states.length === 0) {
            drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, blokie.getEmptyPiece(), state.game_state.piece_set);
            updateScore(state.game_state.game.score);
        } else {
            const next_game_state = state.game_state.queued_game_states[0];
            updateScore(next_game_state.score);
            const piece_set_to_render = state.game_state.piece_set.map(p => p === next_game_state.previous_piece ? blokie.getEmptyPiece() : p);
            drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, next_game_state.previous_piece_placement, piece_set_to_render);
        }
    } else {
        drawGame(board_table, pieces_on_deck_div, state.game_state.game.board, blokie.getEmptyPiece(), state.game_state.piece_set);
        updateScore("Final score: " + state.game_state.game.score.toString());
    }
}

// returns: true if should rerender at max speed
function aiPlayGame() {
    if (state.game_state.piece_set.every(p => blokie.isEmpty(p))) {
        state.game_state.piece_set = blokie.getRandomPieceSet();
        return true;
    }
    if (state.game_state.queued_game_states.length === 0) {
        if (state.game_state.piece_set.every(p => blokie.isEmpty(p))) {
            state.game_state.piece_set = blokie.getRandomPieceSet();
        }
        state.game_state.queued_game_states = blokie.getAIMove(state.game_state.game, state.game_state.piece_set).new_game_states;
        state.game_state.game.previous_piece_placement = blokie.getEmptyPiece();
        return false;
    }
    if (blokie.isOver(state.game_state.queued_game_states[0])) {
        return true;
    }

    const new_game_state = state.game_state.queued_game_states.shift();
    const piece_used = new_game_state.previous_piece;
    const used_piece_index = state.game_state.piece_set.indexOf(piece_used);
    if (used_piece_index >= 0) {
        state.game_state.piece_set[used_piece_index] = blokie.getEmptyPiece();
    }
    state.game_state.previous_game_state = state.game_state.game;
    state.game_state.game = new_game_state;
    return false;
}

function getSpeedSlider() {
    return document.getElementById('speed');
}

function getDelayMs() {
    const slider = getSpeedSlider();
    return slider.value == slider.max ? 0 : (2000.0 / 1.5 ** slider.value);
}

function updateScore(score) {
    const score_el = document.getElementById('score');
    score_el.innerText = score;
}

function drawGame(board_table, pieces_on_deck_div, board, placement, piece_set) {
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            const td = board_table.rows[r].cells[c];
            if (blokie.at(placement, r, c)) {
                td.className = 'piece-pending';
            } else if (blokie.at(board, r, c)) {
                td.className = 'has-piece';
            } else if (state.drag_shadow && blokie.at(state.drag_shadow, r, c)) {
                td.className = 'drag-shadow';
            } else {
                td.className = null;
            }
        }
    }

    for (let i = 0; i < 3; ++i) {
        const hidePiece = state.dragging_piece_index === i;
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const td = pieces_on_deck_div.children[i].rows[r].cells[c];
                td.className = (!hidePiece && blokie.at(piece_set[i], r, c)) ? 'has-piece' : null;
            }
        }
    }
}
