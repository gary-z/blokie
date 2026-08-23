"use strict";

import { blokie } from '../../engine/js/blokie.js';

// JavaScript fitness and performance harnesses.

/**
 * @typedef {object} FitnessSample
 * @property {number} score
 * @property {number} num_moves Counting the move it died on, the way the native
 *   harness counts it.
 */

/** @returns {FitnessSample} */
function fitnessSample() {
    let game = blokie.newGame();
    let num_moves = 0;
    while (true) {
        ++num_moves;
        const move = blokie.makeMove(game, blokie.deal());
        if (!move.found) {
            return { score: game.score, num_moves: num_moves };
        }
        game = move.game;
    }
}

/** @type {(a: number, b: number, c: number, d: number) => () => number} */
function sfc32(a, b, c, d) {
    return function () {
        a |= 0; b |= 0; c |= 0; d |= 0;
        var t = (a + b | 0) + d | 0;
        d = d + 1 | 0;
        a = b ^ b >>> 9;
        b = c + (c << 3) | 0;
        c = (c << 21 | c >>> 11);
        c = c + t | 0;
        return (t >>> 0) / 4294967296;
    }
}

// Fixed work over a repeatable piece stream.
/** @type {(n: number) => void} */
function performanceSample(n) {
    const random = sfc32(1, 2, 3, 4);
    let game = blokie.newGame();
    for (let i = 0; i < n; ++i) {
        const move = blokie.makeMove(game, blokie.deal(random));
        game = move.found ? move.game : blokie.newGame();
    }
}

export { fitnessSample, performanceSample, sfc32 };
