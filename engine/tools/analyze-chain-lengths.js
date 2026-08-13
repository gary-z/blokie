"use strict";

// Compares fixed-exposure probe runs with different chain lengths. For equal
// chain sizes, Var(mean) is the across-chain variance divided by chain count.
// Multiplying by aggregate worker time gives variance per unit compute; lower
// is better and includes burn-in, probing, and restart overhead.

import { readFileSync } from 'fs';

function variance(values) {
    const mean = values.reduce((sum, x) => sum + x, 0) / values.length;
    return values.reduce((sum, x) => sum + (x - mean) ** 2, 0) /
        (values.length - 1);
}

function readRun(path) {
    let probes = 0;
    let chainMoves = 0;
    let burnIn = 0;
    const estimates = [];
    let workerSeconds = 0;
    let totalMoves = 0;
    for (const line of readFileSync(path, 'utf8').split('\n')) {
        const trimmed = line.trim();
        if (trimmed.startsWith('# probe ')) {
            const match = /M=(\d+)/.exec(trimmed);
            if (match) probes = Number(match[1]);
            continue;
        }
        if (trimmed.startsWith('# options ')) {
            const chainMatch = /chain_moves=(\d+)/.exec(trimmed);
            const burnMatch = /burn_in=(\d+)/.exec(trimmed);
            if (chainMatch) chainMoves = Number(chainMatch[1]);
            if (burnMatch) burnIn = Number(burnMatch[1]);
            continue;
        }
        if (trimmed === '' || trimmed.startsWith('#')) continue;
        const p = trimmed.split(/\s+/).map(Number);
        if (p.length < 10 || p.some((x) => !Number.isFinite(x))) {
            throw new Error(`${path}: malformed fixed-exposure row: ${trimmed}`);
        }
        estimates.push(p.length >= 12 ? p[11] / p[3]
            : p[4] / (probes * p[3]));
        workerSeconds += p[7];
        totalMoves += p[0];
    }
    if (probes === 0 || chainMoves === 0 || estimates.length < 2) {
        throw new Error(`${path}: requires a fixed-exposure uniform-probe run`);
    }
    const estimateVariance = variance(estimates) / estimates.length;
    return {
        path, probes, chainMoves, burnIn, chains: estimates.length,
        estimates, workerSeconds, totalMoves, estimateVariance,
        varianceCost: estimateVariance * workerSeconds,
    };
}

const paths = process.argv.slice(2);
if (paths.length === 0 || paths.includes('--help')) {
    console.error('usage: node engine/tools/analyze-chain-lengths.js <run.txt> ...');
    process.exit(1);
}
const rawRuns = paths.map(readRun);
const groups = new Map();
for (const run of rawRuns) {
    const key = `${run.chainMoves}:${run.burnIn}:${run.probes}`;
    if (!groups.has(key)) {
        groups.set(key, {
            ...run, paths: [run.path], estimates: [...run.estimates], runs: 1,
        });
    } else {
        const group = groups.get(key);
        group.paths.push(run.path);
        group.estimates.push(...run.estimates);
        group.chains += run.chains;
        group.workerSeconds += run.workerSeconds;
        group.totalMoves += run.totalMoves;
        group.runs++;
    }
}
const runs = [...groups.values()];
for (const run of runs) {
    run.estimateVariance = variance(run.estimates) / run.estimates.length;
    run.varianceCost = run.estimateVariance * run.workerSeconds;
}
const best = Math.min(...runs.map((run) => run.varianceCost));
console.log('moves\tburn\tM\truns\tchains\tworker_s\tvar(mean)\tvar*worker_s\trelative_cost');
for (const run of runs.sort((a, b) => a.chainMoves - b.chainMoves)) {
    console.log([
        run.chainMoves, run.burnIn, run.probes, run.runs, run.chains,
        run.workerSeconds.toFixed(1), run.estimateVariance.toExponential(6),
        run.varianceCost.toExponential(6), (run.varianceCost / best).toFixed(3),
    ].join('\t'));
}
