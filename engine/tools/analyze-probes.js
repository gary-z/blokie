"use strict";

// Reports the empirical across-chain variance reduction from a probe run.
// Fixed-exposure output from fitness --chain-moves is required: each row is
// one independent replicate with the same denominator.

import { readFileSync } from 'fs';

/**
 * One fixed-exposure chain, as the row that recorded it describes it. Each is
 * an independent replicate with the same denominator, which is what makes the
 * across-chain variance below meaningful.
 * @typedef {object} Chain
 * @property {number} seed Identifies the chain, and is what the baseline times
 *   are joined on.
 * @property {number} boards
 * @property {number} failures
 * @property {number} probeSeconds
 * @property {number} totalSeconds
 * @property {number} exposure
 * @property {number} deaths
 * @property {number} estimate The per-board hazard this chain came to, taken
 *   from the recorded column when the run wrote one and reconstructed from the
 *   failure count when it did not.
 */

/**
 * A probe run, and the chains it holds.
 * @typedef {object} ProbeRun
 * @property {string} path
 * @property {number} probes The M of `--probe M`, or 0 for an adaptive run.
 * @property {string} label How the run is named in the output: the adaptive
 *   schedule when there was one, and the probe count otherwise.
 * @property {Chain[]} chains
 */

/** @type {(path: string) => ProbeRun} */
function readRun(path) {
    let probes = 0;
    let label = null;
    let chainMoves = 0;
    const chains = [];
    for (const line of readFileSync(path, 'utf8').split('\n')) {
        const trimmed = line.trim();
        if (trimmed.startsWith('# probe ')) {
            const match = /M=(\d+)/.exec(trimmed);
            if (match) probes = Number(match[1]);
            const adaptive =
                /adaptive low=(\d+) middle=(\d+) high=(\d+)/.exec(trimmed);
            if (adaptive) {
                label = Number(adaptive[2]) === 0
                    ? `${adaptive[1]}/${adaptive[3]}`
                    : `${adaptive[1]}/${adaptive[2]}/${adaptive[3]}`;
            } else {
                const legacyAdaptive =
                    /adaptive low=(\d+) high=(\d+)/.exec(trimmed);
                if (legacyAdaptive) {
                    label = `${legacyAdaptive[1]}/${legacyAdaptive[2]}`;
                }
            }
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
            throw new Error(`${path}: malformed fixed-exposure row: ${trimmed}`);
        }
        chains.push({
            seed: p[2], boards: p[3], failures: p[4],
            probeSeconds: p[6], totalSeconds: p[7],
            exposure: p[8], deaths: p[9],
            estimate: p.length >= 12 ? p[11] / p[3]
                : p[4] / (probes * p[3]),
        });
    }
    if (probes === 0 && label === null) {
        throw new Error(`${path}: no probe metadata`);
    }
    if (chainMoves === 0) {
        throw new Error(`${path}: probe analysis requires --chain-moves`);
    }
    if (chains.length < 2) throw new Error(`${path}: need at least two chains`);
    for (const chain of chains) {
        if (chain.boards !== chainMoves || chain.exposure !== chainMoves) {
            throw new Error(
                `${path}: chain ${chain.seed} does not match recorded exposure`);
        }
    }
    return { path, probes, label: label ?? String(probes), chains };
}

/** @type {(paths: string[]) => Map<number, number>} */
function readBaseline(paths) {
    const times = new Map();
    for (const path of paths) {
        for (const line of readFileSync(path, 'utf8').split('\n')) {
            const trimmed = line.trim();
            if (trimmed === '' || trimmed.startsWith('#')) continue;
            const p = trimmed.split(/\s+/).map(Number);
            if (p.length < 10 || p.some((x) => !Number.isFinite(x))) {
                throw new Error(`${path}: malformed fixed-exposure row: ${trimmed}`);
            }
            times.set(p[2], p[7]);
        }
    }
    return times;
}

/** @type {(values: number[]) => number} */
function variance(values) {
    const mean = values.reduce((a, b) => a + b, 0) / values.length;
    return values.reduce((sum, x) => sum + (x - mean) ** 2, 0) /
        (values.length - 1);
}

/** @type {(seed: number) => () => number} */
function mulberry32(seed) {
    return function random() {
        seed |= 0;
        seed = seed + 0x6D2B79F5 | 0;
        let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
        t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
        return ((t ^ t >>> 14) >>> 0) / 4294967296;
    };
}

/** @type {(sorted: number[], p: number) => number} */
function quantile(sorted, p) {
    const index = p * (sorted.length - 1);
    const lo = Math.floor(index);
    const hi = Math.ceil(index);
    const fraction = index - lo;
    return sorted[lo] * (1 - fraction) + sorted[hi] * fraction;
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
    const deathEstimates = run.chains.map((c) => c.deaths / c.exposure);
    const probeEstimates = run.chains.map((c) => c.estimate);
    const deathVariance = variance(deathEstimates);
    const probeVariance = variance(probeEstimates);
    const rawGain = deathVariance / probeVariance;
    const random = mulberry32(seed ^ run.probes ^ run.chains.length);
    const bootGains = [];
    for (let iteration = 0; iteration < iterations; ++iteration) {
        const sampledDeaths = [];
        const sampledProbes = [];
        for (let i = 0; i < run.chains.length; ++i) {
            const selected = Math.floor(random() * run.chains.length);
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
        const total = run.chains.reduce((sum, c) => sum + c.totalSeconds, 0);
        const probe = run.chains.reduce((sum, c) => sum + c.probeSeconds, 0);
        costRatio = total / (total - probe);
    } else {
        let probedTime = 0;
        let baselineTime = 0;
        for (const chain of run.chains) {
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
        run.label, run.chains.length,
        deathVariance.toExponential(6), probeVariance.toExponential(6),
        gainInterval, costRatio.toFixed(4),
        (rawGain / costRatio).toFixed(4),
    ].join('\t'));
}
