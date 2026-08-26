"use strict";

// Compares continuous per-chain hazard estimates emitted by fitness --probe.
// The existing compare-fitness.js remains the exact test for death counts.

import { readFitnessOutput } from './fitness-output.js';
import { mean, mulberry32, quantile, resampledMean, variance } from './stats.js';

/** @typedef {import('./fitness-output.js').ChainRow} ChainRow */

/**
 * One side of the comparison. `estimates` is `values` projected down to the
 * numbers the bootstrap resamples, kept alongside rather than recomputed
 * because every consumer below wants that array and not the rows.
 * @typedef {object} ProbeComparison
 * @property {string} path
 * @property {ChainRow[]} values
 * @property {number[]} estimates
 */

/** @type {(path: string) => ProbeComparison} */
function readRun(path) {
    const run = readFitnessOutput(path);
    if (run.probes === 0 && run.adaptiveLabel === null) {
        throw new Error(`${path}: no probe metadata`);
    }
    if (run.chainMoves === 0) {
        throw new Error(`${path}: probe comparison requires --chain-moves`);
    }
    if (run.rows.length < 2) throw new Error(`${path}: need at least two chains`);
    if (run.rows.some((row) => row.probeBoards !== run.chainMoves)) {
        throw new Error(`${path}: chains do not all have the recorded exposure`);
    }
    return {
        path,
        values: run.rows,
        estimates: run.rows.map((row) => row.estimate),
    };
}

const args = process.argv.slice(2);
let iterations = 10000;
let seed = 0x52424553;
let paired = false;
const extraBaselinePaths = [];
const extraCandidatePaths = [];
const paths = [];
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--bootstrap' && i + 1 < args.length) {
        iterations = Number(args[++i]);
    } else if (args[i] === '--seed' && i + 1 < args.length) {
        seed = Number(args[++i]);
    } else if (args[i] === '--paired') {
        paired = true;
    } else if (args[i] === '--append-baseline' && i + 1 < args.length) {
        extraBaselinePaths.push(args[++i]);
    } else if (args[i] === '--append-candidate' && i + 1 < args.length) {
        extraCandidatePaths.push(args[++i]);
    } else if (args[i] === '--help') {
        paths.length = 0;
        break;
    } else {
        paths.push(args[i]);
    }
}
if (paths.length !== 2 || !Number.isInteger(iterations) || iterations < 1000) {
    console.error('usage: node engine/tools/compare-probes.js ' +
        '[--bootstrap N] [--seed S] [--paired] ' +
        '[--append-baseline run.txt] [--append-candidate run.txt] ' +
        '<baseline.txt> <candidate.txt>');
    process.exit(1);
}

const a = readRun(paths[0]);
const b = readRun(paths[1]);
for (const path of extraBaselinePaths) {
    const extra = readRun(path);
    a.values.push(...extra.values);
    a.estimates.push(...extra.estimates);
}
for (const path of extraCandidatePaths) {
    const extra = readRun(path);
    b.values.push(...extra.values);
    b.estimates.push(...extra.estimates);
}
if (paired) {
    if (new Set(a.values.map((x) => x.seed)).size !== a.values.length ||
        new Set(b.values.map((x) => x.seed)).size !== b.values.length) {
        throw new Error('--paired requires unique chain seeds in each arm');
    }
    const bBySeed = new Map(b.values.map((x) => [x.seed, x.estimate]));
    // Built by hand rather than with `.map`, so the missing-seed case is a
    // check on one chain instead of a scan for undefined afterwards -- which
    // is also what leaves `pairs` a list of two real numbers.
    /** @type {[number, number][]} */
    const pairs = [];
    for (const chain of a.values) {
        const counterpart = bBySeed.get(chain.seed);
        if (counterpart === undefined) {
            throw new Error('--paired requires exactly the same chain seeds in both runs');
        }
        pairs.push([chain.estimate, counterpart]);
    }
    if (pairs.length !== b.values.length) {
        throw new Error('--paired requires exactly the same chain seeds in both runs');
    }
    a.estimates = pairs.map((x) => x[0]);
    b.estimates = pairs.map((x) => x[1]);
}
const meanA = mean(a.estimates);
const meanB = mean(b.estimates);
const observedDifference = meanB - meanA;
const observedRatio = meanB / meanA;
const random = mulberry32(seed);

const differences = new Float64Array(iterations);
const ratios = [];
const centeredA = a.estimates.map((x) => x - meanA);
const centeredB = b.estimates.map((x) => x - meanB);
const pairedDifferences = paired
    ? a.estimates.map((x, i) => b.estimates[i] - x) : [];
const centeredPairedDifferences = pairedDifferences.map(
    (x) => x - observedDifference);
let nullAsExtreme = 0;
for (let i = 0; i < iterations; i++) {
    let sampleA;
    let sampleB;
    let nullDifference;
    if (paired) {
        let sumA = 0;
        let sumB = 0;
        let nullSum = 0;
        for (let j = 0; j < a.estimates.length; ++j) {
            const selected = Math.floor(random() * a.estimates.length);
            sumA += a.estimates[selected];
            sumB += b.estimates[selected];
            nullSum += centeredPairedDifferences[selected];
        }
        sampleA = sumA / a.estimates.length;
        sampleB = sumB / a.estimates.length;
        nullDifference = nullSum / a.estimates.length;
    } else {
        sampleA = resampledMean(a.estimates, random);
        sampleB = resampledMean(b.estimates, random);
        nullDifference = resampledMean(centeredB, random) -
            resampledMean(centeredA, random);
    }
    differences[i] = sampleB - sampleA;
    if (sampleA > 0) ratios.push(sampleB / sampleA);
    if (Math.abs(nullDifference) >= Math.abs(observedDifference)) {
        nullAsExtreme++;
    }
}
differences.sort();
ratios.sort((x, y) => x - y);

const seA = Math.sqrt(variance(a.estimates) / a.estimates.length);
const seB = Math.sqrt(variance(b.estimates) / b.estimates.length);
console.log(`${paired ? 'paired ' : ''}bootstrap seed=${seed} iterations=${iterations}`);
console.log(`baseline   n=${a.estimates.length}  h=${meanA.toExponential(6)}  ` +
    `SE=${seA.toExponential(3)}`);
console.log(`candidate  n=${b.estimates.length}  h=${meanB.toExponential(6)}  ` +
    `SE=${seB.toExponential(3)}`);
console.log('');
console.log(`difference (candidate - baseline): ${observedDifference.toExponential(6)}` +
    `  95% bootstrap CI ${quantile(differences, 0.025).toExponential(6)} .. ` +
    quantile(differences, 0.975).toExponential(6));
if (Number.isFinite(observedRatio) && ratios.length > 0) {
    console.log(`hazard ratio (candidate / baseline): ${observedRatio.toFixed(5)}` +
        `  95% bootstrap CI ${quantile(ratios, 0.025).toFixed(5)} .. ` +
        quantile(ratios, 0.975).toFixed(5));
}
console.log(`two-sided centered-bootstrap p = ${((nullAsExtreme + 1) /
    (iterations + 1)).toExponential(3)}`);
