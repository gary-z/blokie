#include "eval.h"
#include "game.h"
#include "bitboard.h"

#include <algorithm>
#include <cassert>
#include <bit>
#include <limits>
#include <cstdint>

using namespace bitboard_detail;

// How many ways are left to clear.
//
// Off by default: turning it on changes what the engine plays, which means the
// committed WASM and the reference evaluation in tests/eval-test.cpp both have
// to be regenerated, and the numbers below were measured with the other weights
// left at values tuned without it. See docs/clear-opportunity.md.
//
// The evaluation knows which pieces have nowhere left to go -- that is the
// deadly-piece term -- but nothing about how near a row, column or cube is to
// completing. On a crowded board those are different questions: a position can
// have room for every piece and still have no way to clear, and a position with
// no way to clear is a position that only gets fuller.
//
// Measured at 41% longer games, replicated on two seeds, hazard ratio 0.710 with
// 631 deaths against 634 (p about 1e-9). The same penalty made blind to clears
// -- a flat charge for being past the gate -- is 27% *worse* than not having it,
// so the gain is the clear counting and not the extra crowding aversion.
//
// Two things about the shape of it. It is a penalty for the ways that are
// missing rather than a bonus for the ways that exist, because the search prunes
// against a running maximum and a negative term would let a candidate that has
// already exceeded the bound come back under it. And it is gated on occupancy,
// which is what makes it affordable: the mean board carries 18 squares, so the
// enumeration almost never runs, and measured throughput is within about 5% of
// leaving it out.

