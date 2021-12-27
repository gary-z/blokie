"use strict";
import { blocky } from "./blocky.js";

document.addEventListener("DOMContentLoaded", function (event) {
    onLoad();
});


async function onLoad() {
    var canvas = document.getElementById('board');
    await playGameLoop();
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function playGameLoop() {
    const DELAY = 1000;
    var canvas = document.getElementById('board');
    var game = blocky.getNewGame();
    let score = 0;
    while (!blocky.isOver(game)) {
        const piece_set = blocky.getRandomPieceSet();
        const centered_pieces = [];
        for (const p of piece_set) {
            centered_pieces.push(blocky.centerPiece(p));
        }

        drawGame(canvas, game, [], centered_pieces);
        const [unused, ai_move] = await Promise.all(
            [
                sleep(DELAY),
                async function () {
                    return blocky.getAIMove(game, piece_set);
                }(),
            ]
        );
        for (let i = 0; i < 3; ++i) {
            const num_cleared = Math.max(0, blocky.count(ai_move.prev_boards[i]) +
                blocky.count(ai_move.prev_piece_placements[i]) - blocky.count(ai_move.prev_boards[i + 1]));
            drawGame(canvas, ai_move.prev_boards[i], ai_move.prev_piece_placements[i], centered_pieces);
            score += blocky.count(ai_move.pieces[i]);
            updateScore(score);
            await sleep(DELAY);
            if (num_cleared > 0) {
                score += 16;
                updateScore(score);
                drawGame(canvas, ai_move.prev_boards[i + 1], blocky.getNewGame(), centered_pieces);
                await sleep(DELAY);
            }
        }
        game = ai_move.board;
    }
}

function updateScore(score) {
    const score_el = document.getElementById('score');
    score_el.innerText = score.toString();
}

function drawGame(canvas, board, placement, piece_set) {
    const ctx = canvas.getContext('2d');

    const grid_size = Math.min(canvas.width, canvas.height) / 9;

    if (blocky.isOver(board)) {
        ctx.fillStyle = 'red';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        return;
    }

    // Paint all white.
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // Paint major squares
    ctx.fillStyle = 'rgb(228,233,238)';
    for (let r = 0; r < 3; ++r) {
        for (let c = 0; c < 3; ++c) {
            if ((r % 2 == 0) ^ (c % 2 == 0)) {
                ctx.fillRect(r * grid_size * 3, c * grid_size * 3, grid_size * 3, grid_size * 3);
            }
        }
    }

    // Minor grid lines
    ctx.lineWidth = 0.5;
    ctx.strokeStyle = 'grey';
    for (let i = 0; i < 9; ++i) {
        ctx.beginPath();
        ctx.moveTo(i * grid_size, 0);
        ctx.lineTo(i * grid_size, 9 * grid_size);
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(0, i * grid_size);
        ctx.lineTo(9 * grid_size, i * grid_size);
        ctx.stroke();
    }

    // Major grid lines.
    ctx.strokeStyle = 'black';
    ctx.lineWidth = 2;
    for (let i = 0; i < 3; ++i) {
        ctx.beginPath();
        ctx.moveTo(i * grid_size * 3, 0);
        ctx.lineTo(i * grid_size * 3, 9 * grid_size);
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(0, i * grid_size * 3);
        ctx.lineTo(9 * grid_size, i * grid_size * 3,);
        ctx.stroke();
    }

    // The pieces.
    if (placement.length) {
        drawBoard(ctx, grid_size, placement, 'pink');
    }
    drawBoard(ctx, grid_size, board, 'rgb(54,112,232)');

    // Draw outer border
    ctx.strokeStyle = 'black';
    ctx.lineWidth = 2;
    ctx.strokeRect(1, 1, 9 * grid_size - 2, 9 * grid_size - 2);

    for (let i = 0; i < piece_set.length; ++i) {
        ctx.save();
        ctx.translate(10 + grid_size * 3 * i, grid_size * 9 + 50);
        ctx.scale(0.55, 0.55);
        drawBoard(ctx, grid_size, piece_set[i]);
        ctx.restore()
    }
}

function drawBoard(ctx, grid_size, board, fill_style) {
    ctx.fillStyle = fill_style;
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (blocky.at(board, r, c)) {
                const rect = [c * grid_size, r * grid_size, grid_size, grid_size];
                ctx.fillRect(...rect);
                ctx.strokeStyle = 'black';
                ctx.lineWidth = 1;
                ctx.strokeRect(...rect);
            }
        }
    }
}
