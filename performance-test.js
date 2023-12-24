"use strict";

import { blokie } from './blokie.js';
import { performance } from 'perf_hooks';

const NUM_MOVES = 1000;

const start_time = performance.now();
let game = blokie.getNewGame();

for (let i = 0; i < NUM_MOVES; ++i) {
    const piece_set = blokie.getRandomPieceSet();

    game = blokie.getAIMove(game, piece_set).new_game_states[2];
    if (blokie.isOver(game)) {
        game = blokie.getNewGame();
    }
}
const execution_time_ms = performance.now() - start_time;

console.log(`${NUM_MOVES} moves in ${(execution_time_ms / 1000).toFixed(2)} seconds`);
