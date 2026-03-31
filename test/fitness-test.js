"use strict";

import { blokie, initPromise } from '../engine/blokie.js';

await initPromise;
const sample = blokie.getFitnessSample();
console.log("Moves: %d, Score: %d", sample.num_moves, sample.score);
