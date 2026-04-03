"use strict";
import { blokie, init } from "../engine/blokie.js";

self.onmessage = async (e) => {
    await init();

    const game_state = e.data.game_state;

    // Compute AI moves and post them back immediately.
    // The main thread controls pacing via animation speed.
    function computeNextMoves() {
        if (game_state.queued_game_states.length > 0) {
            // Still have queued states to consume — nothing to do
            return;
        }
        game_state.queued_game_states = blokie.getAIMove(game_state.game, game_state.piece_set).new_game_states;
        game_state.game.previous_piece_placement = blokie.getEmptyPiece();
    }

    computeNextMoves();
    self.postMessage({
        game_state: game_state,
        id: e.data.id,
    });
    self.close();
}
