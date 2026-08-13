"use strict";

import { blokie } from '../../engine/js/blokie.js';

// The two ways the engine gets run for a number rather than for a move: play a
// game out and report how long it lasted, or play a fixed number of moves and
// report how long that took.
//
// These used to sit in engine/js/blokie.js and ship with it. They are here
// instead because nothing on the page has ever called them -- a harness is not
// part of the engine's API, and the deployed site should not carry one.
//
// The native equivalents are engine/cpp/fitness.cpp and engine/cpp/benchmark.cpp,
// which are what long runs actually use. See docs/evaluating-changes.md for why
// game length is measured the way it is.

/**
 * @typedef {object} FitnessSample
 * @property {number} score
 * @property {number} num_moves Counting the move it died on, the way the native
 *   harness counts it.
 */

// Plays a game out with a fresh hand every move, so the only thing that can end
// it is a hand the search cannot place in full.
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

// Chris Doty-Humphrey's sfc32, mirrored by Sfc32 in engine/cpp/benchmark.cpp so
// the native benchmark and this one play the same pieces in the same order.
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

// A fixed amount of work: `n` searched moves off a fixed seed, so two runs of
// this are timing the same moves and not two different games. Starting over
// whenever the search runs out of room keeps it that way -- a run that stopped
// at the first dead board would time however far that particular game got.
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
