"use strict";

import { init } from '../blokie.js';
import { fitnessSample } from './harness.js';

await init();
const sample = fitnessSample();
console.log("Moves: %d, Score: %d", sample.num_moves, sample.score);
