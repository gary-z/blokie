#include "../solver.h"
#include "test-support.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

void testMoveResultPlacementsReplay() {
	test::Random random(987654321);
	int with_a_move = 0;
	for (int trial = 0; trial < 150; ++trial) {
		const auto density = random.below(7);
		const auto board = test::clearCompletedLines(random.board(density));
		Piece pieces[3];
		for (auto &piece : pieces) {
			piece = Piece::byIndex((int)random.below(Piece::NUM_PIECES));
		}
		const auto move = AI::makeMoveSimple(EvalWeights::getDefault(),
			GameState(board), PieceSet(pieces[0], pieces[1], pieces[2]));
		const auto context = "trial " + std::to_string(trial);
		if (move.evaluation == std::numeric_limits<uint64_t>::max()) {
			test::require(move.state.getBitBoard() == BitBoard::full(),
				context + ": a search that found nothing is not a game over");
			continue;
		}
		++with_a_move;

		auto current = board;
		for (const auto &placement : move.placements) {
			test::require(!(placement & current),
				context + ": a placement does not fit where it is played");
			current = test::clearCompletedLines(current | placement);
		}
		test::require(current == move.state.getBitBoard(),
			context + ": replaying the placements misses the board searched");
		test::require(move.evaluation ==
			GameState(current).simpleEval(EvalWeights::getDefault()),
			context + ": the evaluation is not the one for that board");
	}
	test::require(with_a_move > 0, "fixture produced searches with a move");
}

bool referenceCanClearWithTwo(BitBoard board, const PieceSet &pieces) {
	for (int first = 0; first < 3; ++first) {
		const auto p0 = pieces.pieces[first];
		const auto after_first_count = board.count() + p0.getBitBoard().count();
		for (const auto &after_p0 : test::referencePlacements(board, p0)) {
			if (after_p0.board.count() < after_first_count) {
				return true;
			}
			for (int second = 0; second < 3; ++second) {
				if (first == second) {
					continue;
				}
				const auto p1 = pieces.pieces[second];
				const auto after_second_count = board.count() +
					p0.getBitBoard().count() + p1.getBitBoard().count();
				for (const auto &after_p1 :
					test::referencePlacements(after_p0.board, p1)) {
					if (after_p1.board.count() < after_second_count) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

void testCanClearWithTwoPieces() {
	const auto one = Piece::byIndex(0);
	const auto two_horizontal = Piece::byIndex(1);
	const Piece blank;

	const std::array<std::pair<BitBoard, PieceSet>, 4> fixtures = {{
		{BitBoard::empty(), PieceSet(one, two_horizontal, blank)},
		{BitBoard::row(0) - test::square(0, 0), PieceSet(one, blank, blank)},
		{BitBoard::row(0) - (test::square(0, 0) | test::square(0, 1)),
			PieceSet(one, one, blank)},
		{BitBoard::column(8) - test::square(8, 8),
			PieceSet(two_horizontal, one, blank)},
	}};
	for (size_t index = 0; index < fixtures.size(); ++index) {
		const auto &[board, pieces] = fixtures[index];
		const auto expected = referenceCanClearWithTwo(board, pieces);
		const auto actual = AI::canClearWith2PiecesOrFewer(GameState(board), pieces);
		test::require(actual == expected,
			"can-clear fixture " + std::to_string(index));
	}

	test::Random random(0xC1EA2ULL);
	for (int sample = 0; sample < 100; ++sample) {
		const auto board = test::clearCompletedLines(random.board(4 + random.below(4)));
		const auto p0 = Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES)));
		const auto p1 = Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES)));
		const auto p2 = Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES)));
		const PieceSet pieces(p0, p1, p2);
		const auto expected = referenceCanClearWithTwo(board, pieces);
		const auto actual = AI::canClearWith2PiecesOrFewer(GameState(board), pieces);
		test::require(actual == expected,
			"can-clear random sample " + std::to_string(sample));
	}
}

