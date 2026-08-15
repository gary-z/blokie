#include "eval.h"
#include "game.h"
#include "bitboard.h"

#include <algorithm>
#include <cassert>
#include <bit>
#include <limits>
#include <cstdint>

using namespace bitboard_detail;

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

