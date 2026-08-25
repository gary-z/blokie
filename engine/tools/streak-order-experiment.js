"use strict";

// What the clear streak is worth as a price rather than a tiebreak.
//
// A hand's play order cannot change the board it ends on: the search picks the
// three placements, and only their sequence is open. So every policy compared
// here walks the same boards, sees the same hands and dies on the same one --
// the only thing that separates them is points, which makes this a paired
// measurement with no survival difference to control for. That also bounds
// what is on offer: the streak pays 9, and a hand can collect it once, so no
// policy can be more than 9 points per hand ahead of another.
//
// Policies:
//   baseline      what make_move does today -- most points, and a tie goes to
//                 the order that leaves a clear behind.
//   streak W      most points after crediting W to an order that ends on a
//                 clear, so a streak can outbid points rather than only ties.
//   optimal       the best any ordering policy could have done, by a two-state
//                 forward pass over the whole game with the hands known in
//                 advance. Nothing that decides a hand when it is dealt can
//                 beat this, so it is the ceiling the sweep is measured against.

import { init, blokie, bits } from '../js/blokie.js';
import { sfc32 } from '../../test/engine/harness.js';

/**
 * Every play order that reaches the board the search settled on, priced. The
 * arrays are parallel and hold one entry per surviving order.
 * @typedef {object} HandOptions
 * @property {number[]} afterMiss Points when the move before this hand did not
 *   clear.
 * @property {number[]} afterClear Points when it did. Differs from afterMiss
 *   only by the 9 a streak pays, and only for orders that clear on the first
 *   placement.
 * @property {boolean[]} endsOnClear Whether the last placement cleared, which
 *   is what the next hand inherits. A board property, so the incoming state
 *   does not enter into it.
 * @property {boolean[]} startsOnClear Whether the first placement cleared,
 *   which is the only way an inherited streak can be collected -- the bonus is
 *   paid to a clear whose immediately preceding move also cleared, so a hand
 *   that opens with anything else has already let it lapse.
 */

/** Every order the three slots can be played in. */
const SLOT_ORDERS = [
    [0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0],
];

/** @type {(board: import('../js/blokie.js').BitBoard, previousWasClear: boolean, placementOfSlot: readonly import('../js/blokie.js').BitBoard[], order: readonly number[]) => import('../js/blokie.js').Game | null} */
function playOrder(board, previousWasClear, placementOfSlot, order) {
    let game = {
        board: board, previous_move_was_clear: previousWasClear, score: 0,
    };
    for (const slot of order) {
        const placement = placementOfSlot[slot];
        if (bits.isEmpty(placement)) continue;
        const move = blokie.place(game, placement);
        if (move === null) return null;
        game = move.new_game;
    }
    return game;
}

/** @type {(board: import('../js/blokie.js').BitBoard, planned: readonly import('../js/blokie.js').PlannedMove[], target: import('../js/blokie.js').BitBoard) => HandOptions} */
function handOptions(board, planned, target) {
    const placementOfSlot = [bits.empty(), bits.empty(), bits.empty()];
    for (const move of planned) {
        placementOfSlot[move.piece_index] = move.placement;
    }

    /** @type {HandOptions} */
    const options = {
        afterMiss: [], afterClear: [], endsOnClear: [], startsOnClear: [],
    };
    for (const order of SLOT_ORDERS) {
        const miss = playOrder(board, false, placementOfSlot, order);
        if (miss === null || !bits.equals(miss.board, target)) continue;
        const hit = playOrder(board, true, placementOfSlot, order);
        if (hit === null) continue;
        options.afterMiss.push(miss.score);
        options.afterClear.push(hit.score);
        options.endsOnClear.push(miss.previous_move_was_clear);
        // The two prices can only differ by the streak bonus, and only when the
        // opening placement is the clear that collects it.
        options.startsOnClear.push(hit.score !== miss.score);
    }
    return options;
}

/** @type {(planned: readonly import('../js/blokie.js').PlannedMove[]) => number} */
function totalPlaced(planned) {
    let total = 0;
    for (const move of planned) total += bits.count(move.placement);
    return total;
}

/** @type {(options: HandOptions, previousWasClear: boolean) => number[]} */
function pointsIn(options, previousWasClear) {
    return previousWasClear ? options.afterClear : options.afterMiss;
}

