"use strict";

// Reading what `fitness` writes to stdout: a header naming the columns, a few
// comment lines recording the options the run was made with, and one row per
// chain.
//
// Every tool in this directory reads that format, and each used to re-derive
// it -- the same two header regexes, the same row check, and the same column
// numbers spelled as `p[3]` and `p[11]`. Those numbers are the reason this
// file exists. A row is fourteen fields wide and the interesting ones are in
// the middle, so a tool reaching for the wrong one gets a plausible number
// back rather than an error, and nothing says so until a comparison comes out
// strange. The columns are named once, here.

import { readFileSync } from 'fs';

/**
 * One chain, by the names `fitness` prints above its own output.
 * @typedef {object} ChainRow
 * @property {number} moves All moves attempted, burn-in after restarts included.
 * @property {boolean} ended Whether the chain died rather than being cut off.
 * @property {number} seed Identifies the chain, and is what two runs are
 *   joined on when they are compared pair by pair.
 * @property {number} probeBoards
 * @property {number} probeFailures
 * @property {number} probeSumP2
 * @property {number} probeSeconds
 * @property {number} totalSeconds Aggregate, so it includes burn-in and
 *   probing rather than just the moves that were scored.
 * @property {number} exposure Measured moves, which is what a hazard is per.
 * @property {number} deaths
 * @property {number} probeDraws
 * @property {number} probeSumP
 * @property {number} adaptiveHighBoards
 * @property {number} adaptiveMiddleBoards
 * @property {number} estimate The per-board hazard this chain came to, taken
 *   from the recorded probeSumP when the run wrote one and reconstructed from
 *   the failure count when it did not.
 */

/**
 * A run, and the chains it holds.
 * @typedef {object} FitnessRun
 * @property {string} path The file it was read from, which is what names it in
 *   both the output and the errors.
 * @property {number} probes The M of `--probe M`, or 0 when there was none.
 * @property {string | null} adaptiveLabel How an adaptive schedule is written
 *   in a table -- "low/high", or "low/middle/high" -- and null when the run
 *   used a fixed probe count or none at all.
 * @property {number} chainMoves The `--chain-moves N`, or 0 for whole games.
 * @property {number | null} burnIn The burn-in the run was measured with, when
 *   the file records one.
 * @property {ChainRow[]} rows In the order the file lists them.
 */

// A row is only required to reach `deaths`, which is everything a death-rate
// comparison needs. The columns past it were added later, and a run made
// before they were is still worth reading.
const REQUIRED_COLUMNS = 10;

// Where probeSumP sits. A row that reaches it carries the hazard the run
// measured; a shorter one predates the column and has to have it worked out
// from the failure count instead.
const PROBE_SUM_P_COLUMN = 11;

/**
 * The run recorded in a `fitness` output file.
 *
 * Reads the format and nothing beyond it: a file with no probe metadata, no
 * `--chain-moves`, or a single chain comes back as it is. What a particular
 * comparison needs of a run is that comparison's business, and each tool
 * checks for itself.
 *
 * @type {(path: string) => FitnessRun}
 */
function readFitnessOutput(path) {
    let probes = 0;
    /** @type {string | null} */
    let adaptiveLabel = null;
    let chainMoves = 0;
    /** @type {number | null} */
    let burnIn = null;
    /** @type {ChainRow[]} */
    const rows = [];

    for (const line of readFileSync(path, 'utf8').split('\n')) {
        const trimmed = line.trim();
        if (trimmed.startsWith('# probe ')) {
            const fixed = /M=(\d+)/.exec(trimmed);
            if (fixed) probes = Number(fixed[1]);
            adaptiveLabel = readAdaptiveLabel(trimmed) ?? adaptiveLabel;
            continue;
        }
        if (trimmed.startsWith('# options ')) {
            const chain = /chain_moves=(\d+)/.exec(trimmed);
            if (chain) chainMoves = Number(chain[1]);
            const burn = /burn_in=(\d+)/.exec(trimmed);
            if (burn) burnIn = Number(burn[1]);
            continue;
        }
        if (trimmed === '' || trimmed.startsWith('#')) continue;

        const fields = trimmed.split(/\s+/).map(Number);
        if (fields.length < REQUIRED_COLUMNS
            || fields.some((x) => !Number.isFinite(x))) {
            throw new Error(`${path}: malformed chain row: ${trimmed}`);
        }
        // The header comes before the rows, so the probe count a row needs to
        // reconstruct its estimate is already in hand by the time it is read.
        rows.push(toRow(fields, probes));
    }

    return { path, probes, adaptiveLabel, chainMoves, burnIn, rows };
}

/**
 * The schedule an adaptive `# probe` line describes, or null when the line is
 * not one. The two-band form is what older runs wrote.
 * @type {(line: string) => string | null}
 */
function readAdaptiveLabel(line) {
    const banded = /adaptive low=(\d+) middle=(\d+) high=(\d+)/.exec(line);
    if (banded) {
        return Number(banded[2]) === 0
            ? `${banded[1]}/${banded[3]}`
            : `${banded[1]}/${banded[2]}/${banded[3]}`;
    }
    const legacy = /adaptive low=(\d+) high=(\d+)/.exec(line);
    return legacy === null ? null : `${legacy[1]}/${legacy[2]}`;
}

// The one place the column order is written down. Everything else in this
// directory reads a field by name.
/** @type {(fields: number[], probes: number) => ChainRow} */
function toRow(fields, probes) {
    const [
        moves, ended, seed, probeBoards, probeFailures, probeSumP2,
        probeSeconds, totalSeconds, exposure, deaths, probeDraws, probeSumP,
        adaptiveHighBoards, adaptiveMiddleBoards,
    ] = fields;
    return {
        moves,
        ended: ended === 1,
        seed,
        probeBoards,
        probeFailures,
        probeSumP2,
        probeSeconds,
        totalSeconds,
        exposure,
        deaths,
        probeDraws,
        probeSumP,
        adaptiveHighBoards,
        adaptiveMiddleBoards,
        estimate: fields.length > PROBE_SUM_P_COLUMN
            ? probeSumP / probeBoards
            : probeFailures / (probes * probeBoards),
    };
}

/**
 * How a run names itself in a table: the adaptive schedule when there was one,
 * and the probe count otherwise.
 * @type {(run: FitnessRun) => string}
 */
function runLabel(run) {
    return run.adaptiveLabel ?? String(run.probes);
}

export { readFitnessOutput, runLabel };
