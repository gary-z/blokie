#include "solver.h"
#include <algorithm>
#include <cstdint>

namespace {
template<typename Evaluate>
MoveResult makeMoveSimpleImpl(GameState game, PieceSet piece_set,
	Evaluate evaluate) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);
	// Blank slots sort last and need no permutation.
	const int num_pieces = AI::countPieces(piece_set);

	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);

	MoveResult best;

	// Iterators retain the placements needed to replay the winner.
	bool is_first_permutation = true;
	do {
		const auto p0 = piece_set.pieces[0];
		const auto p1 = piece_set.pieces[1];
		const auto p2 = piece_set.pieces[2];
		// A non-clearing suffix commutes, so only its sorted order is needed.
		const bool last_two_reversed = p2 < p1;
		const bool pair_may_be_reversed = !is_first_permutation && p1 < p0;
		const auto states_0 = game.nextStatesClearsFirst(p0);
		const auto last_0 = states_0.end();
		for (auto it_0 = states_0.begin(); it_0 != last_0; ++it_0) {
			const auto after_p0 = *it_0;
			const bool p0_cleared = it_0.didClear();
			const bool pair_test_applies = pair_may_be_reversed && !p0_cleared;
			const auto placement_0 = pair_test_applies ?
				it_0.getPlacement() : BitBoard::empty();
			const auto board_0 = after_p0.getBitBoard();
			const auto states_1 = after_p0.nextStatesClearsFirst(p1);
			const auto last_1 = states_1.end();
			for (auto it_1 = states_1.begin(); it_1 != last_1; ++it_1) {
				const auto after_p1 = *it_1;
				const bool p1_cleared = it_1.didClear();
				// Skip reversed pairs when swapping preserves their clear.
				if (pair_test_applies) {
					if (!p1_cleared) {
						continue;
					}
					const auto board_1 = after_p1.getBitBoard();
					const auto cleared_cells =
						(board_0 | it_1.getPlacement()) - board_1;
					if (!(placement_0 & cleared_cells)) {
						continue;
					}
				}
				const bool leaf_covered_elsewhere = !p1_cleared &&
					(last_two_reversed ||
						(!is_first_permutation && !p0_cleared));
				const auto states_2 = after_p1.nextStates(p2);
				const auto last_2 = states_2.end();
				for (auto it_2 = states_2.begin(); it_2 != last_2; ++it_2) {
					const auto after_p2 = *it_2;
					if (leaf_covered_elsewhere && !it_2.didClear()) {
						continue;
					}
					const auto score = evaluate(after_p2, best.evaluation);
					if (score < best.evaluation) {
						best.evaluation = score;
						best.state = after_p2;
						best.placements[0] = it_0.getPlacement();
						best.placements[1] = it_1.getPlacement();
						best.placements[2] = it_2.getPlacement();
					}
				}
			}
		}
		is_first_permutation = false;
	} while (can_clear_with_2_pieces &&
		std::next_permutation(piece_set.pieces, piece_set.pieces + num_pieces));

	return best;
}
}

MoveResult AI::makeMoveSimple(const EvalWeights weights, GameState game,
	PieceSet piece_set) {
	return makeMoveSimpleImpl(game, piece_set,
		[weights](GameState state, uint64_t max) {
			return state.simpleEval(weights, max);
		});
}

MoveResult AI::makeMoveSimpleDefault(GameState game, PieceSet piece_set) {
	return makeMoveSimpleImpl(game, piece_set,
		[](GameState state, uint64_t max) {
			return state.simpleEvalDefault(max);
		});
}

bool AI::tripleFits(GameState game, PieceSet piece_set) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);
	do {
		for (const auto after_p0 : game.nextStates(piece_set.pieces[0])) {
			for (const auto after_p1 : after_p0.nextStates(piece_set.pieces[1])) {
				for (const auto after_p2 : after_p1.nextStates(piece_set.pieces[2])) {
					(void)after_p2;
					return true;
				}
			}
		}
	} while (std::next_permutation(piece_set.pieces, piece_set.pieces + 3));
	return false;
}

int AI::countPieces(const PieceSet &piece_set) {
	int num_pieces = 3;
	while (num_pieces > 0 &&
		piece_set.pieces[num_pieces - 1].isEmpty()) {
		num_pieces--;
	}
	return num_pieces;
}

bool AI::canClearWith2PiecesOrFewer(GameState game, PieceSet piece_set) {
	// Determine if we need to check permutations.
	for (int i = 0; i < 3; ++i) {
		const auto p0 = piece_set.pieces[i];
		const auto states_0 = game.nextStates(p0);
		const auto last_0 = states_0.end();
		for (auto it_0 = states_0.begin(); it_0 != last_0; ++it_0) {
			const auto after_p0 = *it_0;
			// A piece that clears on its own counts, whether or not any of the
			// other two still fit afterwards. Leaving this to the inner loop
			// would miss the case where the clear is the only thing that makes
			// room, but nothing is left that fits in it.
			if (it_0.didClear()) {
				return true;
			}
			for (int j = 0; j < 3; ++j) {
				if (i == j) {
					continue;
				}
				const auto p1 = piece_set.pieces[j];
				const auto states_1 = after_p0.nextStates(p1);
				const auto last_1 = states_1.end();
				for (auto it_1 = states_1.begin(); it_1 != last_1; ++it_1) {
					(void)*it_1;
					if (it_1.didClear()) {
						return true;
					}
				}
			}
		}
	}
	return false;
}