// Most points, and a tie goes to the order that leaves a clear behind. This is
// make_move's loop: it only ever replaces its best with a strictly higher score
// or an equal score that ends on a clear.
/** @type {(options: HandOptions, previousWasClear: boolean) => number} */
function chooseBaseline(options, previousWasClear) {
    const points = pointsIn(options, previousWasClear);
    let best = 0;
    for (let i = 1; i < points.length; ++i) {
        if (points[i] > points[best]) {
            best = i;
        } else if (points[i] === points[best]
            && options.endsOnClear[i] && !options.endsOnClear[best]) {
            best = i;
        }
    }
    return best;
}

// Most points once an order that ends on a clear has been credited `weight`.
/** @type {(options: HandOptions, previousWasClear: boolean, weight: number) => number} */
function chooseWeighted(options, previousWasClear, weight) {
    const points = pointsIn(options, previousWasClear);
    /** @type {(i: number) => number} */
    const value = (i) => points[i] + (options.endsOnClear[i] ? weight : 0);
    let best = 0;
    for (let i = 1; i < points.length; ++i) {
        if (value(i) > value(best)) {
            best = i;
        } else if (value(i) === value(best)
            && options.endsOnClear[i] && !options.endsOnClear[best]) {
            best = i;
        }
    }
    return best;
}

/**
 * A policy's running total over the shared trajectory.
 * @typedef {object} Arm
 * @property {string} name
 * @property {number} weight Ignored by the baseline arm, which is the one with
 *   no weight of its own.
 * @property {boolean} isBaseline
 * @property {number} points
 * @property {boolean} streakAlive
 * @property {number} handsEndingOnClear
 * @property {number} handsDivergingFromBaseline Hands where this arm took an
 *   order the baseline would not have.
 * @property {number} handsInheritingAStreak Hands this arm opened with the
 *   streak alive.
 * @property {number} handsCollectingAStreak Hands where it opened with the
 *   streak alive and then opened on a clear, which is what collects the 9. The
 *   ratio of the two is what a live streak is actually worth.
 */

/** @type {(name: string, weight: number, isBaseline: boolean) => Arm} */
function newArm(name, weight, isBaseline) {
    return {
        name: name, weight: weight, isBaseline: isBaseline, points: 0,
        streakAlive: false, handsEndingOnClear: 0,
        handsDivergingFromBaseline: 0, handsInheritingAStreak: 0,
        handsCollectingAStreak: 0,
    };
}

/**
 * The ceiling: the best total any ordering policy could have reached, found by
 * carrying the best total that ends each hand in each of the two streak states.
 * A policy choosing when the hand is dealt cannot do better, because this one
 * chooses knowing every hand still to come.
 */
class OptimalRun {
    constructor() {
        // Index 0 is "the last move did not clear", index 1 is "it did". A game
        // opens having cleared nothing, so the other state is unreachable.
        this.best = [0, -Infinity];
        this.finished = 0;
    }

    /** @type {(options: HandOptions) => void} */
    play(options) {
        const next = [-Infinity, -Infinity];
        for (let state = 0; state < 2; ++state) {
            if (this.best[state] === -Infinity) continue;
            const points = pointsIn(options, state === 1);
            for (let i = 0; i < points.length; ++i) {
                const to = options.endsOnClear[i] ? 1 : 0;
                const total = this.best[state] + points[i];
                if (total > next[to]) next[to] = total;
            }
        }
        this.best = next;
    }

    // A death ends the run: banking it and reopening in the un-cleared state is
    // what the next game starts from.
    restart() {
        this.finished += Math.max(this.best[0], this.best[1]);
        this.best = [0, -Infinity];
    }

    /** @returns {number} */
    total() {
        return this.finished + Math.max(this.best[0], this.best[1]);
    }
}

// The ceiling again, by exhaustive search over every combination of orders.
// Only usable over a handful of hands, which is the point: it says whether the
// forward pass above is to be believed before it is run over a hundred thousand.
/** @type {(recorded: readonly HandOptions[]) => number} */
function bruteForceOptimal(recorded) {
    /** @type {(index: number, previousWasClear: boolean) => number} */
    const best = (index, previousWasClear) => {
        if (index === recorded.length) return 0;
        const options = recorded[index];
        const points = pointsIn(options, previousWasClear);
        let most = -Infinity;
        for (let i = 0; i < points.length; ++i) {
            most = Math.max(most,
                points[i] + best(index + 1, options.endsOnClear[i]));
        }
        return most;
    };
    return best(0, false);
}

