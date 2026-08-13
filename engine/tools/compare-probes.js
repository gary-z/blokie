"use strict";

// Compares continuous per-chain hazard estimates emitted by fitness --probe.
// The existing compare-fitness.js remains the exact test for death counts.

import { readFileSync } from 'fs';

function readRun(path) {
    let probes = 0;
    let adaptive = false;
    let chainMoves = 0;
    const values = [];
    for (const line of readFileSync(path, 'utf8').split('\n')) {
        const trimmed = line.trim();
        if (trimmed.startsWith('# probe ')) {
            const match = /M=(\d+)/.exec(trimmed);
            if (match) probes = Number(match[1]);
            if (trimmed.startsWith('# probe adaptive ')) adaptive = true;
            continue;
        }
        if (trimmed.startsWith('# options ')) {
            const match = /chain_moves=(\d+)/.exec(trimmed);
            if (match) chainMoves = Number(match[1]);
            continue;
        }
        if (trimmed === '' || trimmed.startsWith('#')) continue;
        const p = trimmed.split(/\s+/).map(Number);
        if (p.length < 10 || p.some((x) => !Number.isFinite(x))) {
            throw new Error(`${path}: malformed probe row: ${trimmed}`);
        }
        values.push({
            seed: p[2],
            boards: p[3],
            estimate: p.length >= 12 ? p[11] / p[3]
                : p[4] / (probes * p[3]),
        });
    }
    if (probes === 0 && !adaptive) {
        throw new Error(`${path}: no probe metadata`);
    }
    if (chainMoves === 0) {
        throw new Error(`${path}: probe comparison requires --chain-moves`);
    }
    if (values.length < 2) throw new Error(`${path}: need at least two chains`);
    if (values.some((x) => x.boards !== chainMoves)) {
        throw new Error(`${path}: chains do not all have the recorded exposure`);
    }
    return {
        path,
        values,
        estimates: values.map((x) => x.estimate),
    };
}

function mean(xs) {
    return xs.reduce((a, b) => a + b, 0) / xs.length;
}

function variance(xs) {
    const m = mean(xs);
    return xs.reduce((sum, x) => sum + (x - m) ** 2, 0) / (xs.length - 1);
}

function mulberry32(seed) {
    return function random() {
        seed |= 0;
        seed = seed + 0x6D2B79F5 | 0;
        let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
        t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
        return ((t ^ t >>> 14) >>> 0) / 4294967296;
    };
}

function resampledMean(xs, random) {
    let sum = 0;
    for (let i = 0; i < xs.length; i++) {
        sum += xs[Math.floor(random() * xs.length)];
    }
    return sum / xs.length;
}

function quantile(sorted, p) {
    const index = p * (sorted.length - 1);
    const lo = Math.floor(index);
    const hi = Math.ceil(index);
    const fraction = index - lo;
    return sorted[lo] * (1 - fraction) + sorted[hi] * fraction;
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
    const pairs = a.values.map((x) => [x.estimate, bBySeed.get(x.seed)]);
    if (pairs.some((x) => x[1] === undefined) || pairs.length !== b.values.length) {
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