uint64_t GameState::simpleEvalImpl(EvalWeights weights, BitBoard bb, uint64_t max) {
	uint64_t result = 0;

	// Occupied cubes and squares. Square weights only depend on a cube's
	// category, so count each category together instead of popcounting all nine
	// cubes independently. The individual tests are still needed for the fixed
	// cost of making each cube nonempty.
	const auto center_cube = BitBoard::cube(1, 1) & bb;
	const auto side_squares = (BitBoard::cube(0, 1) | BitBoard::cube(1, 0) |
		BitBoard::cube(1, 2) | BitBoard::cube(2, 1)) & bb;
	const auto corner_squares = (BitBoard::cube(0, 0) | BitBoard::cube(0, 2) |
		BitBoard::cube(2, 0) | BitBoard::cube(2, 2)) & bb;
	const int occupied_side_cubes =
		static_cast<bool>(BitBoard::cube(0, 1) & bb) +
		static_cast<bool>(BitBoard::cube(1, 0) & bb) +
		static_cast<bool>(BitBoard::cube(1, 2) & bb) +
		static_cast<bool>(BitBoard::cube(2, 1) & bb);
	const int occupied_corner_cubes =
		static_cast<bool>(BitBoard::cube(0, 0) & bb) +
		static_cast<bool>(BitBoard::cube(0, 2) & bb) +
		static_cast<bool>(BitBoard::cube(2, 0) & bb) +
		static_cast<bool>(BitBoard::cube(2, 2) & bb);
	result += static_cast<bool>(center_cube) * weights.getOccupiedCenterCube();
	result += center_cube.count() * weights.getOccupiedCenterSquare();
	result += occupied_side_cubes * weights.getOccupiedSideCube();
	result += side_squares.count() * weights.getOccupiedSideSquare();
	result += occupied_corner_cubes * weights.getOccupiedCornerCube();
	result += corner_squares.count() * weights.getOccupiedCornerSquare();
	if (result >= max) {
		return max;
	}

	const auto open = ~bb;

	{
		// Every neighbour a three-square piece can reach, which is everything
		// within two steps. The deadly-piece section below reuses all of them.
		const auto open_left =  open.shiftRight();
		const auto open_2_left = open_left.shiftRight();
		const auto open_right = open.shiftLeft();
		const auto open_2_right = open_right.shiftLeft();
		const auto open_up = open.shiftDown();
		const auto open_2_up = open_up.shiftDown();
		const auto open_down = open.shiftUp();
		const auto open_2_down = open_down.shiftUp();
		const auto open_up_left = open_up.shiftRight();
		const auto open_down_left = open_down.shiftRight();
		const auto open_up_right = open_up.shiftLeft();
		const auto open_down_right = open_down.shiftLeft();

		// How much room an empty square has left: the number of placements of a
		// three-square piece that still cover it. Six pieces -- two bars and four
		// L shapes -- times three placements each, since the square can be any of
		// the piece's three cells, so the count runs 0 to 18.
		//
		// This is one signal in place of four. Jaggedness counted blocked sides,
		// the three-bar fit asked two of these eighteen questions, and cornered
		// and squashed empty counted pairs of blocked sides; all of them were
		// reading the same thing badly. Nothing special happens at the board
		// edge: a placement that would run off the board simply does not fit, so
		// rim and corner squares have fewer ways without a rule saying so.
		//
		// The diagonal staircases are excluded on purpose. They cannot cover 24%
		// of open squares even on a healthy board, so counting them adds noise
		// rather than caution -- measured, in docs/empty-patterns.md.
		const BitBoard ways[18] = {
			// horizontal bar, the square at each of its three cells
			open & open_right & open_2_right,
			open & open_left & open_right,
			open & open_2_left & open_left,
			// vertical bar
			open & open_down & open_2_down,
			open & open_up & open_down,
			open & open_2_up & open_up,
			// L, corner up-left
			open & open_right & open_down,
			open & open_left & open_down_left,
			open & open_up & open_up_right,
			// L, corner up-right
			open & open_right & open_down_right,
			open & open_left & open_down,
			open & open_up_left & open_up,
			// L, corner down-left
			open & open_down & open_down_right,
			open & open_up & open_right,
			open & open_up_left & open_left,
			// L, corner down-right
			open & open_down_left & open_down,
			open & open_up_right & open_right,
			open & open_up & open_left,
		};

		// Add the eighteen masks a bit-plane at a time, so each square ends up
		// holding its own count in five bits. Eighteen needs five planes.
		BitBoard plane[5] = {BitBoard::empty(), BitBoard::empty(),
			BitBoard::empty(), BitBoard::empty(), BitBoard::empty()};
		for (const BitBoard &mask : ways) {
			BitBoard carry = mask;
			for (BitBoard &bit : plane) {
				const BitBoard next = bit & carry;
				bit = bit ^ carry;
				carry = next;
			}
		}

		// A square with `count` ways matches exactly one weight. Cells that are
		// filled hold zero in every plane, so the open mask has to come first.
		for (int count = 0; count <= 18; ++count) {
			const int weight = weights.getThreeWays(count);
			if (weight == 0) {
				continue;
			}
			BitBoard matching = open;
			for (int bit = 0; bit < 5; ++bit) {
				matching = (count >> bit) & 1 ? (matching & plane[bit])
					: (matching - plane[bit]);
			}
			result += (uint64_t)matching.count() * weight;
		}

		if (result >= max) {
			return max;
		}

		const int crowded_blocks = std::max(0, bb.count() - 20);
		int scarce_deadly_placements = 0;
		const auto score_deadly_piece = [&](BitBoard deadly_piece_placement) {
			const int placements = deadly_piece_placement.count();
			if (placements == 0) {
				result += weights.getDeadlyPiece();
			}
			if (crowded_blocks != 0 && placements < 4) {
				scarce_deadly_placements += 4 - placements;
			}
		};

		// 5 bars
		score_deadly_piece(open & open_left & open_2_left & open_right & open_2_right);
		score_deadly_piece(open & open_up & open_2_up & open_down & open_2_down);

		// L
		score_deadly_piece(open & open_up & open_2_up & open_right & open_2_right);
		score_deadly_piece(open & open_up & open_2_up & open_left & open_2_left);
		score_deadly_piece(open & open_down & open_2_down & open_right & open_2_right);
		score_deadly_piece(open & open_down & open_2_down & open_left & open_2_left);

		// T
		score_deadly_piece(open & open_left & open_right & open_down & open_2_down);
		score_deadly_piece(open & open_left & open_right & open_up & open_2_up);
		score_deadly_piece(open & open_up & open_down & open_left & open_2_left);
		score_deadly_piece(open & open_up & open_down & open_right & open_2_right);

		// +
		score_deadly_piece(open & open_left & open_right & open_up & open_down);

		// 3 star
		score_deadly_piece(open & open_down_left & open_up_right);
		score_deadly_piece(open & open_up_left & open_down_right);

		// C
		score_deadly_piece(open & open_up & open_down & open_up_right & open_down_right);
		score_deadly_piece(open & open_up & open_down & open_up_left & open_down_left);
		score_deadly_piece(open & open_left & open_right & open_up_left & open_up_right);
		score_deadly_piece(open & open_left & open_right & open_down_left & open_down_right);

		// The other pieces in a deal can consume a hard piece's last few legal
		// placements. That scarcity only becomes dangerous on a crowded board;
		// multiplying the two signals intervenes in the short failure cascade
		// without disturbing the already-tuned sparse-board evaluation.
		if (scarce_deadly_placements != 0) {
			result += (uint64_t)scarce_deadly_placements * crowded_blocks
				* weights.getCrowdedPieceScarcity();
		}

		// Pieces that could still clear a line, counted once each. Two placements
		// that both need the same piece are one opportunity, not two: if that
		// piece is not dealt, neither is available. Counting placements instead
		// treats correlated options as independent and measures 8.7% shorter
		// games. Of everything tried against the crowded pairs in
		// engine/golden/golden.json, every static statistic over how full the
		// lines are ordered at most two of the six correctly; this orders five.
		// The quantity is a one-ply lookahead and not a property of the board,
		// which is why nothing already here could stand in for it.
		if (bb.count() >= BLOKIE_CLEAR_OPPORTUNITY_GATE) {
			const GameState here(bb);
			int ways = 0;
			for (int index = 0; index < Piece::NUM_PIECES; ++index) {
				const Piece piece = Piece::byIndex(index);
				const int squares = piece.count();
				if (squares < BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES ||
					squares > BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES) {
					continue;
				}
				for (auto it = here.nextStates(piece).begin();
					it != here.nextStates(piece).end(); ++it) {
					(void)*it;
					if (it.didClear()) {
						++ways;
						// One per piece: stopping here is the whole change, and
						// it also makes the term cheaper than counting on.
						//
						// It has to be one. Allowing two measures 83,626, which is
						// the uncapped 84,361 rather than anything between -- most
						// pieces that can clear have only one placement that does,
						// so a cap of two barely changes the count. Only a cap of
						// one removes the duplicates that share a piece.
						break;
					}
				}
			}
			const int cap = bb.count()
				* BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT / 100;
			const int missing = std::max(0, cap - ways);
			result += (uint64_t)missing * crowded_blocks
				* weights.getClearOpportunity();
		}
	}

	return std::min(result, max);
}

uint64_t GameState::simpleEval(EvalWeights weights, uint64_t max) const {
	const auto result = simpleEvalImpl(weights, bb, max);

	assert(bb == bb.topDownFlip().topDownFlip());
	assert(max != UINT64_MAX || result == simpleEvalImpl(weights, bb.topDownFlip()));

	return result;
}

uint64_t GameState::simpleEvalDefault(uint64_t max) const {
	const auto result = simpleEvalImpl(EvalWeights::getDefault(), bb, max);

	assert(bb == bb.topDownFlip().topDownFlip());
	assert(max != UINT64_MAX || result ==
		simpleEvalImpl(EvalWeights::getDefault(), bb.topDownFlip()));

	return result;
}