/** @type {(argv: readonly string[]) => {hands: number, seed: number, weights: number[], verify: number}} */
function readOptions(argv) {
    let hands = 100000;
    let seed = 1;
    let verify = 0;
    let weights = [1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 18, 36];
    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i];
        if (arg === '--hands') hands = Number(argv[++i]);
        else if (arg === '--seed') seed = Number(argv[++i]);
        else if (arg === '--verify') verify = Number(argv[++i]);
        else if (arg === '--weights') {
            weights = argv[++i].split(',').map(Number);
        } else if (arg === '--help') {
            console.log('usage: streak-order-experiment.js [--hands N] '
                + '[--seed N] [--weights a,b,c] [--verify N]');
            process.exit(0);
        } else {
            throw new Error(`unknown option: ${arg}`);
        }
    }
    return {
        hands: hands, seed: seed, weights: weights, verify: verify,
    };
}

const options = readOptions(process.argv.slice(2));
await init();

const random = sfc32(options.seed, 2, 3, 4);
const arms = [newArm('baseline', 0, true)];
for (const weight of options.weights) {
    arms.push(newArm(`streak ${weight}`, weight, false));
}
const optimal = new OptimalRun();

let board = blokie.newGame().board;
let hands = 0;
let deaths = 0;
let handsWithAChoice = 0;
let handsWithAClear = 0;
let handsWithAPricedStreak = 0;
let streakPriceTotal = 0;
/** @type {Map<number, number>} How often each asking price came up. */
const streakPrices = new Map();
/** @type {HandOptions[]} The opening hands, kept only to check the forward pass. */
const recorded = [];
const started = Date.now();

while (hands < options.hands) {
    const hand = blokie.deal(random);
    const move = blokie.makeMove(
        { board: board, previous_move_was_clear: false, score: 0 }, hand);
    if (!move.found) {
        ++deaths;
        board = blokie.newGame().board;
        for (const arm of arms) arm.streakAlive = false;
        optimal.restart();
        continue;
    }

    const target = move.game.board;
    const choices = handOptions(board, move.moves, target);
    if (choices.afterMiss.length === 0) {
        throw new Error('no play order reaches the board the search settled on');
    }
    ++hands;
    if (choices.afterMiss.length > 1) ++handsWithAChoice;
    // With no clear a hand pays exactly one point per square. A clear costs at
    // most the fifteen squares three pieces can cover and pays at least
    // eighteen, so anything above the square count is a bonus and nothing else
    // is. Which order is measured does not matter: the placements are the same
    // either way, so a line completed in one is completed in all of them.
    if (Math.max(...choices.afterMiss) > totalPlaced(move.moves)) {
        ++handsWithAClear;
    }

    // What forcing the streak forward costs, when it costs anything: the gap
    // between the best order overall and the best one that ends on a clear.
    const bestOverall = Math.max(...choices.afterMiss);
    let bestEnding = -Infinity;
    for (let i = 0; i < choices.afterMiss.length; ++i) {
        if (choices.endsOnClear[i]) {
            bestEnding = Math.max(bestEnding, choices.afterMiss[i]);
        }
    }
    if (bestEnding !== -Infinity && bestEnding < bestOverall) {
        ++handsWithAPricedStreak;
        streakPriceTotal += bestOverall - bestEnding;
        const gap = bestOverall - bestEnding;
        streakPrices.set(gap, (streakPrices.get(gap) ?? 0) + 1);
    }

    const baselineIndex = chooseBaseline(choices, arms[0].streakAlive);
    for (const arm of arms) {
        const index = arm.isBaseline ? chooseBaseline(choices, arm.streakAlive)
            : chooseWeighted(choices, arm.streakAlive, arm.weight);
        if (arm.streakAlive) {
            ++arm.handsInheritingAStreak;
            if (choices.startsOnClear[index]) ++arm.handsCollectingAStreak;
        }
        arm.points += pointsIn(choices, arm.streakAlive)[index];
        arm.streakAlive = choices.endsOnClear[index];
        if (arm.streakAlive) ++arm.handsEndingOnClear;
        // Only meaningful where the arms agree on the incoming state, which is
        // most hands; it is a diagnostic, not part of the result.
        if (!arm.isBaseline && index !== baselineIndex) {
            ++arm.handsDivergingFromBaseline;
        }
    }
    optimal.play(choices);
    if (recorded.length < options.verify) recorded.push(choices);

    board = target;
}

