#include "solver.h"
#include "test-support.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

void testAllStandardPiecePlacements() {
	const auto empty = BitBoard::empty();
	for (int piece_index = 0; piece_index < Piece::NUM_PIECES; ++piece_index) {
		const auto indexed_piece = Piece::byIndex(piece_index);
		test::requireSamePlacements(empty, indexed_piece,
			"standard piece " + std::to_string(piece_index) + " on empty board");

		// Piece(BitBoard) is how WASM inputs reach the solver. It should identify
		// a canonical piece and take the same precomputed path as byIndex().
		const auto reconstructed_piece = Piece(indexed_piece.getBitBoard());
		const auto indexed = test::actualPlacements(GameState(empty), indexed_piece);
		const auto reconstructed = test::actualPlacements(GameState(empty),
			reconstructed_piece);
		test::require(indexed.size() == reconstructed.size(),
			"reconstructed piece placement count " + std::to_string(piece_index));
		for (size_t placement = 0; placement < indexed.size(); ++placement) {
			test::require(indexed[placement].placement ==
				reconstructed[placement].placement &&
				indexed[placement].board == reconstructed[placement].board,
				"reconstructed piece differs at piece " +
				std::to_string(piece_index));
		}
	}
}

void testRandomStandardPiecePlacements() {
	test::Random random(0x51A7E5ULL);
	for (int sample = 0; sample < 120; ++sample) {
		// Include valid game boards and arbitrary bit patterns. The latter lock
		// down nextStates' documented clear-what-it-finds behavior as well as
		// stressing simultaneous rows, columns, and cubes.
		auto board = random.board(random.below(9));
		if ((sample & 1) == 0) {
			board = test::clearCompletedLines(board);
		}
		for (int piece_index = 0; piece_index < Piece::NUM_PIECES; ++piece_index) {
			test::requireSamePlacements(board, Piece::byIndex(piece_index),
				"random board " + std::to_string(sample) + ", piece " +
				std::to_string(piece_index));
		}
	}
}

void testGenericAndSentinelPieces() {
	const std::array<Piece, 5> pieces = {
		Piece(),
		Piece(test::square(0, 0) | test::square(0, 2) | test::square(2, 1)),
		Piece(test::square(0, 0) | test::square(5, 8) | test::square(6, 0)),
		Piece(test::square(1, 1) | test::square(7, 7)),
		Piece(BitBoard::full()),
	};
	test::Random random(0xA8B17A4EULL);
	for (int sample = 0; sample < 80; ++sample) {
		const auto board = random.board(random.below(9));
		for (size_t piece = 0; piece < pieces.size(); ++piece) {
			test::requireSamePlacements(board, pieces[piece],
				"generic piece " + std::to_string(piece) + ", board " +
				std::to_string(sample));
		}
	}
}

BitBoard resultForPlacement(BitBoard board, Piece piece, BitBoard wanted) {
	for (const auto &candidate : test::actualPlacements(GameState(board), piece)) {
		if (candidate.placement == wanted) {
			return candidate.board;
		}
	}
	throw test::Failure("wanted placement was not legal");
}

void testParallelLineClearCases() {
	const Piece one_square = Piece::byIndex(0);
	const std::array<BitBoard, 5> completions = {
		BitBoard::row(6),
		BitBoard::column(4),
		BitBoard::cube(2, 2),
		BitBoard::row(4) | BitBoard::column(4),
		BitBoard::row(4) | BitBoard::column(4) | BitBoard::cube(1, 1),
	};
	const std::array<BitBoard, 5> final_squares = {
		test::square(6, 4), test::square(4, 4), test::square(7, 7),
		test::square(4, 4), test::square(4, 4),
	};
	for (size_t index = 0; index < completions.size(); ++index) {
		const auto before = completions[index] - final_squares[index];
		const auto actual = resultForPlacement(before, one_square,
			final_squares[index]);
		const auto expected = test::clearCompletedLines(before | final_squares[index]);
		test::require(actual == expected,
			"parallel clear fixture " + std::to_string(index));
		if (index == completions.size() - 1) {
			test::require(actual == BitBoard::empty(),
				"overlapping row, column, and cube all clear");
		}
	}
}

void testClearingStatesComeFirst() {
	const auto maximum_placements = GameState(BitBoard::empty())
		.nextStatesClearsFirst(Piece::byIndex(0));
	test::require(maximum_placements.size() == 81,
		"clears-first fixed buffer capacity");

	const auto missing = test::square(3, 4);
	const auto board = BitBoard::row(3) - missing;
	const auto piece = Piece::byIndex(0);
	const auto normal = test::actualPlacements(GameState(board), piece);
	std::vector<BitBoard> expected;
	for (const bool want_clear : {true, false}) {
		for (const auto &placement : normal) {
			const bool cleared = placement.board.count() <
				board.count() + piece.getBitBoard().count();
			if (cleared == want_clear) {
				expected.push_back(placement.board);
			}
		}
	}
	const auto actual_states = GameState(board).nextStatesClearsFirst(piece);
	test::require(actual_states.size() == expected.size(), "clears-first size");
	for (size_t index = 0; index < expected.size(); ++index) {
		test::require(actual_states[index].getBitBoard() == expected[index],
			"clears-first stable order at " + std::to_string(index));
	}
}

