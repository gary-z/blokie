"use strict";
import { blokie } from "./blokie.js";

let state = {
    game_progress: 'OVER',
    previous_game_state: blokie.getNewGame(),
    game: blokie.getNewGame(),
    queued_game_states: [],
    piece_set: [],
    ai_interval_id: null,
    mouse_down: false,
    last_dragged_board_cell: null,
};

document.addEventListener("DOMContentLoaded", function (event) {
    onNewGame();
    const slider = document.getElementById('speed');
    slider.addEventListener("input", (event) => {
        cancelAIInterval();
        queueAIInterval();
    });

    var board_table = document.getElementById('game-board');
    board_table.addEventListener("click", () => {
        if (state.game_progress === 'OVER') {
            onNewGame();
        }
    });
    board_table.addEventListener("click", (event) => {
        onBoardCellClick(event.target);
    });
    board_table.addEventListener("mouseover", (event) => {
        if (state.mouse_down) {
            onBoardCellClick(event.target);
        }
    });
    board_table.addEventListener("touchmove", (event) => {
        processCellDrag(event, onBoardCellClick);
    });
    document.addEventListener('mousedown', () => {
        state.mouse_down = true;
    });
    document.addEventListener('touchstart', () => {
        state.mouse_down = true;
    });
    document.addEventListener('mouseup', () => {
        state.mouse_down = false;
    });
    document.addEventListener('touchend', () => {
        state.mouse_down = false;
        state.last_dragged_board_cell = null;
    });

    const pieces_on_deck_container = document.getElementById('pieces-on-deck-container');
    pieces_on_deck_container.addEventListener('click', (event) => {
        onPieceCellClick(event.target);
    });

    pieces_on_deck_container.addEventListener('mouseover', (event) => {
        if (state.mouse_down) {
            onPieceCellClick(event.target);
        }
    });
    pieces_on_deck_container.addEventListener('touchmove', (event) => {
        processCellDrag(event, onPieceCellClick);
    });
});

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

async function onNewGame() {
    state.game_progress = 'ACTIVE';
    state.queued_game_states = [];
    state.game = blokie.getNewGame(),
        state.previous_game_state = blokie.getNewGame(),
        state.piece_set = blokie.getRandomPieceSet();
    render();
    queueAIInterval();
}

function queueAIInterval() {
    state.ai_interval_id = setInterval(() => {
        aiPlayGame();
        render();
    }, getDelayMs());
}

function cancelAIInterval() {
    clearInterval(state.ai_interval_id);
}

function onBoardCellClick(cell) {
    if (state.game_progress !== 'ACTIVE' || cell.nodeName !== 'TD') {
        return;
    }
    state.game.board = blokie.toggleSquare(state.game.board, cell.parentNode.rowIndex, cell.cellIndex);
    resetAIOnHumanInterferance();
}
function onPieceCellClick(cell) {
    if (state.game_progress !== 'ACTIVE' || cell.nodeName !== 'TD') {
        return;
    }
    const piece_table_id = parseInt(cell.closest('table').id.slice(-1));
    state.piece_set[piece_table_id] = blokie.toggleSquare(state.piece_set[piece_table_id], cell.parentNode.rowIndex, cell.cellIndex);
    resetAIOnHumanInterferance();
}

function resetAIOnHumanInterferance() {
    state.queued_game_states = [];
    cancelAIInterval();
    queueAIInterval();
    render();
}

function render() {
    let board_table = document.getElementById('game-board');
    let pieces_on_deck_div = document.getElementById('pieces-on-deck-container');
    if (state.game_progress === 'ACTIVE') {
        if (state.queued_game_states.length === 0) {
            drawGame(board_table, pieces_on_deck_div, state.game.board, blokie.getEmptyPiece(), state.piece_set);
            updateScore(state.game.score);
        } else {
            const next_game_state = state.queued_game_states[0];
            updateScore(next_game_state.score);
            const piece_set_to_render = state.piece_set.map(p => p === next_game_state.previous_piece ? blokie.getEmptyPiece() : p);
            drawGame(board_table, pieces_on_deck_div, state.game.board, next_game_state.previous_piece_placement, piece_set_to_render);
        }
    } else if (state.game_progress === 'OVER') {
        drawGame(board_table, pieces_on_deck_div, state.game.board, blokie.getEmptyPiece(), state.piece_set);
        updateScore("Final score: " + state.game.score.toString() + ". Tap board to restart.");
    }
}

function aiPlayGame() {
    if (state.queued_game_states.length === 0) {
        if (state.piece_set.every(p => blokie.isEmpty(p))) {
            state.piece_set = blokie.getRandomPieceSet();
        }
        state.queued_game_states = blokie.getAIMove(state.game, state.piece_set).new_game_states;
        state.game.previous_piece_placement = blokie.getEmptyPiece();
        return;
    }
    const new_game_state = state.queued_game_states.shift();
    if (blokie.isOver(new_game_state)) {
        state.game_progress = 'OVER';
        cancelAIInterval();
        return;
    }
    const piece_used = new_game_state.previous_piece;
    const used_piece_index = state.piece_set.indexOf(piece_used);
    if (used_piece_index >= 0) {
        state.piece_set[used_piece_index] = blokie.getEmptyPiece();
    }
    state.previous_game_state = state.game;
    state.game = new_game_state;
}

function getSpeedSlider() {
    return document.getElementById('speed');
}

function getDelayMs() {
    const slider = getSpeedSlider();
    return slider.value == slider.max ? 0 : 300 * slider.max / slider.value;
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
            } else if (
                blokie.at(board, r, c)
            ) {
                td.className = 'has-piece';
            } else {
                td.className = null;
            }
        }
    }

    for (let i = 0; i < 3; ++i) {
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const td = pieces_on_deck_div.children[i].rows[r].cells[c];
                td.className = blokie.at(piece_set[i], r, c) ? 'has-piece' : null;
            }
        }
    }
}
