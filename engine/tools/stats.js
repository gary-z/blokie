"use strict";

// The statistics the comparison tools share: a sample mean and variance, a
// seeded RNG, and the two pieces a bootstrap is built out of.
//
// The RNG is here for the same reason the parsing is. A bootstrap is only
// reproducible if everyone draws from the same stream, and two copies of a
// generator that differ by a constant are two tools that quietly disagree
// about what "--seed 42" means.

/** @type {(xs: readonly number[]) => number} */
function mean(xs) {
    return xs.reduce((sum, x) => sum + x, 0) / xs.length;
}

// Sample variance, over n - 1: these are estimates drawn from chains that were
// run, not the whole population of chains that could have been.
/** @type {(xs: readonly number[]) => number} */
function variance(xs) {
    const m = mean(xs);
    return xs.reduce((sum, x) => sum + (x - m) ** 2, 0) / (xs.length - 1);
}

// A small, fast, seeded generator. Nothing here needs cryptographic quality --
// what it needs is to give the same answer twice.
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

// One bootstrap resample: n draws with replacement, averaged.
/** @type {(xs: readonly number[], random: () => number) => number} */
function resampledMean(xs, random) {
    let sum = 0;
    for (let i = 0; i < xs.length; i++) {
        sum += xs[Math.floor(random() * xs.length)];
    }
    return sum / xs.length;
}

// Takes an ArrayLike rather than an array: a bootstrap sorts its resamples in
// a Float64Array, and everything this reads is length and indexing.
/** @type {(sorted: ArrayLike<number>, p: number) => number} */
function quantile(sorted, p) {
    const index = p * (sorted.length - 1);
    const lo = Math.floor(index);
    const hi = Math.ceil(index);
    const fraction = index - lo;
    return sorted[lo] * (1 - fraction) + sorted[hi] * fraction;
}

export { mean, variance, mulberry32, resampledMean, quantile };
