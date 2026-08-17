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
#ifndef BLOKIE_CLEAR_OPPORTUNITY
#define BLOKIE_CLEAR_OPPORTUNITY 0
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_WEIGHT
#define BLOKIE_CLEAR_OPPORTUNITY_WEIGHT 150
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_GATE
#define BLOKIE_CLEAR_OPPORTUNITY_GATE 30
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_CAP
#define BLOKIE_CLEAR_OPPORTUNITY_CAP 30
#endif
// Which pieces are asked whether they could clear. Small pieces fit almost
// anywhere, so counting them says more about the board being open than about a
// clear being available; the largest are rare enough to be poor evidence.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES 3
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES 5
#endif

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
		const auto blocked_right = open - open.shiftLeft();
		const auto blocked_left = open - open.shiftRight();
		const auto blocked_up = open - open.shiftDown();
		const auto blocked_down = open - open.shiftUp();

		// Every horizontal or vertical run of open squares has one transition
		// at each end. Count just the upper and left ends, then double them.
		// The four aligned boundary masks do not overlap, so their contributions
		// can be unioned before counting as well.
		const int transition_weight = weights.getTransition();
		const int aligned_transition_weight = weights.getTransitionAligned();
		const int base_transition_weight = std::min(transition_weight,
			aligned_transition_weight);
		const int all_transitions = 2 *
			(blocked_up.count() + blocked_left.count());
		result += all_transitions * base_transition_weight;
		if (result >= max) {
			return max;
		}

		const auto aligned_vertical =
			(blocked_up & (BitBoard::row(3) | BitBoard::row(6))) |
			(blocked_down & (BitBoard::row(2) | BitBoard::row(5)));
		const auto aligned_horizontal =
			(blocked_left & (BitBoard::column(3) | BitBoard::column(6))) |
			(blocked_right & (BitBoard::column(2) | BitBoard::column(5)));
		const int aligned_transitions = aligned_vertical.count() +
			aligned_horizontal.count();
		const int transitions = all_transitions - aligned_transitions;
		result += transitions * (transition_weight - base_transition_weight) +
			aligned_transitions *
				(aligned_transition_weight - base_transition_weight);
		if (result >= max) [[likely]] {
			return max;
		}

		// Cornerish squares carry the next strongest signal. Evaluate them before
		// the cheaper-weighted squashed-square features so a losing candidate can
		// stop without calculating either kind of squash.
		int cornered_empty = 0;
		const auto blocked_up_left = blocked_up & blocked_left;
		cornered_empty += (blocked_up_left -
			(BitBoard::row(0) | BitBoard::column(0))).count();
		const auto blocked_up_right = blocked_up & blocked_right;
		cornered_empty += (blocked_up_right -
			(BitBoard::row(0) | BitBoard::column(8))).count();
		const auto blocked_down_left = blocked_down & blocked_left;
		cornered_empty += (blocked_down_left -
			(BitBoard::row(8) | BitBoard::column(0))).count();
		const auto blocked_down_right = blocked_down & blocked_right;
		cornered_empty += (blocked_down_right -
			(BitBoard::row(8) | BitBoard::column(8))).count();
		result += cornered_empty * weights.getCorneredEmpty();
		if (result >= max) [[likely]] {
			return max;
		}

		const auto edges = BitBoard::row(0) | BitBoard::row(8) |
			BitBoard::column(0) | BitBoard::column(8);
		const auto horizontal_squashed = blocked_right & blocked_left;
		const auto verticle_squashed = blocked_up & blocked_down;
		const int squashed_empty = (horizontal_squashed - edges).count() +
			(verticle_squashed - edges).count();
		const int squashed_empty_at_edge =
			(horizontal_squashed & edges).count() +
			(verticle_squashed & edges).count();
		result += squashed_empty * weights.getSquashedEmpty() +
			squashed_empty_at_edge * weights.getSquashedEmptyAtEdge();
	}

	if (result >= max) {
		return max;
	}

	{
		// Deadly pieces.
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


		auto fillable_by_horizontal_3_bar =
		(open & open_left & open_right) | (open & open_left & open_2_left) |
		(open & open_right & open_2_right);
		result += (open &~ fillable_by_horizontal_3_bar).count() * weights.get3Bar();

		auto fillable_by_verticle_3_bar = (open & open_up & open_down) |
		(open & open_up & open_2_up) | (open & open_down & open_2_down);
		result += (open &~fillable_by_verticle_3_bar).count() * weights.get3Bar();

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

#if BLOKIE_CLEAR_OPPORTUNITY
		// Ways left to clear, counted where a mid-sized piece would complete a
		// line. Of everything tried against the crowded pairs in
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
					}
				}
			}
			const int missing = std::max(0, BLOKIE_CLEAR_OPPORTUNITY_CAP - ways);
			result += (uint64_t)missing * crowded_blocks
				* BLOKIE_CLEAR_OPPORTUNITY_WEIGHT;
		}
#endif
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