const elapsed = (Date.now() - started) / 1000;
const baselinePerHand = arms[0].points / hands;

if (options.verify > 0) {
    if (deaths > 0) {
        throw new Error('--verify wants an unbroken run; raise --hands or reseed');
    }
    const checked = new OptimalRun();
    for (const hand of recorded) checked.play(hand);
    const exhaustive = bruteForceOptimal(recorded);
    if (checked.total() !== exhaustive) {
        throw new Error(`forward pass says ${checked.total()} over `
            + `${recorded.length} hands, exhaustive search says ${exhaustive}`);
    }
    console.log(`# forward pass agrees with exhaustive search over `
        + `${recorded.length} hands: ${exhaustive} points`);
}

// Every arm plays one of the orders the ceiling chose among, so none of them
// can be above it. A violation means the forward pass is not seeing the whole
// choice set.
for (const arm of arms) {
    if (arm.points > optimal.total()) {
        throw new Error(`${arm.name} scored above the ceiling`);
    }
}

console.log(`# hands ${hands}, deaths ${deaths}, seed ${options.seed}, `
    + `${elapsed.toFixed(1)}s`);
console.log(`# hands with more than one play order: `
    + `${(100 * handsWithAChoice / hands).toFixed(1)}%`
    + `, hands that cleared: ${(100 * handsWithAClear / hands).toFixed(1)}%`);
console.log(`# hands where ending on a clear costs points: `
    + `${(100 * handsWithAPricedStreak / hands).toFixed(1)}%`
    + `, mean asking price `
    + `${(streakPriceTotal / Math.max(1, handsWithAPricedStreak)).toFixed(2)}`);
const prices = [...streakPrices.entries()].sort((a, b) => b[1] - a[1]);
console.log(`# asking prices, commonest first: `
    + prices.slice(0, 6).map(([gap, n]) =>
        `${gap} (${(100 * n / handsWithAPricedStreak).toFixed(0)}%)`).join(', '));
const baselineArm = arms[0];
console.log(`# a live streak is collected on `
    + `${(100 * baselineArm.handsCollectingAStreak
        / Math.max(1, baselineArm.handsInheritingAStreak)).toFixed(1)}%`
    + ` of the hands that inherit one, so it is worth `
    + `${(9 * baselineArm.handsCollectingAStreak
        / Math.max(1, baselineArm.handsInheritingAStreak)).toFixed(2)}`
    + ` points, not 9`);
console.log();
console.log(['policy', 'points', 'points/hand', 'delta/hand', 'percent',
    'ends_on_clear', 'diverged'].join('\t'));

/** @type {(name: string, points: number, endsOnClear: string, diverged: string) => void} */
function report(name, points, endsOnClear, diverged) {
    const perHand = points / hands;
    const delta = perHand - baselinePerHand;
    console.log([
        name,
        points,
        perHand.toFixed(4),
        (delta >= 0 ? '+' : '') + delta.toFixed(5),
        (delta >= 0 ? '+' : '') + (100 * delta / baselinePerHand).toFixed(4) + '%',
        endsOnClear,
        diverged,
    ].join('\t'));
}

for (const arm of arms) {
    report(arm.name, arm.points,
        (100 * arm.handsEndingOnClear / hands).toFixed(1) + '%',
        arm.isBaseline ? '--'
            : (100 * arm.handsDivergingFromBaseline / hands).toFixed(2) + '%');
}
report('optimal', optimal.total(), '--', '--');
console.log();
console.log(`# the ceiling is ${optimal.total() - arms[0].points} points over `
    + `${hands} hands, or `
    + `${((optimal.total() - arms[0].points) / hands).toFixed(5)} per hand`);
