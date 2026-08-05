"use strict";

// Reads the JSON the survival harness writes and says whether two runs differ.
//
//   node tools/survival-report.js run.json                 -- summarise one run
//   node tools/survival-report.js baseline.json new.json   -- compare two
//
// The number being compared is the mean game length, which both runs measure
// two ways: by counting deaths, and by averaging the exact hazard of the boards
// the engine visited. The second is far more precise for the same number of
// moves, so it is what the verdict is based on; the first is printed beside it
// because two estimates of the same quantity that disagree mean something is
// wrong with the run.
//
// Nothing here decides anything on its own. What it is for is answering "have I
// run this long enough yet", which is the question that otherwise gets answered
// by waiting overnight. See docs/game-length.md.

import { readFileSync } from 'fs';

// 95% two sided, and 80% power. Together: how many standard errors a difference
// has to be worth before a run this size can be expected to see it.
const Z_95 = 1.959964;
const Z_POWER_80 = 0.8416212;

function readRun(path) {
    let parsed;
    try {
        parsed = JSON.parse(readFileSync(path, 'utf8'));
    } catch (error) {
        console.error(`could not read ${path}: ${error.message}`);
        process.exit(1);
    }
    if (typeof parsed.moves !== 'number') {
        console.error(`${path} does not look like survival --json output`);
        process.exit(1);
    }
    parsed._path = path;
    return parsed;
}

function count(n) {
    return Math.round(n).toLocaleString('en-US');
}

// Everything printed here is a 95% half width, which is what the harness itself
// prints. Quoting one standard error next to the harness's two would read as
// the same run being twice as precise depending on which output you looked at.
function halfWidth(estimate) {
    return `+-${(100 * Z_95 * estimate.relative).toFixed(1)}%`;
}

// The mean game length a hazard implies, with the interval the hazard's
// standard error implies. Returns null for a run that measured no hazard.
function lengthFromHazard(run) {
    if (!run.hazard_samples || run.hazard_mean <= 0) {
        return null;
    }
    const relative = run.hazard_sem / run.hazard_mean;
    return {
        length: 1 / run.hazard_mean,
        relative: relative,
        lo: 1 / (run.hazard_mean + Z_95 * run.hazard_sem),
        hi: run.hazard_mean > Z_95 * run.hazard_sem
            ? 1 / (run.hazard_mean - Z_95 * run.hazard_sem)
            : Infinity,
    };
}

function lengthFromDeaths(run) {
    if (!run.deaths) {
        return null;
    }
    return {
        length: run.moves / run.deaths,
        relative: 1 / Math.sqrt(run.deaths),
        lo: run.death_rate_ci95[1] > 0 ? 1 / run.death_rate_ci95[1] : 0,
        hi: run.death_rate_ci95[0] > 0 ? 1 / run.death_rate_ci95[0] : Infinity,
    };
}

function describe(run) {
    const hazard = lengthFromHazard(run);
    const deaths = lengthFromDeaths(run);
    console.log(`${run._path}`);
    console.log(`  pool           ${run.pool} (${run.pool_size} pieces), `
        + `deal of ${run.deal_size}`);
    console.log(`  weights        ${run.weights.join(',')}`);
    console.log(`  moves          ${count(run.moves)} in ${run.wall_seconds.toFixed(0)}s`);
    console.log(`  deaths         ${count(run.deaths)}`
        + (run.censored_games ? `  (${count(run.censored_games)} game(s) cut short)` : ''));
    if (deaths) {
        console.log(`  length, deaths ${count(deaths.length)}`
            + `  ${halfWidth(deaths)}`);
    }
    if (hazard && run.hazard_is_game_length === false) {
        console.log(`  stress         1 deal in ${count(hazard.length)}`
            + `  ${halfWidth(hazard)}`
            + `   (measured against '${run.hazard_pool}' deals of `
            + `${run.hazard_deal_size}, so it is not a game length)`);
    } else if (hazard) {
        console.log(`  length, hazard ${count(hazard.length)}`
            + `  ${halfWidth(hazard)}`
            + `   (${count(run.hazard_samples)} boards, `
            + `worth ${count(run.effective_deaths)} deaths of precision)`);
    }
    if (run.capped_boards) {
        console.log(`  WARNING        ${count(run.capped_boards)} board(s) hit --pair-cap; `
            + `the hazard is an upper bound on those`);
    }

    // Two readings of the same number. Far enough apart and one of them is
    // measuring something other than what it claims to.
    if (hazard && deaths && run.deaths >= 20 && run.hazard_is_game_length !== false) {
        const spread = Math.hypot(hazard.relative, deaths.relative);
        const gap = Math.log(hazard.length / deaths.length);
        if (Math.abs(gap) > Z_95 * spread) {
            console.log(`  WARNING        the two estimates disagree by `
                + `${(100 * (Math.exp(Math.abs(gap)) - 1)).toFixed(0)}%, `
                + `which is more than their error bars allow`);
        }
    }

    if (run.hazard_by_move_index && run.hazard_by_move_index.length > 0) {
        console.log('\n  hazard by move index');
        const peak = Math.max(...run.hazard_by_move_index.map(b => b.hazard));
        for (const bucket of run.hazard_by_move_index) {
            const range = bucket.to < 0
                ? `${count(bucket.from)}+`
                : `${count(bucket.from)}-${count(bucket.to - 1)}`;
            const bar = peak > 0
                ? '#'.repeat(Math.max(0, Math.round(40 * bucket.hazard / peak)))
                : '';
            console.log(`    ${range.padEnd(15)} n=${String(count(bucket.n)).padEnd(9)}`
                + `${bucket.hazard.toExponential(2).padEnd(10)} ${bar}`);
        }
    }
}