// The clears-first buffer keeps an anchor per board rather than the placement
// itself, and rebuilds the bitboard when asked. What is checked here is that
// what comes back out is the placement that produced the board stored beside
// it: a search reports its move from these, so an anchor filed against the
// wrong board would hand back a move that was never searched.
void testClearsFirstPlacements() {
	const auto empty_states = GameState(BitBoard::empty())
		.nextStatesClearsFirst(Piece());
	test::require(empty_states.size() == 1, "a blank slot has one placement");
	test::require(empty_states.begin().getPlacement() == BitBoard::empty(),
		"a blank slot places nothing");

	test::Random random(20260810);
	for (int trial = 0; trial < 200; ++trial) {
		const auto density = random.below(6);
		const auto board = test::clearCompletedLines(random.board(density));
		const auto piece = Piece::byIndex((int)random.below(Piece::NUM_PIECES));
		const auto states = GameState(board).nextStatesClearsFirst(piece);
		const auto context = "trial " + std::to_string(trial);
		for (auto it = states.begin(), end = states.end(); it != end; ++it) {
			const auto placement = it.getPlacement();
			test::require(!(placement & board),
				context + ": clears-first placement overlaps the board");
			test::require(placement.count() == piece.getBitBoard().count(),
				context + ": clears-first placement is not the whole piece");
			test::require((*it).getBitBoard() ==
				test::clearCompletedLines(board | placement),
				context + ": clears-first placement does not produce its board");
		}
	}
}

// A search reports the placements that reach the board it settled on, in the
// order it played them. JS replays exactly that order to work out what the move
// scores, so an order that did not fit, or that landed somewhere else, would be
// a move it could not play.
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

void testClearingPlacementsOnly() {
	// nextStatesThatClear exists so the search can skip building placements it
	// would discard, which is only sound if it drops exactly the ones that
	// clear nothing. Check it against the placements themselves, over boards
	// crowded enough for lines to be within reach of a piece.
	test::Random random(0x0C1EA125ULL);
	size_t total_clearing = 0;
	for (int sample = 0; sample < 120; ++sample) {
		const auto board = test::clearCompletedLines(random.board(4 + random.below(4)));
		for (int piece_index = 0; piece_index < Piece::NUM_PIECES; ++piece_index) {
			const auto piece = Piece::byIndex(piece_index);
			const auto context = "random board " + std::to_string(sample) +
				", piece " + std::to_string(piece_index);

			const auto expected_count = board.count() + piece.getBitBoard().count();
			std::vector<BitBoard> expected;
			for (const auto &placement : test::actualPlacements(GameState(board), piece)) {
				if (placement.board.count() < expected_count) {
					expected.push_back(placement.board);
				}
			}

			std::vector<BitBoard> actual;
			auto states = GameState(board).nextStatesThatClear(piece);
			for (auto current = states.begin(), finish = states.end();
				current != finish; ++current) {
				actual.push_back((*current).getBitBoard());
			}

			test::require(actual.size() == expected.size(), context +
				": expected " + std::to_string(expected.size()) +
				" clearing placements, got " + std::to_string(actual.size()) +
				"\n" + test::describe(board));
			for (size_t index = 0; index < expected.size(); ++index) {
				test::require(actual[index] == expected[index], context +
					": clearing placement differs at " + std::to_string(index));
			}
			total_clearing += expected.size();
		}
	}
	// Returning nothing at all would agree with every board that had no
	// clearing placement to find, so say that the corpus had plenty.
	test::require(total_clearing > 1000, "corpus holds " +
		std::to_string(total_clearing) + " clearing placements, too few to test on");
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
	const std::array<Fixture, 3> fixtures = {{
		{test::boardFromJs(6815804, 161655, 99374901),
			PieceSet(Piece(test::boardFromJs(787459, 0, 0)),
				Piece(test::boardFromJs(525319, 0, 0)),
				Piece(test::boardFromJs(1837060, 0, 0))),
			"clear required to fit the deck"},
		{test::boardFromJs(85258932, 102633091, 31642653),
			PieceSet(Piece(test::boardFromJs(514, 0, 0)),
				Piece(test::boardFromJs(262657, 0, 0)),
				Piece(test::boardFromJs(15, 0, 0))),
			"reordered deck reaches best board"},
		{test::boardFromJs(12477989, 12471835, 16671542),
			PieceSet(Piece(test::boardFromJs(525315, 0, 0)),
				Piece(test::boardFromJs(262663, 0, 0)),
				Piece(test::boardFromJs(525315, 0, 0))),
			"duplicate shapes in ordering-sensitive deck"},
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
		"empty deck");
	requireSimpleSearchOptimal(BitBoard::row(2) - test::square(2, 2),
		PieceSet(one, blank, blank), "one-piece deck");
	requireSimpleSearchOptimal(BitBoard::row(2) -
		(test::square(2, 2) | test::square(2, 3)),
		PieceSet(one, two, blank), "two-piece deck");

	auto diagonal_holes = BitBoard::full();
	for (unsigned index = 0; index < 9; ++index) {
		diagonal_holes = diagonal_holes - test::square(index, index);
	}
	requireSimpleSearchOptimal(diagonal_holes,
		PieceSet(Piece::byIndex(32), Piece::byIndex(32), Piece::byIndex(32)),
		"deck that cannot fit");
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
		"random sweep should include enough fully playable decks");
}

