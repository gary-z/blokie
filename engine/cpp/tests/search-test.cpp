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
		// The search takes its first ordering as having reached every board no
		// clear can move, so that ordering is the one that must not skip
		// anything. Walking the hand in sorted order used to guarantee it. The
		// same position is in test/engine/enumeration-test.js, but that one only
		// runs against a rebuilt .wasm; this is the C++ the weights are trained
		// against, and it is where the ordering rule lives.
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

// What the search would settle on with every one of its pruning rules switched
// off: all six orderings walked, no back-to-front pair skipped, no board
// dropped for being one an earlier ordering is argued to have reached.
//
// Unlike the reference above this shares nextStates and simpleEval with the
// search, which the placement sweeps in this file and eval-test check against
// independent oracles. What it isolates is the pruning, and the pruning is the
// part that is an argument rather than a calculation: one rule decides which
// ordering is searched in full and another decides which pairs are redundant,
// and if the two stop agreeing about the same ordering, the boards each thought
// the other was covering are reached by neither.
struct UnprunedSearch {
	bool any_line_of_play = false;
	uint64_t best = std::numeric_limits<uint64_t>::max();
	// The best board no clear can move: three disjoint placements, so every
	// ordering reaches it and only one of them is meant to score it. These are
	// the boards the rules above argue about, and the only ones they can lose.
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

// Open boards, which is what the engine spends a game on and what the sweep
// above cannot reach: at 5 to 7 squares in 8 a clear is nearly always there for
// the taking, so the best board almost always has one and the boards no clear
// can move never decide anything. On a quarter-full board a hand often plays
// out without completing a line at all, and then the whole move rests on the
// pruning agreeing with itself.
void testSearchPruningOnOpenBoards() {
	const auto weights = EvalWeights::getDefault();
	test::Random random(0xF1B5C0DEULL);
	int playable = 0;
	int decided_by_a_board_no_clear_can_move = 0;
	for (int sample = 0; sample < 240; ++sample) {
		const auto board = test::clearCompletedLines(random.board(2));
		// The smallest pieces fit almost everywhere on a board this open, which
		// costs the unpruned reference a great deal and tests nothing extra.
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
		if (reference.best_no_clear_can_move == reference.best &&
			AI::canClearWith2PiecesOrFewer(GameState(board), pieces)) {
			++decided_by_a_board_no_clear_can_move;
		}
		test::require(move.evaluation == reference.best,
			context + ": search settled for " + std::to_string(move.evaluation) +
			", searching the same hand without pruning finds " +
			std::to_string(reference.best));
	}

	test::require(playable >= 200,
		"open board sweep should be mostly playable hands");
	// Without positions of this kind the sweep passes whatever the pruning does,
	// because every board it could drop was worse than one it kept anyway.
	test::require(decided_by_a_board_no_clear_can_move >= 5,
		"open board sweep should include hands that could have cleared early but "
		"whose best board no clear can move");
}

// Boards where a line is nearly full, which is where the two rules that let a
// clear through have anything to do. Both argue that a placement which cleared
// can be played elsewhere in the order and clear the same lines: one takes a
// pair whose second placement cleared without using a cell of the first, the
// other takes the last two placements when neither cleared and the first one
// did. Neither can fire on a board too open to complete a line, so the sweep
// above passes whatever they do.
//
// The lines here are full but for two cells, so a clear usually arrives on the
// second placement of a pair and needs a cell the first one put down. That is
// the case the extended pair test has to decide correctly, and the case a
// board with one-cell gaps never produces.
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
	// Without these the sweep is the open-board sweep with a different seed:
	// one ordering is searched at all, and no rule that lets a clear through
	// can fire.
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
