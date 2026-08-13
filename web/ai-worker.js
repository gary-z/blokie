"use strict";
import { blokie, init } from "../engine/js/blokie.js";

// The assist's only job over here is working out where the pieces in hand
// should go. Playing them -- the board, the hand, the score, the animation and
// the sounds -- happens on the main thread, down the same path a dropped piece
// takes, so that there is one of each rather than two.
//
// The search is what needs a worker: it is slow enough to be felt as a stutter
// on the thread drawing the board. This one stays alive between plans, so the
// WASM solver is only ever instantiated once per run of the assist.
self.onmessage = async (e) => {
    await init();
    self.postMessage({
        id: e.data.id,
        plan: blokie.plan(e.data.game, e.data.piece_set),
    });
};
