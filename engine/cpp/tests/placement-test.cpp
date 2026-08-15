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
		Piece(test::square(0, 0) | test::square(4, 8) | test::square(5, 0)),
		Piece(test::square(1, 1) | test::square(5, 7)),
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
			const auto expected_board =
				test::clearCompletedLines(board | placement);
			const bool expected_clear = expected_board.count() <
				board.count() + placement.count();
			test::require(!(placement & board),
				context + ": clears-first placement overlaps the board");
			test::require(placement.count() == piece.getBitBoard().count(),
				context + ": clears-first placement is not the whole piece");
			test::require((*it).getBitBoard() == expected_board,
				context + ": clears-first placement does not produce its board");
			test::require(it.didClear() == expected_clear,
				context + ": clears-first clear flag does not match its board");
		}
	}
}

// A search reports the placements that reach the board it settled on, in the
// order it played them. JS replays exactly that order to work out what the move
// scores, so an order that did not fit, or that landed somewhere else, would be
// a move it could not play.

} // namespace

int main() {
	return test::run({
		{"testAllStandardPiecePlacements", testAllStandardPiecePlacements},
		{"testRandomStandardPiecePlacements", testRandomStandardPiecePlacements},
		{"testGenericAndSentinelPieces", testGenericAndSentinelPieces},
		{"testParallelLineClearCases", testParallelLineClearCases},
		{"testClearingStatesComeFirst", testClearingStatesComeFirst},
		{"testClearsFirstPlacements", testClearsFirstPlacements}
	});
}
