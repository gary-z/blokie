"use strict";

import { blokie, init } from '../engine/blokie.js';

await init();
const sample = blokie.getFitnessSample();
console.log("Moves: %d, Score: %d", sample.num_moves, sample.score);