void testPieceCounting() {
	const Piece blank;
	const auto one = Piece::byIndex(0);
	const auto two = Piece::byIndex(1);
	const auto three = Piece::byIndex(5);
	std::array<PieceSet, 4> sets = {
		PieceSet(blank, blank, blank), PieceSet(one, blank, blank),
		PieceSet(one, two, blank), PieceSet(one, two, three),
	};
	for (size_t expected = 0; expected < sets.size(); ++expected) {
		std::sort(sets[expected].pieces, sets[expected].pieces + 3);
		test::require(AI::countPieces(sets[expected]) == static_cast<int>(expected),
			"piece count " + std::to_string(expected));
	}
}

void collectReachableBoards(BitBoard board, const std::vector<Piece> &remaining,
	std::set<BitBoard> &result) {
	if (remaining.empty()) {
		result.insert(board);
		return;
	}
	for (size_t selected = 0; selected < remaining.size(); ++selected) {
		auto next_remaining = remaining;
		const auto piece = next_remaining[selected];
		next_remaining.erase(next_remaining.begin() + static_cast<long>(selected));
		for (const auto &placement : test::referencePlacements(board, piece)) {
			collectReachableBoards(placement.board, next_remaining, result);
		}
	}
}

void requireSimpleSearchOptimal(BitBoard board, PieceSet pieces,
	const std::string &context) {
	std::vector<Piece> held;
	for (const auto &piece : pieces.pieces) {
		if (!(piece.getBitBoard() == BitBoard::empty())) {
			held.push_back(piece);
		}
	}
	std::set<BitBoard> reachable;
	collectReachableBoards(board, held, reachable);

	const auto weights = EvalWeights::getDefault();
	const auto actual = AI::makeMoveSimple(weights, GameState(board), pieces)
		.state.getBitBoard();
	const auto default_actual =
		AI::makeMoveSimpleDefault(GameState(board), pieces).state.getBitBoard();
	test::require(default_actual == actual,
		context + ": constexpr default search differs from generic search");
	if (reachable.empty()) {
		test::require(actual == BitBoard::full(), context + ": expected game over");
		return;
	}

	uint64_t expected_score = std::numeric_limits<uint64_t>::max();
	for (const auto &candidate : reachable) {
		expected_score = std::min(expected_score,
			GameState(candidate).simpleEval(weights));
	}
	test::require(reachable.find(actual) != reachable.end(),
		context + ": returned board is not reachable");
	const auto actual_score = GameState(actual).simpleEval(weights);
	test::require(actual_score == expected_score,
		context + ": expected score " + std::to_string(expected_score) +
		", got " + std::to_string(actual_score) + " across " +
		std::to_string(reachable.size()) + " reachable boards");
}

void testKnownOrderingSensitiveSearches() {
	struct Fixture {
		BitBoard board;
		PieceSet pieces;
		const char *name;
	};
	const std::array<Fixture, 4> fixtures = {{
		{test::boardFromJs(6815804, 161655, 99374901),
			PieceSet(Piece(test::boardFromJs(787459, 0, 0)),
				Piece(test::boardFromJs(525319, 0, 0)),
				Piece(test::boardFromJs(1837060, 0, 0))),
			"clear required to fit the hand"},
		{test::boardFromJs(85258932, 102633091, 31642653),
			PieceSet(Piece(test::boardFromJs(514, 0, 0)),
				Piece(test::boardFromJs(262657, 0, 0)),
				Piece(test::boardFromJs(15, 0, 0))),
			"reordered hand reaches best board"},
		{test::boardFromJs(12477989, 12471835, 16671542),
			PieceSet(Piece(test::boardFromJs(525315, 0, 0)),
				Piece(test::boardFromJs(262663, 0, 0)),
				Piece(test::boardFromJs(525315, 0, 0))),
			"duplicate shapes in ordering-sensitive hand"},
		{test::boardFromJs(6316088, 786433, 786432),
			PieceSet(Piece(test::boardFromJs(1052164, 0, 0)),
				Piece(test::boardFromJs(1052164, 0, 0)),
				Piece(test::boardFromJs(1539, 0, 0))),
			"first ordering must not skip its back-to-front pairs"},
	}};
	for (const auto &fixture : fixtures) {
		requireSimpleSearchOptimal(fixture.board, fixture.pieces, fixture.name);
	}
}

