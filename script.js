"use strict";
import { blocky } from "./blocky.js";

document.addEventListener("DOMContentLoaded", function (event) {
    onLoad();
});


async function onLoad() {
    var canvas = document.getElementById('board');
    resizeCanvas(canvas);

    await playGameLoop();
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function playGameLoop() {
    var canvas = document.getElementById('board');
    var game = blocky.getNewGame();
    while (!blocky.isOver(game)) {
        const piece_set = blocky.getRandomPieceSet();
        const ai_move = blocky.getAIMove(game, piece_set);
        for (let i = 0; i < 3; ++i) {
            drawGame(canvas, ai_move.prev_boards[i], ai_move.prev_piece_placements[i]);
            await sleep(1000);
            console.log([blocky.count(ai_move.prev_boards[i])+blocky.count(ai_move.prev_piece_placements[i]), blocky.count(ai_move.prev_boards[i + 1]) ]);
            if (blocky.count(ai_move.prev_boards[i + 1]) !== blocky.count(ai_move.prev_boards[i])+blocky.count(ai_move.prev_piece_placements[i])) {
                drawGame(canvas, ai_move.prev_boards[i+1], blocky.getNewGame());
                await sleep(1000);
            }

        }
        game = ai_move.board;
    }
}


function resizeCanvas(canvas) {
    canvas.width = Math.min(window.innerWidth, window.innerHeight);
    canvas.height = Math.min(window.innerWidth, window.innerHeight);
}

function drawGame(canvas, board, placement) {
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
    ctx.lineWidth = 1;
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
    ctx.fillStyle = 'blue';
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (blocky.at(placement, r, c) || blocky.at(board, r, c)) {
                const rect = [c * grid_size, r * grid_size, grid_size, grid_size];
                ctx.fillStyle = blocky.at(placement, r, c) ? 'pink' : 'rgb(54,112,232)';
                ctx.fillRect(...rect);
                ctx.strokeStyle = 'black';
                ctx.lineWidth = 1;
                ctx.strokeRect(...rect);
            }
        }
    }



    // Draw outer border
    ctx.strokeStyle = 'black';
    ctx.lineWidth = 4;
    ctx.strokeRect(0, 0, canvas.width, canvas.height);
}
