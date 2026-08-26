"use strict";

// Reports the empirical across-chain variance reduction from a probe run.
// Fixed-exposure output from fitness --chain-moves is required: each row is
// one independent replicate with the same denominator.

import { readFitnessOutput, runLabel } from './fitness-output.js';
import { mulberry32, quantile, variance } from './stats.js';

/** @typedef {import('./fitness-output.js').FitnessRun} FitnessRun */

/**
 * A probe run that has been checked to be the kind this analysis needs: a
 * fixed exposure per chain, at least two of them, and probe metadata saying
 * how each chain's estimate was made.
 * @type {(path: string) => FitnessRun}
 */
function readRun(path) {
    const run = readFitnessOutput(path);
    if (run.probes === 0 && run.adaptiveLabel === null) {
        throw new Error(`${path}: no probe metadata`);
    }
    if (run.chainMoves === 0) {
        throw new Error(`${path}: probe analysis requires --chain-moves`);
    }
    if (run.rows.length < 2) throw new Error(`${path}: need at least two chains`);
    for (const chain of run.rows) {
        if (chain.probeBoards !== run.chainMoves
            || chain.exposure !== run.chainMoves) {
            throw new Error(
                `${path}: chain ${chain.seed} does not match recorded exposure`);
        }
    }
    return run;
}

// What each chain of an unprobed run cost, which is what the probed times are
// measured against. Seeds identify the chains, so the two runs line up however
// their rows are ordered.
/** @type {(paths: string[]) => Map<number, number>} */
function readBaseline(paths) {
    const times = new Map();
    for (const path of paths) {
        for (const row of readFitnessOutput(path).rows) {
            times.set(row.seed, row.totalSeconds);
        }
    }
    return times;
}

const args = process.argv.slice(2);
const baselinePaths = [];
let iterations = 10000;
let seed = 0x52424553;
const paths = [];
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--baseline' && i + 1 < args.length) {
        baselinePaths.push(args[++i]);
    } else if (args[i] === '--bootstrap' && i + 1 < args.length) {
        iterations = Number(args[++i]);
    } else if (args[i] === '--seed' && i + 1 < args.length) {
        seed = Number(args[++i]);
    } else if (args[i] === '--help') {
        paths.length = 0;
        break;
    } else {
        paths.push(args[i]);
    }
}
if (paths.length === 0) {
    console.error('usage: node engine/tools/analyze-probes.js ' +
        '[--baseline unprobed.txt ...] [--bootstrap N] [--seed S] ' +
        '<probe-M1.txt> [probe-M10.txt ...]');
    process.exit(1);
}

const baseline = baselinePaths.length === 0 ? null : readBaseline(baselinePaths);
console.log('M\tchains\tvar_deaths\tvar_probe\traw_gain_95%CI\tcost_ratio\tspeedup');
for (const path of paths) {
    const run = readRun(path);
    const deathEstimates = run.rows.map((c) => c.deaths / c.exposure);
    const probeEstimates = run.rows.map((c) => c.estimate);
    const deathVariance = variance(deathEstimates);
    const probeVariance = variance(probeEstimates);
    const rawGain = deathVariance / probeVariance;
    const random = mulberry32(seed ^ run.probes ^ run.rows.length);
    const bootGains = [];
    for (let iteration = 0; iteration < iterations; ++iteration) {
        const sampledDeaths = [];
        const sampledProbes = [];
        for (let i = 0; i < run.rows.length; ++i) {
            const selected = Math.floor(random() * run.rows.length);
            sampledDeaths.push(deathEstimates[selected]);
            sampledProbes.push(probeEstimates[selected]);
        }
        const sampledProbeVariance = variance(sampledProbes);
        if (sampledProbeVariance > 0) {
            bootGains.push(variance(sampledDeaths) / sampledProbeVariance);
        }
    }
    bootGains.sort((x, y) => x - y);
    const gainInterval = `${rawGain.toFixed(3)} [` +
        `${quantile(bootGains, 0.025).toFixed(3)},` +
        `${quantile(bootGains, 0.975).toFixed(3)}]`;

    let costRatio;
    if (baseline === null) {
        const total = run.rows.reduce((sum, c) => sum + c.totalSeconds, 0);
        const probe = run.rows.reduce((sum, c) => sum + c.probeSeconds, 0);
        costRatio = total / (total - probe);
    } else {
        let probedTime = 0;
        let baselineTime = 0;
        for (const chain of run.rows) {
            // Read once rather than has()-then-get(), so what is checked and
            // what is added up are the same lookup.
            const unprobed = baseline.get(chain.seed);
            if (unprobed === undefined) {
                throw new Error(`baseline files: missing seed ${chain.seed}`);
            }
            probedTime += chain.totalSeconds;
            baselineTime += unprobed;
        }
        costRatio = probedTime / baselineTime;
    }
    console.log([
        runLabel(run), run.rows.length,
        deathVariance.toExponential(6), probeVariance.toExponential(6),
        gainInterval, costRatio.toFixed(4),
        (rawGain / costRatio).toFixed(4),
    ].join('\t'));
}
