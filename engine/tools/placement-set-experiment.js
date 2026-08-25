"use strict";

// Whether the placements the search hands back are the best-scoring ones that
// reach the board it settled on.
//
// The search picks placements by evaluation, and every placement set that ends
// on the same board evaluates the same, so which one it returns is settled by
// the order it enumerates them and by nothing else. Points are not consulted.
// That is fine as long as they all score the same -- and they do not. Three
// pieces that exactly fill a 3x3 cube clear it wherever it sits, so the board
// cannot tell the difference, but the piece that completes the cube forfeits
// its own squares to the clear, and swapping which piece that is moves the
// score.
//
// So this walks a run and, at every hand, enumerates every placement of the
// three pieces in every order that lands on the board the search chose, and
// asks whether any of them outscores what the engine played. The board
// trajectory is the engine's either way, which makes it a paired measurement:
// the games are the same length and only the points differ.
//
// It is far too slow to run inside a game -- the enumeration costs an order of
// magnitude more than the search it is checking -- so it is a measurement, not
// a candidate implementation.

import { init, blokie, bits, _internals } from '../js/blokie.js';
import { sfc32 } from '../../test/engine/harness.js';

const { count, or, diff, is_empty, equal, row, column, cube } = _internals;

/** @type {import('../js/blokie.js').BitBoard[]} Every row, column and cube: the twenty-seven things that can clear. */
const LINES = [];
for (let i = 0; i < 9; ++i) {
    LINES.push(row(i));
    LINES.push(column(i));
    LINES.push(cube(i));
}

/** @type {(game: import('../js/blokie.js').Game, piece: import('../js/blokie.js').Piece) => import('../js/blokie.js').Placement[]} */
function placementsOf(game, piece) {
    const found = [];
    for (let r = 0; r < 9; ++r) {
        for (let c = 0; c < 9; ++c) {
            const move = blokie.placeAt(game, piece, r, c);
            if (move !== null) found.push(move.placement);
        }
    }
    return found;
}

// Two necessary conditions for `board` still being able to become `target` with
// `squaresLeft` squares still to place. Neither can reject a reachable board:
//
//   - a square the target has and the board does not has to be placed, and
//     there are only so many squares left to place it with;
//   - a square the board has and the target does not has to be cleared, and
//     only a line that can still be filled can clear it.
//
// Without them the search is the full cross product of three pieces' placements
// and runs about seven times longer. --verify checks the two agree.
/** @type {(board: import('../js/blokie.js').BitBoard, target: import('../js/blokie.js').BitBoard, squaresLeft: number) => boolean} */
function canStillReach(board, target, squaresLeft) {
    if (count(diff(target, board)) > squaresLeft) return false;
    const extra = diff(board, target);
    if (is_empty(extra)) return true;
    let clearable = bits.empty();
    for (const line of LINES) {
        if (count(diff(line, board)) <= squaresLeft) clearable = or(clearable, line);
    }
    return is_empty(diff(extra, clearable));
}

/**
 * What the hand could have been worth, and how its alternatives differed.
 * @typedef {object} HandSearch
 * @property {number} best The most points any placement set reaching the target
 *   board scores, played in its best order.
 * @property {number} worst The least.
 * @property {boolean} otherSquares Some alternative covers a different set of
 *   squares than the engine's.
 * @property {boolean} retiled Some alternative covers the engine's squares but
 *   hands them to different pieces.
 */

/** @type {(start: import('../js/blokie.js').Game, hand: import('../js/blokie.js').Hand, enginePlacements: readonly import('../js/blokie.js').Placement[], target: import('../js/blokie.js').BitBoard, prune: boolean) => HandSearch} */
function searchHand(start, hand, enginePlacements, target, prune) {
    const justified = hand.map((piece) => bits.justify(piece));
    const sizes = justified.map((piece) => count(piece));
    const engineUnion = or(or(enginePlacements[0], enginePlacements[1]),
        enginePlacements[2]);

    /** @type {HandSearch} */
    const result = {
        best: -Infinity, worst: Infinity, otherSquares: false, retiled: false,
    };
    const chosen = [bits.empty(), bits.empty(), bits.empty()];
    // The hand on its own, from the streak it really inherited. `start` carries
    // the game's running total, which would otherwise be added to every score
    // here and compared against a single hand's worth of points.
    const from = {
        board: start.board,
        previous_move_was_clear: start.previous_move_was_clear,
        score: 0,
    };

    /** @type {(game: import('../js/blokie.js').Game, remaining: readonly number[], squaresLeft: number) => void} */
    const walk = (game, remaining, squaresLeft) => {
        if (remaining.length === 0) {
            if (!equal(game.board, target)) return;
            const union = or(or(chosen[0], chosen[1]), chosen[2]);
            if (!equal(union, engineUnion)) {
                result.otherSquares = true;
            } else if (!chosen.every((p, i) => equal(p, enginePlacements[i]))) {
                result.retiled = true;
            }
            result.best = Math.max(result.best, game.score);
            result.worst = Math.min(result.worst, game.score);
            return;
        }
        // Two slots holding the same shape are interchangeable, so playing the
        // second one is the first one's placement set under another name.
        /** @type {number[]} */
        const shapesTried = [];
        for (const slot of remaining) {
            if (shapesTried.some((s) => equal(justified[s], justified[slot]))) {
                continue;
            }
            shapesTried.push(slot);
            for (const placement of placementsOf(game, hand[slot])) {
                const move = blokie.place(game, placement);
                if (move === null) continue;
                const left = squaresLeft - sizes[slot];
                if (prune && !canStillReach(move.new_game.board, target, left)) {
                    continue;
                }
                chosen[slot] = placement;
                walk(move.new_game, remaining.filter((s) => s !== slot), left);
                chosen[slot] = bits.empty();
            }
        }
    };
    walk(from, [0, 1, 2], sizes[0] + sizes[1] + sizes[2]);
    return result;
}

