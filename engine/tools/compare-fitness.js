"use strict";

// Compares two fitness runs on the rate at which the engine dies, rather than
// on how long its games were.
//
// The reason to prefer the rate: game length is very close to exponential, so
// a run's mean carries a standard deviation about as large as itself, and the
// only thing that shrinks the error bar is watching more games end. A game cut
// off by --max-moves never ends, but it still went a known number of moves
// without dying, and that is real evidence about the rate even though it says
// nothing usable about the mean. Counting deaths over moves survived uses every
// cut-off game at full value; averaging lengths throws them away and quietly
// biases the average down besides.
//
//   node engine/tools/compare-fitness.js baseline.txt candidate.txt
//
// Each argument is either a file of "moves ended seed" lines as written by the
// fitness harness, or a literal deaths:exposure pair, so a baseline measured
// once can be carried forward without re-running it.

import { readFileSync } from 'fs';

function lgamma(z) {
    // Lanczos approximation, g = 7, n = 9.
    const c = [
        676.5203681218851, -1259.1392167224028, 771.32342877765313,
        -176.61502916214059, 12.507343278686905, -0.13857109526572012,
        9.9843695780195716e-6, 1.5056327351493116e-7,
    ];
    if (z < 0.5) {
        return Math.log(Math.PI / Math.sin(Math.PI * z)) - lgamma(1 - z);
    }
    z -= 1;
    let x = 0.99999999999980993;
    for (let i = 0; i < c.length; i++) x += c[i] / (z + i + 1);
    const t = z + c.length - 0.5;
    return 0.5 * Math.log(2 * Math.PI) + (z + 0.5) * Math.log(t) - t + Math.log(x);
}

function logChoose(n, k) {
    return lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1);
}

// Two-sided exact binomial test, summing every outcome no more likely than the
// one observed.
function binomTest(k, n, p) {
    if (n === 0) return 1;
    const logPmf = (i) =>
        logChoose(n, i) + i * Math.log(p) + (n - i) * Math.log1p(-p);
    const observed = logPmf(k);
    let total = 0;
    for (let i = 0; i <= n; i++) {
        const lp = logPmf(i);
        // The 1e-9 slack keeps the symmetric outcome of a fair split from
        // being dropped by rounding.
        if (lp <= observed + 1e-9) total += Math.exp(lp);
    }
    return Math.min(1, total);
}

function readRun(arg, burnIn) {
    const literal = /^(\d+):(\d+)$/.exec(arg);
    if (literal) {
        return {
            name: arg,
            deaths: Number(literal[1]),
            exposure: Number(literal[2]),
            games: null,
            cutOff: null,
        };
    }
    let deaths = 0;
    let exposure = 0;
    let games = 0;
    let cutOff = 0;
    const text = readFileSync(arg, 'utf8');
    for (const line of text.split('\n')) {
        const trimmed = line.trim();
        if (trimmed === '' || trimmed.startsWith('#')) continue;
        const parts = trimmed.split(/\s+/);
        const moves = Number(parts[0]);
        const ended = parts.length > 1 ? Number(parts[1]) === 1 : true;
        if (!Number.isFinite(moves)) {
            throw new Error(`${arg}: cannot read a move count from "${trimmed}"`);
        }
        games++;
        if (!ended) cutOff++;
        if (moves > burnIn) {
            exposure += moves - burnIn;
            if (ended) deaths++;
        }
    }
    return { name: arg, deaths, exposure, games, cutOff };
}

const args = process.argv.slice(2);
let burnIn = 0;
const positional = [];
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--burn-in' && i + 1 < args.length) {
        burnIn = Number(args[++i]);
    } else if (args[i] === '--help') {
        positional.length = 0;
        break;
    } else {
        positional.push(args[i]);
    }
}

if (positional.length !== 2) {
    console.error(
        'usage: node engine/tools/compare-fitness.js [--burn-in B] ' +
        '<baseline> <candidate>\n' +
        '\n' +
        'Each of <baseline> and <candidate> is a file of "moves ended seed"\n' +
        'lines from the fitness harness, or a literal deaths:exposure pair.');
    process.exit(1);
}

const a = readRun(positional[0], burnIn);
const b = readRun(positional[1], burnIn);

for (const run of [a, b]) {
    if (run.exposure === 0) {
        console.error(`${run.name}: no moves survived past the burn-in`);
        process.exit(1);
    }
}
if (a.deaths === 0 || b.deaths === 0) {
    console.error(
        `no deaths in ${a.deaths === 0 ? a.name : b.name}, so there is ` +
        'nothing to compare. Raise --max-moves or run more games.');
    process.exit(1);
}

const hazardA = a.deaths / a.exposure;
const hazardB = b.deaths / b.exposure;
const ratio = hazardB / hazardA;
// sd(log h) is 1/sqrt(deaths), so the two runs' errors add in quadrature.
const se = Math.sqrt(1 / a.deaths + 1 / b.deaths);
const lo = ratio * Math.exp(-1.959963985 * se);
const hi = ratio * Math.exp(1.959963985 * se);

// Conditioned on the total number of deaths, how they split between the two
// runs is binomial with a probability set only by how many moves each one
// played. That makes the test exact rather than a normal approximation to a
// very skewed thing.
const totalDeaths = a.deaths + b.deaths;
const share = a.exposure / (a.exposure + b.exposure);
const p = binomTest(a.deaths, totalDeaths, share);

const pct = (x) => `${((x - 1) * 100).toFixed(1)}%`;

console.log(`burn-in: ${burnIn} moves`);
for (const [label, run, hazard] of [['baseline ', a, hazardA],
                                    ['candidate', b, hazardB]]) {
    const games = run.games === null
        ? 'banked'
        : `${run.games} games, ${run.cutOff} cut off`;
    console.log(
        `${label}  ${run.deaths} deaths / ${run.exposure} moves` +
        `  hazard ${hazard.toExponential(4)}` +
        `  mean length ${(1 / hazard).toFixed(0)}  (${games})`);
}
console.log('');
console.log(`hazard ratio (candidate / baseline): ${ratio.toFixed(4)}` +
            `  95% CI ${lo.toFixed(4)} .. ${hi.toFixed(4)}`);
console.log(`mean game length: ${pct(1 / ratio)} ` +
            `(95% CI ${pct(1 / hi)} .. ${pct(1 / lo)})`);
console.log(`exact two-sided p = ${p.toExponential(3)}`);
console.log('');

if (p < 0.05) {
    console.log(ratio < 1
        ? 'The candidate dies less often. The change looks real.'
        : 'The candidate dies more often. The change looks real, and bad.');
} else {
    // What the run could have resolved is worth saying, because "no
    // difference" and "not enough deaths to tell" look identical otherwise.
    const resolvable = 2.8015852 * se;
    console.log(
        'Not significant at 0.05. With ' +
        `${a.deaths} and ${b.deaths} deaths this run could only have caught ` +
        `a change of about ${(resolvable * 100).toFixed(0)}% or more, so a ` +
        'smaller real change would not have shown up.');
}
