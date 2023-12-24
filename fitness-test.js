"use strict";

import { blokie } from './blokie.js';

const sample = blokie.getFitnessSample();
console.log("Moves: %d, Score: %d", sample.num_moves, sample.score);
