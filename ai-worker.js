"use strict";
import { blokie } from "./blokie.js";

self.onmessage = (e) => {
    const game_state = e.data.game_state;

    function aiPlayGame() {
        if (game_state.piece_set.every(p => blokie.isEmpty(p))) {
            game_state.piece_set = blokie.getRandomPieceSet();
            return false;
        }
        if (game_state.queued_game_states.length === 0) {
            if (game_state.piece_set.every(p => blokie.isEmpty(p))) {
                game_state.piece_set = blokie.getRandomPieceSet();
            }
            game_state.queued_game_states = blokie.getAIMove(game_state.game, game_state.piece_set).new_game_states;
            game_state.game.previous_piece_placement = blokie.getEmptyPiece();
            return false;
        }
        if (blokie.isOver(game_state.queued_game_states[0])) {
            return true;
        }

        const new_game_state = game_state.queued_game_states.shift();
        const piece_used = new_game_state.previous_piece;
        const used_piece_index = game_state.piece_set.indexOf(piece_used);
        if (used_piece_index >= 0) {
            game_state.piece_set[used_piece_index] = blokie.getEmptyPiece();
        }
        game_state.previous_game_state = game_state.game;
        game_state.game = new_game_state;
        return false;
    }

    if (e.data.delay_ms === 0) {
        // If there is no delay, don't do intervals.
        let is_over = false;
        do {
            is_over = aiPlayGame();
            self.postMessage({
                game_state: game_state,
                id: e.data.id,
            });
        } while (!is_over);
        self.close();
    } else {
        setInterval(() => {
            const is_over = aiPlayGame();
            self.postMessage({
                game_state: game_state,
                id: e.data.id,
            });

            if (is_over) {
                self.close();
            }
        }, e.data.delay_ms);
    }
}