// A run is a rate measured over a number of moves, so the ratio of two runs is
// a ratio of rates. Everything below works on the log of that ratio, where the
// error bars are symmetric and the two runs' errors just add.
function compare(baseline, candidate) {
    const a = lengthFromHazard(baseline);
    const b = lengthFromHazard(candidate);
    const usingHazard = a !== null && b !== null;

    const fromA = usingHazard ? a : lengthFromDeaths(baseline);
    const fromB = usingHazard ? b : lengthFromDeaths(candidate);
    if (!fromA || !fromB) {
        console.log('\nnot enough data to compare: a run measured neither deaths '
            + 'nor the hazard');
        return;
    }

    const logRatio = Math.log(fromB.length / fromA.length);
    const logSe = Math.hypot(fromA.relative, fromB.relative);
    const ratio = Math.exp(logRatio);

    const stress = usingHazard && baseline.hazard_is_game_length === false;
    console.log(stress
        ? '\ncompared on how much punishment the boards take, which ranks '
            + 'variants but is not a game length'
        : '\ncompared on the mean game length, '
            + `measured ${usingHazard ? 'from the hazard' : 'by counting deaths'}`);
    console.log(`  baseline       ${count(fromA.length)}`);
    console.log(`  candidate      ${count(fromB.length)}`);
    console.log(`  ratio          ${ratio.toFixed(3)}`
        + `  95% CI [${Math.exp(logRatio - Z_95 * logSe).toFixed(3)}, `
        + `${Math.exp(logRatio + Z_95 * logSe).toFixed(3)}]`);

    const significant = Math.abs(logRatio) > Z_95 * logSe;
    const direction = logRatio > 0 ? 'longer' : 'shorter';
    const percent = Math.abs(100 * (ratio - 1)).toFixed(1);
    if (significant) {
        console.log(`\n  the candidate survives ${percent}% ${direction}, `
            + 'and the interval clears 1.000.');
    } else {
        console.log(`\n  no call: the candidate looks ${percent}% ${direction}, `
            + 'but the interval still covers 1.000.');
    }

    // What it would take to settle it. Error falls as 1/sqrt(moves), so the
    // moves needed for a given resolution scale as the square of the ratio of
    // the error we have to the error we need.
    const needed = Z_95 + Z_POWER_80;
    for (const effect of [0.02, 0.05, 0.10, 0.20]) {
        const targetSe = effect / needed;
        const scale = (logSe / targetSe) ** 2;
        const movesEach = Math.max(baseline.moves, candidate.moves) * scale;
        const label = `${(100 * effect).toFixed(0)}%`;
        if (scale <= 1) {
            console.log(`  ${label.padStart(4)} difference: already resolvable at this length`);
        } else {
            console.log(`  ${label.padStart(4)} difference: needs about `
                + `${count(movesEach)} moves per side (${scale.toFixed(1)}x this run)`);
        }
    }

    if (baseline.hazard_pool !== candidate.hazard_pool) {
        console.log(`\n  NOTE: the runs measured the hazard against different deals `
            + `(${baseline.hazard_pool} vs ${candidate.hazard_pool}), so this ratio `
            + 'compares nothing in particular.');
    }
    if (baseline.pool !== candidate.pool || baseline.deal_size !== candidate.deal_size) {
        console.log(`\n  NOTE: the runs used different deals `
            + `(${baseline.pool}/${baseline.deal_size} vs `
            + `${candidate.pool}/${candidate.deal_size}), so this ratio is not `
            + 'a like for like comparison.');
    }
}

const paths = process.argv.slice(2);
if (paths.length === 0 || paths.length > 2) {
    console.error('usage: node tools/survival-report.js <run.json> [<candidate.json>]');
    process.exit(1);
}

const runs = paths.map(readRun);
runs.forEach((run, i) => {
    if (i > 0) {
        console.log('');
    }
    describe(run);
});
if (runs.length === 2) {
    compare(runs[0], runs[1]);
}
