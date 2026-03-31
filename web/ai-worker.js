"use strict";
import { blokie } from "../engine/blokie.js";

self.onmessage = (e) => {
    const game_state = e.data.game_state;

    // Returns: { is_over, new_piece_set }
    function aiPlayGame() {
        if (game_state.queued_game_states.length === 0) {
            let new_piece_set = false;
            if (game_state.piece_set.every(p => blokie.isEmpty(p))) {
                game_state.piece_set = blokie.getRandomPieceSet();
                new_piece_set = true;
            }
            game_state.queued_game_states = blokie.getAIMove(game_state.game, game_state.piece_set).new_game_states;
            game_state.game.previous_piece_placement = blokie.getEmptyPiece();
            return { is_over: false, new_piece_set };
        }
        if (blokie.isOver(game_state.queued_game_states[0])) {
            return { is_over: true, new_piece_set: false };
        }

        const new_game_state = game_state.queued_game_states.shift();
        const piece_used = new_game_state.previous_piece;
        const used_piece_index = game_state.piece_set.indexOf(piece_used);
        if (used_piece_index >= 0) {
            game_state.piece_set[used_piece_index] = blokie.getEmptyPiece();
        }
        game_state.previous_game_state = game_state.game;
        game_state.game = new_game_state;
        return { is_over: false, new_piece_set: false };
    }

    function postState() {
        self.postMessage({
            game_state: game_state,
            id: e.data.id,
        });
    }

    if (e.data.delay_ms === 0) {
        // If there is no delay, don't do intervals.
        let is_over = false;
        do {
            const result = aiPlayGame();
            is_over = result.is_over;
            postState();
        } while (!is_over);
        self.close();
    } else {
        setInterval(() => {
            const result = aiPlayGame();
            // When a new piece set was generated, post the state showing all
            // three pieces, then immediately play the first move so there is
            // no extra interval of dead time.
            if (result.new_piece_set) {
                postState();
                const result2 = aiPlayGame();
                postState();
                if (result2.is_over) {
                    self.close();
                }
            } else {
                postState();
                if (result.is_over) {
                    self.close();
                }
            }
        }, e.data.delay_ms);
    }
}