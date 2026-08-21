#include "solver.h"
#include <algorithm>
#include <cstdint>

// ====== AI
namespace {
template<typename Evaluate>
MoveResult makeMoveSimpleImpl(GameState game, PieceSet piece_set,
	Evaluate evaluate) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);
	// Blank slots sort to the end and are placed by doing nothing, so where
	// they fall in the order cannot change a board. Permuting only the pieces
	// that are really there keeps a two piece hand to two orderings.
	const int num_pieces = AI::countPieces(piece_set);

	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);

	MoveResult best;

	// The three levels below are walked with explicit iterators rather than a
	// range-for because a winning candidate needs the placement each level is
	// holding, and only the iterator knows it. Reading one costs a shift, so they
	// are read where a winner is recorded rather than at every node.
	bool is_first_permutation = true;
	do {
		const auto p0 = piece_set.pieces[0];
		const auto p1 = piece_set.pieces[1];
		const auto p2 = piece_set.pieces[2];
		// Does this ordering play the last two pieces back to front? Whatever
		// the first piece did, a pair of placements after it that neither
		// clears lands on the same board played either way round, and the
		// ordering that plays them the right way round is one the loop also
		// walks -- the same argument as the pair test below, one level down
		// and with no interest in what the first piece did. Sorted pieces put
		// p1 before p2 on the first ordering, so this never fires there.
		const bool last_two_reversed = p2 < p1;
		// Whether the pair test below can fire at all on this ordering, which
		// is a fact about the ordering rather than about any placement in it.
		const bool pair_may_be_reversed = !is_first_permutation && p1 < p0;
		const auto states_0 = game.nextStatesClearsFirst(p0);
		const auto last_0 = states_0.end();
		for (auto it_0 = states_0.begin(); it_0 != last_0; ++it_0) {
			const auto after_p0 = *it_0;
			const bool p0_cleared = it_0.didClear();
			// Only the reversed-pair test reads it, and only when the first
			// placement did not clear, so it costs a shift on the orderings
			// that can use it and nothing on the rest.
			const bool pair_test_applies = pair_may_be_reversed && !p0_cleared;
			const auto placement_0 = pair_test_applies ?
				it_0.getPlacement() : BitBoard::empty();
			const auto board_0 = after_p0.getBitBoard();
			const auto states_1 = after_p0.nextStatesClearsFirst(p1);
			const auto last_1 = states_1.end();
			for (auto it_1 = states_1.begin(); it_1 != last_1; ++it_1) {
				const auto after_p1 = *it_1;
				const bool p1_cleared = it_1.didClear();
				// Nothing cleared, so these two placements land on the same
				// board played the other way round -- and the other way round
				// is an ordering the loop also walks, in which p0 and p1 are
				// the right way up and this test does not fire. Skip the half
				// of those pairs that are back to front.
				//
				// A clear on the second placement need not stop that. What
				// makes the swap work is that the second piece completes the
				// same lines wherever it is played, and it does whenever the
				// cleared cells owe nothing to the first placement: the lines
				// were already full without it, so the second piece played
				// first completes exactly those lines and no others -- a line
				// the pair completes together needs a cell from both, and
				// there is none. Clearing them leaves the board the other
				// order reaches before the first piece is even down, and
				// adding the first piece to it is the same union either way.
				//
				// Never on the first ordering, which the third-level test below
				// takes as having seen every board no clear can move. Walking
				// the orderings in sorted order already gives that -- p1 < p0
				// cannot hold on the first one -- so this says out loud what
				// the walk order would otherwise be quietly relied on for.
				// Anything that reorders the walk has to keep it: a first
				// ordering that skipped half its pairs would take those boards
				// down with it, which is what "Most placements first" did.
				// Ordering the hand by placement count has since been measured
				// properly, both ways round and at both ends of the hand, and
				// is worth less than recompiling the same walk moves it:
				// docs/piece-ordering.md.
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
				// Whether a leaf under this pair is one some other ordering is
				// already responsible for, leaving the third level to ask only
				// whether it cleared. Both reasons need the second placement
				// not to have cleared, so neither can fire once it has.
				const bool leaf_covered_elsewhere = !p1_cleared &&
					(last_two_reversed ||
						(!is_first_permutation && !p0_cleared));
				const auto states_2 = after_p1.nextStates(p2);
				const auto last_2 = states_2.end();
				for (auto it_2 = states_2.begin(); it_2 != last_2; ++it_2) {
					const auto after_p2 = *it_2;
					// Nothing cleared since the board this pair reached, so
					// this board is that one plus two disjoint placements
					// however they were ordered, and the ordering named above
					// reaches it. When that ordering is the first one, which
					// starts from sorted pieces, all three placements are
					// disjoint and it walks every one of these boards.
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
