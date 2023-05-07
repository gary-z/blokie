"use strict";

import * as blokie from './blokie.js';

const sample = blokie.blokie.getFitnessSample();
console.log("Moves: %d, Score: %d", sample.num_moves, sample.score);