/** @type {(argv: readonly string[]) => {hands: number, seed: number, verify: number}} */
function readOptions(argv) {
    let hands = 20000;
    let seed = 1;
    let verify = 0;
    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i];
        if (arg === '--hands') hands = Number(argv[++i]);
        else if (arg === '--seed') seed = Number(argv[++i]);
        else if (arg === '--verify') verify = Number(argv[++i]);
        else if (arg === '--help') {
            console.log('usage: placement-set-experiment.js [--hands N] '
                + '[--seed N] [--verify N]');
            process.exit(0);
        } else {
            throw new Error(`unknown option: ${arg}`);
        }
    }
    return { hands: hands, seed: seed, verify: verify };
}

const options = readOptions(process.argv.slice(2));
await init();

const random = sfc32(options.seed, 2, 3, 4);
let game = blokie.newGame();
let hands = 0;
let deaths = 0;
let otherSquares = 0;
let retiled = 0;
let scoreVaries = 0;
let beaten = 0;
let extraPoints = 0;
let biggestGain = 0;
const started = Date.now();

while (hands < options.hands) {
    const hand = blokie.deal(random);
    const move = blokie.makeMove(game, hand);
    if (!move.found) {
        ++deaths;
        game = blokie.newGame();
        continue;
    }

    const enginePlacements = [bits.empty(), bits.empty(), bits.empty()];
    for (const planned of move.moves) {
        enginePlacements[planned.piece_index] = planned.placement;
    }
    const target = move.game.board;
    const found = searchHand(game, hand, enginePlacements, target, true);
    if (found.best === -Infinity) {
        throw new Error('no placement set reaches the board the search settled on');
    }
    if (hands < options.verify) {
        const unpruned = searchHand(game, hand, enginePlacements, target, false);
        if (unpruned.best !== found.best || unpruned.worst !== found.worst
            || unpruned.otherSquares !== found.otherSquares
            || unpruned.retiled !== found.retiled) {
            throw new Error('the prunes changed what the search found');
        }
    }

    ++hands;
    if (found.otherSquares) ++otherSquares;
    if (found.retiled) ++retiled;
    if (found.best !== found.worst) ++scoreVaries;

    // move.game.score is the whole game's running total; the hand is worth the
    // difference, and both sides of it saw the same incoming streak.
    const engineScore = move.game.score - game.score;
    if (found.best > engineScore) {
        ++beaten;
        extraPoints += found.best - engineScore;
        biggestGain = Math.max(biggestGain, found.best - engineScore);
    }

    game = move.game;
}

const elapsed = (Date.now() - started) / 1000;
if (options.verify > 0) {
    console.log(`# prunes agree with the full cross product over `
        + `${Math.min(options.verify, hands)} hands`);
}
console.log(`# hands ${hands}, deaths ${deaths}, seed ${options.seed}, `
    + `${elapsed.toFixed(1)}s (${(1000 * elapsed / hands).toFixed(2)} ms/hand)`);
console.log();
console.log(`same board, different squares covered\t`
    + `${otherSquares}\t${(100 * otherSquares / hands).toFixed(2)}%`);
console.log(`same board, same squares, other pieces\t`
    + `${retiled}\t${(100 * retiled / hands).toFixed(2)}%`);
console.log(`score varies across the alternatives\t`
    + `${scoreVaries}\t${(100 * scoreVaries / hands).toFixed(2)}%`);
console.log(`an alternative outscores the engine\t`
    + `${beaten}\t${(100 * beaten / hands).toFixed(3)}%`);
console.log();
console.log(`# points left on the table: ${extraPoints} over ${hands} hands, `
    + `${(extraPoints / hands).toFixed(4)} per hand, biggest single ${biggestGain}`);
