"use strict";
import { blokie } from "./blokie.js";

document.addEventListener("DOMContentLoaded", function (event) {
    onLoad();
});

let game_ongoing = false;

async function onLoad() {
    var canvas = document.getElementById('board');
    canvas.addEventListener("click", playGameLoop);

    await playGameLoop();
}

function sleep() {
    const slider = document.getElementById('speed');
    const ms = slider.value == slider.max ? 0 : 5000 / slider.value;
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function playGameLoop() {
    if (game_ongoing) {
        return;
    }
    game_ongoing = true;
    let board_table = document.getElementById('game-board');
    let on_deck_table = document.getElementById('pieces-on-deck');
    console.log(board_table);
    let game = blokie.getNewGame();
    let score = 0;
    let prev_move_was_clear = false;

    while (!blokie.isOver(game)) {
        const piece_set = blokie.getRandomPieceSet();
        const centered_pieces = [];
        for (const p of piece_set) {
            centered_pieces.push(blokie.centerPiece(p));
        }

        drawGame(board_table, on_deck_table, game, [], centered_pieces);
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
            const num_cleared = Math.max(0, blokie.count(ai_move.prev_boards[i]) +
                blokie.count(ai_move.prev_piece_placements[i]) - blokie.count(ai_move.prev_boards[i + 1]));
            drawGame(board_table, on_deck_table, ai_move.prev_boards[i], ai_move.prev_piece_placements[i], centered_pieces);

            // 1 point for each placed block that was not cleared.
            score += blokie.count(blokie.and(ai_move.prev_piece_placements[i], ai_move.prev_boards[i + 1]));
            updateScore(score.toString());
            await sleep();
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
                updateScore(score.toString());
                drawGame(board_table, on_deck_table, ai_move.prev_boards[i + 1], blokie.getNewGame(), centered_pieces);
                await sleep();
                prev_move_was_clear = true;
            } else {
                prev_move_was_clear = false;
            }
        }
        game = ai_move.board;
    }
}

function updateScore(score) {
    const score_el = document.getElementById('score');
    score_el.innerText = score;
}

function drawGame(board_table, on_deck_table,  board, placement, piece_set) {
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

    for (let i = 0; i < 3; ++i) {
        for (let r = 0; r < 5; ++r) {
            for (let c = 0; c < 5; ++c) {
                const td = on_deck_table.rows[r].cells[c + 5 * i];
                td.className = blokie.at(piece_set[i], r, c) ? 'has-piece' : null;
            }
        }
    }
}
