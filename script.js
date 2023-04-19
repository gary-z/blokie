"use strict";
import { blokie } from "./blokie.js";

document.addEventListener("DOMContentLoaded", function (event) {
    onLoad();
});

let game_ongoing = false;

async function onLoad() {
    var board_table = document.getElementById('game-board');
    board_table.addEventListener("click", playGameLoop);

    await playGameLoop();
}

function isMaxSpeed() {
    const slider = document.getElementById('speed');
    return slider.value == slider.max;
}

function sleep() {
    const slider = document.getElementById('speed');
    const ms = slider.value == slider.max ? 0 : 300 * slider.max / slider.value;
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function playGameLoop() {
    if (game_ongoing) {
        return;
    }
    game_ongoing = true;
    let board_table = document.getElementById('game-board');
    let on_deck_table = document.getElementById('pieces-on-deck');
    let game = blokie.getNewGame();
    let score = 0;
    let prev_move_was_clear = false;

    while (!blokie.isOver(game)) {
        const piece_set = blokie.getRandomPieceSet();
        if (isMaxSpeed()) {
            updateScore(score.toString());
        }
        drawGame(board_table, on_deck_table, game, [], piece_set);
        const [unused, ai_move] = await Promise.all(
            [
                sleep(),
                async function () {
                    return blokie.getAIMove(game, piece_set);
                }(),
            ]
        );
        if (blokie.isOver(ai_move.board)) {
            game_ongoing = false;
            updateScore("Final score: " + score.toString() + ". Tap board to restart.");
            break;
        }

        for (let i = 0; i < 3; ++i) {
            const piece_used = ai_move.pieces[i];
            const used_piece_index = piece_set.indexOf(piece_used);
            if (used_piece_index >= 0) {
                piece_set[used_piece_index] = blokie.getNewGame();
            }

            const placement = ai_move.prev_piece_placements[i];
            const num_cleared = Math.max(0, blokie.count(ai_move.prev_boards[i]) +
                blokie.count(placement) - blokie.count(ai_move.prev_boards[i + 1]));

            if (!isMaxSpeed()) {
                drawGame(board_table, on_deck_table, ai_move.prev_boards[i], placement, piece_set);
            }

            // 1 point for each placed block that was not cleared.
            score += blokie.count(blokie.and(placement, ai_move.prev_boards[i + 1]));
            if (num_cleared > 0) {
                // Streaks.
                if (prev_move_was_clear) {
                    score += 9;
                }

                // Combos.
                score += 18;
                if (num_cleared > 9) {
                    score += 18;
                }
                if (num_cleared >= 18) {
                    score += 18; // Not sure how 3x combos work yet.
                }
                prev_move_was_clear = true;
            } else {
                prev_move_was_clear = false;
            }
            if (!isMaxSpeed()) {
                updateScore(score.toString());
                await sleep();
            }
        }
        game = ai_move.board;
    }
}

function centerPieces(piece_set) {
    const centered_pieces = [];
    for (const p of piece_set) {
        centered_pieces.push(blokie.centerPiece(p));
    }
    return centered_pieces;
}

function updateScore(score) {
    const score_el = document.getElementById('score');
    score_el.innerText = score;
}

function drawGame(board_table, on_deck_table, board, placement, piece_set) {
    if (blokie.isOver(board)) {
        for (let r = 0; r < 9; ++r) {
            for (let c = 0; c < 9; ++c) {
                const td = board_table.rows[r].cells[c];
                td.className = 'piece-pending';
            }
        }
        return;
    }

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

    piece_set = centerPieces(piece_set);
    for (let i = 0; i < 3; ++i) {
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const td = on_deck_table.rows[r].cells[c + 5 * i];
                td.className = blokie.at(piece_set[i], r, c) ? 'has-piece' : null;
            }
        }
    }
}
