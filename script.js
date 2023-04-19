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
    let pieces_on_deck_div = document.getElementById('pieces-on-deck-container');
    let game = blokie.getNewGame();

    while (!blokie.isOver(game)) {
        const piece_set = blokie.getRandomPieceSet();
        if (isMaxSpeed()) {
            updateScore(game.score.toString());
        }
        drawGame(board_table, pieces_on_deck_div, game.board, blokie.getEmptyPiece(), piece_set);
        const [unused, ai_move] = await Promise.all(
            [
                sleep(),
                async function () {
                    return blokie.getAIMove(game, piece_set);
                }(),
            ]
        );
        if (blokie.isOver(ai_move.new_game_states[2])) {
            game_ongoing = false;
            updateScore("Final score: " + game.score.toString() + ". Tap board to restart.");
            break;
        }

        for (let i = 0; i < 3; ++i) {
            const piece_used = ai_move.new_game_states[i].previous_piece;
            const used_piece_index = piece_set.indexOf(piece_used);
            if (used_piece_index >= 0) {
                piece_set[used_piece_index] = blokie.getEmptyPiece();
            }

            const placement = ai_move.new_game_states[i].previous_piece_placement;
            if (!isMaxSpeed()) {
                drawGame(board_table, pieces_on_deck_div, i === 0 ? game.board : ai_move.new_game_states[i - 1].board, placement, piece_set);
            }

            if (!isMaxSpeed()) {
                updateScore(ai_move.new_game_states[i].score.toString());
                await sleep();
            }
        }
        game = ai_move.new_game_states[2];
    }
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
