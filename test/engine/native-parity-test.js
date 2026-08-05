"use strict";

// The engine exists twice.
//
// The web app runs the WASM build, whose move search lives in bindings.cpp
// because it has to hand back the three boards a move passes through and not
// just the one it ends on. Every measurement of how good the engine is runs the
// native build, whose move search is AI::makeMoveSimple in solver.cpp. The two
// are line for line the same search with the same evaluation, but they are two
// pieces of code, and nothing has been stopping them from drifting apart.
//
// That drift would be invisible in the worst way. Both halves would keep
// passing their own tests, the app would keep playing legal moves, and the
// weights would keep being tuned against a player nobody runs.
//
// So: play the same fixed sequence of deals through both and check they land on
// the same boards. Needs the native harness built (npm run build:native); skips
// itself with a warning when it is not there, since most checks here do not
// need a C++ toolchain.

import { execFileSync } from 'child_process';
import { existsSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

import { blokie, init } from '../../engine/js/blokie.js';

const here = dirname(fileURLToPath(import.meta.url));
const survival = join(here, '../../engine/cpp/build-native/survival');

if (!existsSync(survival)) {
    console.log('skipped - no native build at %s', survival);
    console.log('run `npm run build:native` to include this check');
    process.exit(0);
}

const MOVES = 400;

await init();

// The RNG blokie.js uses for its own fixed sequence, and the one
// `survival --parity` mirrors. Seeded identically on both sides, so the two
// engines are handed the same pieces in the same order.
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
    };
}

const native = execFileSync(survival, ['--parity', String(MOVES)], { encoding: 'utf8' })
    .trim().split('\n').map(line => {
        const [a, b, c] = line.split(' ').map(Number);
        return { a: a, b: b, c: c };
    });

if (native.length !== MOVES) {
    console.error('FAIL: asked the native build for %d moves, got %d', MOVES, native.length);
    process.exit(1);
}

// The same walk the native side takes: deal three pieces, play them as one
// move, start over if that ended the game.
// A mismatch in the piece tables would show up below as the two engines playing
// different boards, but it would read as a search that had drifted rather than
// as the tables being out of step, so say which it is.
if (blokie.numPieces !== 47) {
    console.error('FAIL: the JS piece table holds %d pieces, the C++ one holds 47',
        blokie.numPieces);
    process.exit(1);
}

const random = sfc32(1, 2, 3, 4);
let game = blokie.getNewGame();
let mismatch = -1;
for (let i = 0; i < MOVES && mismatch < 0; ++i) {
    const piece_set = [];
    for (let j = 0; j < 3; ++j) {
        piece_set.push(blokie.getPieceByIndex(Math.floor(random() * blokie.numPieces)));
    }
    game = blokie.getAIMove(game, piece_set).new_game_states[2];
    if (blokie.isOver(game)) {
        game = blokie.getNewGame();
    }
    const wasm = game.board;
    const expected = native[i];
    if (wasm.a !== expected.a || wasm.b !== expected.b || wasm.c !== expected.c) {
        mismatch = i;
        console.error('FAIL: the two builds part company at move %d', i);
        console.error('  wasm   %d %d %d', wasm.a, wasm.b, wasm.c);
        console.error('  native %d %d %d', expected.a, expected.b, expected.c);
    }
}

if (mismatch >= 0) {
    console.error('\nAI::makeMoveSimple in solver.cpp and the search in bindings.cpp '
        + 'no longer agree.\nWhichever one changed, the other has to change with it: '
        + 'the app runs one and every\nmeasurement runs the other.');
    process.exit(1);
}

console.log('ok - the WASM build and the native build play %d identical moves', MOVES);
console.log('\nall checks passed');