void testLookaheadGameOver() {
	auto diagonal_holes = BitBoard::full();
	for (unsigned index = 0; index < 9; ++index) {
		diagonal_holes = diagonal_holes - test::square(index, index);
	}
	const PieceSet pieces(Piece::byIndex(32), Piece::byIndex(32), Piece::byIndex(32));
	const auto result = AI::makeMoveLookahead(EvalWeights::getDefault(),
		GameState(diagonal_holes), pieces);
	test::require(result.isOver(), "lookahead reports a deck that cannot fit");
}

uint64_t referenceLookaheadCost(BitBoard board, const EvalWeights &weights) {
	constexpr uint64_t game_over_penalty =
		std::numeric_limits<uint64_t>::max() / (Piece::NUM_PIECES + 1);
	uint64_t result = 0;
	// The implementation deliberately excludes the 1x1 at index zero from its
	// expectation, to avoid giving every position an overly optimistic escape.
	for (int piece_index = 1; piece_index < Piece::NUM_PIECES; ++piece_index) {
		uint64_t best = std::numeric_limits<uint64_t>::max();
		for (const auto &placement :
			test::referencePlacements(board, Piece::byIndex(piece_index))) {
			best = std::min(best, GameState(placement.board).simpleEval(weights));
		}
		result += best == std::numeric_limits<uint64_t>::max() ?
			game_over_penalty : best;
	}
	return result;
}

void testLookaheadAgainstReference() {
	// One real piece and two blank slots keep the outer exhaustive search small,
	// while the oracle still evaluates every one of the 46 modeled next pieces.
	auto diagonal_holes = BitBoard::full();
	for (unsigned index = 0; index < 9; ++index) {
		diagonal_holes = diagonal_holes - test::square(index, index);
	}
	const auto one = Piece::byIndex(0);
	const PieceSet pieces(one, Piece(), Piece());
	std::set<BitBoard> reachable;
	collectReachableBoards(diagonal_holes, {one}, reachable);
	test::require(!reachable.empty(), "lookahead fixture has reachable boards");

	const auto weights = EvalWeights::getDefault();
	uint64_t expected = std::numeric_limits<uint64_t>::max();
	for (const auto &candidate : reachable) {
		expected = std::min(expected, referenceLookaheadCost(candidate, weights));
	}
	const auto actual = AI::makeMoveLookahead(weights,
		GameState(diagonal_holes), pieces).getBitBoard();
	test::require(reachable.find(actual) != reachable.end(),
		"lookahead result is reachable");
	test::require(referenceLookaheadCost(actual, weights) == expected,
		"lookahead minimizes the independent next-piece expectation");
}

} // namespace

int main() {
	return test::run({
		{"all standard-piece placements", testAllStandardPiecePlacements},
		{"random standard-piece placements", testRandomStandardPiecePlacements},
		{"generic and sentinel pieces", testGenericAndSentinelPieces},
		{"parallel line-clear cases", testParallelLineClearCases},
		{"clearing states ordered first", testClearingStatesComeFirst},
		{"clearing placements only", testClearingPlacementsOnly},
		{"clears-first placements", testClearsFirstPlacements},
		{"search placements replay to its board", testMoveResultPlacementsReplay},
		{"two-piece clear detection", testCanClearWithTwoPieces},
		{"held-piece counting", testPieceCounting},
		{"ordering-sensitive searches", testKnownOrderingSensitiveSearches},
		{"blank and game-over searches", testBlankAndGameOverSearches},
		{"random search against brute force", testRandomSearchAgainstBruteForce},
		{"lookahead game-over handling", testLookaheadGameOver},
		{"lookahead against brute force", testLookaheadAgainstReference},
	});
}
