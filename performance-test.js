"use strict";

import { blokie } from './blokie.js';
import { performance } from 'perf_hooks';

const NUM_MOVES = 10000;

const start_time = performance.now();
blokie.getPerformanceSample(NUM_MOVES);
const execution_time_ms = performance.now() - start_time;

console.log(`${NUM_MOVES} moves in ${(execution_time_ms / 1000).toFixed(2)} seconds`);