void testBlankAndGameOverSearches() {
	const Piece blank;
	const auto one = Piece::byIndex(0);
	const auto two = Piece::byIndex(1);
	requireSimpleSearchOptimal(BitBoard::empty(), PieceSet(blank, blank, blank),
		"empty hand");
	requireSimpleSearchOptimal(BitBoard::row(2) - test::square(2, 2),
		PieceSet(one, blank, blank), "one-piece hand");
	requireSimpleSearchOptimal(BitBoard::row(2) -
		(test::square(2, 2) | test::square(2, 3)),
		PieceSet(one, two, blank), "two-piece hand");

	auto diagonal_holes = BitBoard::full();
	for (unsigned index = 0; index < 9; ++index) {
		diagonal_holes = diagonal_holes - test::square(index, index);
	}
	requireSimpleSearchOptimal(diagonal_holes,
		PieceSet(Piece::byIndex(32), Piece::byIndex(32), Piece::byIndex(32)),
		"hand that cannot fit");
}

void testRandomSearchAgainstBruteForce() {
	test::Random random(0xB2A7EF02CEULL);
	int positions_with_complete_play = 0;
	for (int sample = 0; sample < 60; ++sample) {
		const auto board = test::clearCompletedLines(random.board(5 + random.below(3)));
		// Larger pieces keep exhaustive reference search modest while exercising
		// all the ordering and clear-dependent pruning in the real search.
		const auto p0 = Piece::byIndex(13 + static_cast<int>(random.below(34)));
		const auto p1 = Piece::byIndex(13 + static_cast<int>(random.below(34)));
		const auto p2 = Piece::byIndex(13 + static_cast<int>(random.below(34)));
		const PieceSet pieces(p0, p1, p2);

		std::set<BitBoard> reachable;
		collectReachableBoards(board, {p0, p1, p2}, reachable);
		positions_with_complete_play += reachable.empty() ? 0 : 1;
		requireSimpleSearchOptimal(board, pieces,
			"random search sample " + std::to_string(sample));
	}
	test::require(positions_with_complete_play >= 10,
		"random sweep should include enough fully playable hands");
}

// Exhaustive reference with all ordering pruning disabled.
struct UnprunedSearch {
	bool any_line_of_play = false;
	uint64_t best = std::numeric_limits<uint64_t>::max();
	uint64_t best_no_clear_can_move = std::numeric_limits<uint64_t>::max();
};

UnprunedSearch searchWithoutPruning(BitBoard board, const PieceSet &pieces) {
	const auto weights = EvalWeights::getDefault();
	const int untouched_count = board.count() +
		pieces.pieces[0].getBitBoard().count() +
		pieces.pieces[1].getBitBoard().count() +
		pieces.pieces[2].getBitBoard().count();
	UnprunedSearch result;
	int order[3] = {0, 1, 2};
	do {
		for (const auto after_p0 :
			GameState(board).nextStates(pieces.pieces[order[0]])) {
			for (const auto after_p1 : after_p0.nextStates(pieces.pieces[order[1]])) {
				for (const auto after_p2 :
					after_p1.nextStates(pieces.pieces[order[2]])) {
					result.any_line_of_play = true;
					const auto score = after_p2.simpleEval(weights);
					result.best = std::min(result.best, score);
					if (after_p2.getBitBoard().count() == untouched_count) {
						result.best_no_clear_can_move =
							std::min(result.best_no_clear_can_move, score);
					}
				}
			}
		}
	} while (std::next_permutation(order, order + 3));
	return result;
}

