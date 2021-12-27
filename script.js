"use strict";
import { blocky } from "./blocky.js";

document.addEventListener("DOMContentLoaded", function (event) {
    onLoad();
});


function onLoad() {
    var canvas = document.getElementById('board');
    resizeCanvas(canvas);
    var game = blocky.getNewGame();

    setTimeout(() => moveAndDelay(game), 200);
}

function moveAndDelay(game) {
    var canvas = document.getElementById('board');
    drawGame(canvas, game);
    if (blocky.isOver(game)) {
        return;
    }

    const piece_set = blocky.getRandomPieceSet();
    const ai_move = blocky.getAIMove(game, piece_set);
    game = ai_move.board;
    setTimeout(() => moveAndDelay(game), 200);
}

function resizeCanvas(canvas) {
    canvas.width = Math.max(canvas.width, canvas.height);
    canvas.height = Math.max(canvas.width, canvas.height);
}

function drawGame(canvas, board) {
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const grid_size = Math.min(canvas.width, canvas.height) / 9;
    ctx.fillStyle = 'pink';
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            if (blocky.at(board, r, c)) {
                ctx.fillRect(c * grid_size, r * grid_size, grid_size, grid_size);
            }
        }
    }

    // Draw outer border
    ctx.strokeStyle = 'black';
    ctx.lineWidth = 4;
    ctx.strokeRect(0, 0, canvas.width, canvas.height);
}
