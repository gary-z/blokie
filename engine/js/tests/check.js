"use strict";

// What every test file in this repository was writing out for itself: an
// assertion that prints a line and remembers whether it held, and the summary
// at the end that turns those lines into an exit code.
//
// There is no framework here on purpose. A test is a script that runs top to
// bottom and says what it found, which is why they can be run with a bare
// `node path/to/test.js` and read like prose. All this does is stop six files
// from each keeping their own copy of the counter.

let checks = 0;
let failures = 0;

/**
 * One assertion. Prints either way -- a passing run is a readable list of what
 * was checked -- and returns whether it held, for the caller that has more to
 * say only when it did.
 * @type {(condition: boolean, description: string) => boolean}
 */
function check(condition, description) {
    checks++;
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return false;
    }
    console.log("ok - %s", description);
    return true;
}

/**
 * One assertion inside a group: what has to hold, and what to call it if it
 * does not. Named because every `want` passed to checkAll is one of these, and
 * annotating checkAll is what gives all of them their types.
 * @typedef {(condition: boolean, detail: string) => void} Want
 */

/**
 * A whole group of assertions reported as one line, so a passing run stays
 * readable while a failure still says which case broke.
 * @type {(description: string, body: (want: Want) => void) => void}
 */
function checkAll(description, body) {
    /** @type {string[]} */
    const broken = [];
    body((condition, detail) => {
        if (!condition) {
            broken.push(detail);
        }
    });
    if (broken.length === 0) {
        check(true, description);
        return;
    }
    check(false, `${description} (${broken.length} failed, first: ${broken[0]})`);
}

/**
 * A value the check below it is built on, which has to be there for that check
 * to mean anything -- a fixture, or the placement an assertion compares
 * against. Throws rather than handing back null, so a fixture that stopped
 * working says so here instead of failing further down as a null dereference.
 *
 * What is under test is asserted with `check`, never with this.
 * @template T
 * @param {T | null} value
 * @param {string} what
 * @returns {T}
 */
function must(value, what) {
    if (value === null) {
        throw new Error(`${what} should not have been null`);
    }
    return value;
}

/**
 * How many checks have failed so far. For a sweep that reports one line for
 * many cases: take this before it starts, and assert it has not moved.
 * @type {() => number}
 */
function failureCount() {
    return failures;
}

/**
 * The line a run ends on, and the exit code that goes with it. `what` names
 * the suite, so a CI log with six of these in it says which one broke.
 *
 * Returns rather than exiting when everything passed: an explicit exit(0) can
 * cut off output still on its way to a pipe, and there is nothing left to do
 * by then anyway.
 * @type {(what: string) => void}
 */
function finish(what) {
    if (failures > 0) {
        console.error(`\n${failures} of ${checks} ${what} checks failed`);
        process.exit(1);
    }
    console.log(`\nall ${checks} ${what} checks passed`);
}

export { check, checkAll, must, failureCount, finish };