// Open-board coverage for ordering pruning.
void testSearchPruningOnOpenBoards() {
	const auto weights = EvalWeights::getDefault();
	test::Random random(0xF1B5C0DEULL);
	int playable = 0;
	int decided_by_a_board_no_clear_can_move = 0;
	int could_clear_early = 0;
	int best_was_a_board_no_clear_can_move = 0;
	for (int sample = 0; sample < 960; ++sample) {
		const auto board = test::clearCompletedLines(random.board(2));
		const PieceSet pieces(
			Piece::byIndex(13 + static_cast<int>(random.below(Piece::NUM_PIECES - 13))),
			Piece::byIndex(13 + static_cast<int>(random.below(Piece::NUM_PIECES - 13))),
			Piece::byIndex(13 + static_cast<int>(random.below(Piece::NUM_PIECES - 13))));
		const auto context = "open board sample " + std::to_string(sample);

		const auto reference = searchWithoutPruning(board, pieces);
		const auto move = AI::makeMoveSimple(weights, GameState(board), pieces);
		if (!reference.any_line_of_play) {
			test::require(move.evaluation == std::numeric_limits<uint64_t>::max(),
				context + ": search found a move where nothing can be played");
			continue;
		}
		++playable;
		const bool could_clear = AI::canClearWith2PiecesOrFewer(GameState(board), pieces);
		const bool best_is_untouched =
			reference.best_no_clear_can_move == reference.best;
		could_clear_early += could_clear ? 1 : 0;
		best_was_a_board_no_clear_can_move += best_is_untouched ? 1 : 0;
		if (best_is_untouched && could_clear) {
			++decided_by_a_board_no_clear_can_move;
		}
		test::require(move.evaluation == reference.best,
			context + ": search settled for " + std::to_string(move.evaluation) +
			", searching the same hand without pruning finds " +
			std::to_string(reference.best));
	}

	test::require(playable >= 800,
		"open board sweep should be mostly playable hands");
	test::require(decided_by_a_board_no_clear_can_move >= 5,
		"open board sweep should include hands that could have cleared early but "
		"whose best board no clear can move -- counted " +
		std::to_string(decided_by_a_board_no_clear_can_move) + " of " +
		std::to_string(playable) + " playable; could clear early " +
		std::to_string(could_clear_early) + ", best was untouched " +
		std::to_string(best_was_a_board_no_clear_can_move));
}

// Near-complete-line coverage for clear-aware pruning.
void testSearchPruningWhereClearsAreAvailable() {
	const auto weights = EvalWeights::getDefault();
	test::Random random(0xC1EA4B0A4DULL);
	int playable = 0;
	int could_clear_early = 0;
	for (int sample = 0; sample < 200; ++sample) {
		auto board = random.board(1);
		for (int line = 0; line < 3; ++line) {
			const unsigned index = random.below(9);
			const bool is_row = random.below(2) == 0;
			const auto full = is_row ?
				BitBoard::row(index) : BitBoard::column(index);
			auto gaps = BitBoard::empty();
			while (gaps.count() < 2) {
				const unsigned along = random.below(9);
				gaps = gaps | (is_row ? test::square(index, along)
					: test::square(along, index));
			}
			board = (board | full) - gaps;
		}
		board = test::clearCompletedLines(board);
		const PieceSet pieces(
			Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES))),
			Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES))),
			Piece::byIndex(static_cast<int>(random.below(Piece::NUM_PIECES))));
		const auto context = "clearing board sample " + std::to_string(sample);

		const auto reference = searchWithoutPruning(board, pieces);
		const auto move = AI::makeMoveSimple(weights, GameState(board), pieces);
		if (!reference.any_line_of_play) {
			test::require(move.evaluation == std::numeric_limits<uint64_t>::max(),
				context + ": search found a move where nothing can be played");
			continue;
		}
		++playable;
		if (AI::canClearWith2PiecesOrFewer(GameState(board), pieces)) {
			++could_clear_early;
		}
		test::require(move.evaluation == reference.best,
			context + ": search settled for " + std::to_string(move.evaluation) +
			", searching the same hand without pruning finds " +
			std::to_string(reference.best));
	}

	test::require(playable >= 150,
		"clearing board sweep should be mostly playable hands");
	test::require(could_clear_early >= 100,
		"clearing board sweep should mostly be hands that can clear early");
}

} // namespace

int main() {
	return test::run({
		{"search placements replay to its board", testMoveResultPlacementsReplay},
		{"two-piece clear detection", testCanClearWithTwoPieces},
		{"held-piece counting", testPieceCounting},
		{"ordering-sensitive searches", testKnownOrderingSensitiveSearches},
		{"blank and game-over searches", testBlankAndGameOverSearches},
		{"random search against brute force", testRandomSearchAgainstBruteForce},
		{"search pruning on open boards", testSearchPruningOnOpenBoards},
		{"search pruning where clears are available",
			testSearchPruningWhereClearsAreAvailable},
	});
}
