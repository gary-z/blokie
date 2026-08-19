#pragma once
#include <cstdint>

// Charging a crowded board for having no way to clear, worth a measured 74% in
// game length. See docs/clear-opportunity.md.
//
// The knobs live in the header rather than in eval.cpp so that the reference
// evaluation in tests/eval-test.cpp is checked against the same numbers, and are
// overridable from the compiler command line, which is how they were swept. The
// charge itself is not here: it is weights[13], so the fitness tooling can tune
// it like any other, and setting it to zero is an exact way to switch the term
// off -- which is how every control in the write-up was measured.
// Occupancy at which the term switches on. It pays for the enumeration.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_GATE
#define BLOKIE_CLEAR_OPPORTUNITY_GATE 30
#endif
// Pieces able to clear that count as enough; beyond it the board is not charged.
// It is a percentage of occupancy rather than a constant because the count RISES
// with occupancy -- a fuller board has more nearly-complete lines -- so a constant
// cap charges the fullest and most dangerous boards least. 70 puts the cap just
// below the median of 17 pieces on a leaf above the gate, which charges about 80%
// of them, the same fraction the placement count charged at 100.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT
#define BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT 70
#endif
// Which pieces are asked whether they could clear. This is not really a
// statement about pieces: it sets the median of the count, and so the fraction of
// boards the cap charges. Four-to-five measured 74,085 against 67,699 for
// three-to-five and 65,166 for three-to-four, at 1200 deaths an arm.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES 4
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES 5
#endif

// The fifteen ways an empty square can be shut in, each with its own weight.
//
// This replaces three overlapping features -- squashed empty, cornered empty and
// squashed-empty-at-edge -- which could all charge the same square additively.
// That made the cost of a pattern a sum of terms rather than a number: the
// charge for an empty square walled on three sides was two cornered pairs plus
// one squashed pair, so it could not be moved without also moving the cost of
// every two-sided square. Here each pattern is priced directly.
//
// A side counts as blocked when the neighbour is filled *or* off the board, so
// the board edge walls a square in exactly as a filled square does. The classes
// are the orbits of the square's symmetry group, which is what keeps the
// evaluation invariant under flips and transposes as it was before.
//
// Interior squares have four real neighbours. Squares on the rim have a wall on
// one side ("edge", where `inward` is the neighbour away from the wall and
// `along` the two running beside it) or on two sides (the four board corners).
//
// The three `open` patterns -- an empty square with no filled neighbour at all --
// are here for completeness and default to zero. They are nearly collinear with
// occupancy, since the fifteen counts sum to the number of empty squares, so
// they are the least likely of the fifteen to pay for tuning.
//
// See docs/empty-patterns.md.
class EvalWeights {
public:
	// Every weight has a key, and its slot number is written here and nowhere
	// else: the getters below and getDefault() both index with these names. The
	// array is flat and positional because the tuning tools walk it as a vector,
	// but nothing else should have to know which position a weight sits in.
	//
	// The numbering is history rather than design. Slots 0 to 13 predate the
	// empty-square patterns and keep their positions so that every recorded
	// weight vector and every reference to `weights[13]` still means what it
	// said; three of those slots were held by the features the patterns replaced
	// and now name the pattern that inherited each value.
	enum Slot : int {
		OCCUPIED_SIDE_CUBE = 0,
		EMPTY_INTERIOR_TWO_OPPOSITE = 1,   // was squashed empty
		EMPTY_INTERIOR_TWO_ADJACENT = 2,   // was cornered empty
		TRANSITION = 3,
		DEADLY_PIECE = 4,
		THREE_BAR = 5,
		OCCUPIED_CENTER_CUBE = 6,
		OCCUPIED_CORNER_CUBE = 7,
		TRANSITION_ALIGNED = 8,
		EMPTY_EDGE_INWARD = 9,             // was squashed empty at edge
		OCCUPIED_CENTER_SQUARE = 10,
		OCCUPIED_CORNER_SQUARE = 11,
		CROWDED_PIECE_SCARCITY = 12,
		CLEAR_OPPORTUNITY = 13,
		EMPTY_INTERIOR_OPEN = 14,
		EMPTY_INTERIOR_ONE = 15,
		EMPTY_INTERIOR_THREE = 16,
		EMPTY_INTERIOR_FOUR = 17,
		EMPTY_EDGE_OPEN = 18,
		EMPTY_EDGE_ALONG = 19,
		EMPTY_EDGE_INWARD_ALONG = 20,
		EMPTY_EDGE_BOTH_ALONG = 21,
		EMPTY_EDGE_ALL = 22,
		EMPTY_CORNER_OPEN = 23,
		EMPTY_CORNER_ONE = 24,
		EMPTY_CORNER_BOTH = 25,
		// The rest of the three-square pieces. THREE_BAR already charges an open
		// cell once per bar orientation that can no longer cover it; these two
		// ask the same question of the other six pieces. Both default to zero.
		NO_DIAGONAL_THREE = 26,
		NO_L_THREE = 27,
	};

	static constexpr int NUM_WEIGHTS = NO_L_THREE + 1;
	// A guard against a mistyped weight on a command line, not a correctness
	// bound: at most 81 squares carry any one pattern, so even a weight far
	// above this cannot bring the accumulated uint64 result near overflow. It
	// was 40000, which turned out to be tighter than the legitimate values --
	// pricing a walled-in board corner by how hard it is to fill wants about
	// 55,000. See docs/empty-patterns.md.
	static constexpr int MAX_WEIGHT = 120000;

	int weights[NUM_WEIGHTS] = {0};

	constexpr EvalWeights() = default;

	constexpr int getOccupiedSideSquare() const { return 2000; }
	constexpr int getOccupiedSideCube() const { return weights[OCCUPIED_SIDE_CUBE]; }
	constexpr int getTransition() const { return weights[TRANSITION]; }
	constexpr int getDeadlyPiece() const { return weights[DEADLY_PIECE]; }
	constexpr int get3Bar() const { return weights[THREE_BAR]; }
	constexpr int getOccupiedCenterCube() const { return weights[OCCUPIED_CENTER_CUBE]; }
	constexpr int getOccupiedCornerCube() const { return weights[OCCUPIED_CORNER_CUBE]; }
	constexpr int getTransitionAligned() const { return weights[TRANSITION_ALIGNED]; }
	constexpr int getOccupiedCornerSquare() const { return weights[OCCUPIED_CORNER_SQUARE]; }
	constexpr int getOccupiedCenterSquare() const { return weights[OCCUPIED_CENTER_SQUARE]; }
	// Nonlinear penalty for scarce hard-piece placements on crowded boards.
	constexpr int getCrowdedPieceScarcity() const { return weights[CROWDED_PIECE_SCARCITY]; }
	// Charge per missing way to clear, per square of crowding. Zero switches the
	// term off exactly, which is how its controls were measured.
	constexpr int getClearOpportunity() const { return weights[CLEAR_OPPORTUNITY]; }

	// An empty square with four real neighbours.
	constexpr int getEmptyInteriorOpen() const { return weights[EMPTY_INTERIOR_OPEN]; }
	constexpr int getEmptyInteriorOne() const { return weights[EMPTY_INTERIOR_ONE]; }
	constexpr int getEmptyInteriorTwoAdjacent() const {
		return weights[EMPTY_INTERIOR_TWO_ADJACENT];
	}
	constexpr int getEmptyInteriorTwoOpposite() const {
		return weights[EMPTY_INTERIOR_TWO_OPPOSITE];
	}
	constexpr int getEmptyInteriorThree() const { return weights[EMPTY_INTERIOR_THREE]; }
	constexpr int getEmptyInteriorFour() const { return weights[EMPTY_INTERIOR_FOUR]; }

	// An empty square with a wall on one side.
	constexpr int getEmptyEdgeOpen() const { return weights[EMPTY_EDGE_OPEN]; }
	constexpr int getEmptyEdgeAlong() const { return weights[EMPTY_EDGE_ALONG]; }
	constexpr int getEmptyEdgeInward() const { return weights[EMPTY_EDGE_INWARD]; }
	constexpr int getEmptyEdgeInwardAlong() const {
		return weights[EMPTY_EDGE_INWARD_ALONG];
	}
	constexpr int getEmptyEdgeBothAlong() const { return weights[EMPTY_EDGE_BOTH_ALONG]; }
	constexpr int getEmptyEdgeAll() const { return weights[EMPTY_EDGE_ALL]; }

	// An empty square in a board corner, walled on two sides.
	constexpr int getEmptyCornerOpen() const { return weights[EMPTY_CORNER_OPEN]; }
	constexpr int getEmptyCornerOne() const { return weights[EMPTY_CORNER_ONE]; }
	constexpr int getEmptyCornerBoth() const { return weights[EMPTY_CORNER_BOTH]; }

	// How many of the eight three-square pieces can still fill a hole. That is
	// not decided by which of its four sides are blocked -- a diagonal piece
	// reaches a cell whose four neighbours are all filled -- so this is
	// information the patterns above cannot carry. Charged per orientation that
	// cannot cover the cell, which is how get3Bar() already charges the two bars.
	constexpr int getNoDiagonalThree() const { return weights[NO_DIAGONAL_THREE]; }
	constexpr int getNoLThree() const { return weights[NO_L_THREE]; }

	static constexpr EvalWeights getDefault();
};

// Played out by makeMoveSimpleDefault, these weights last a measured
// 91,670 sets of three pieces per game, 95% CI 87,150 to 96,423. From 1,503 deaths
// over 137.8M moves of fixed-exposure chains on seed base 20260822, hazard
// 1.091e-05. Change a weight below and the number no longer describes anything.
//
// Seed bases disagree by more than any one run's interval suggests: the previous
// evaluation measured 81,367, 87,091, 82,642 and 84,361 on four of them. Compare
// candidates against a baseline measured on the same seed, never against a
// remembered number.
//
// The twelve above weights[12] were fitted before the clear-opportunity term
// existed, against an engine that measured 46,962 (95% CI 46,148 to 47,805, from
// 3,520 complete games and 162.7M moves). Adding the term moved the figure to the
// one above without touching them, so they are very likely no longer where they
// should be -- retuning them is the obvious next gain and has not been done.
constexpr EvalWeights EvalWeights::getDefault() {
	EvalWeights result;
	result.weights[OCCUPIED_SIDE_CUBE] = 1358;
	result.weights[TRANSITION] = 4450;
	result.weights[DEADLY_PIECE] = 18185;
	result.weights[THREE_BAR] = 2665;
	result.weights[OCCUPIED_CENTER_CUBE] = 204;
	result.weights[OCCUPIED_CORNER_CUBE] = 908;
	result.weights[TRANSITION_ALIGNED] = 1776;
	result.weights[OCCUPIED_CENTER_SQUARE] = 1607;
	result.weights[OCCUPIED_CORNER_SQUARE] = 3067;
	result.weights[CROWDED_PIECE_SCARCITY] = 200;
	result.weights[CLEAR_OPPORTUNITY] = 335;

	// The empty-square patterns. These fifteen values reproduce the three
	// features they replaced exactly: each is the cornered-empty and
	// squashed-empty charge that the pattern used to accumulate, so the
	// evaluation is bit-identical to the one the game length above was measured
	// on. The arithmetic behind them -- 6540 for a cornered pair, 524 for an
	// interior squashed pair, 3386 for a squashed pair at the rim -- is
	// derivation and not measurement, so the twelve new free variables start
	// life at the only point the old parameterisation could express, and any
	// value they are moved to has to earn it.
	result.weights[EMPTY_INTERIOR_OPEN] = 0;
	result.weights[EMPTY_INTERIOR_ONE] = 0;
	result.weights[EMPTY_INTERIOR_TWO_ADJACENT] = 6540;   // 1 cornered
	result.weights[EMPTY_INTERIOR_TWO_OPPOSITE] = 524;    // 1 squashed
	result.weights[EMPTY_INTERIOR_THREE] = 13604;         // 2 cornered + 1 squashed
	result.weights[EMPTY_INTERIOR_FOUR] = 27208;          // 4 cornered + 2 squashed
	result.weights[EMPTY_EDGE_OPEN] = 0;
	result.weights[EMPTY_EDGE_ALONG] = 0;
	result.weights[EMPTY_EDGE_INWARD] = 3386;             // 1 rim squashed
	result.weights[EMPTY_EDGE_INWARD_ALONG] = 9926;       // 1 cornered + 1 rim squashed
	result.weights[EMPTY_EDGE_BOTH_ALONG] = 3386;         // 1 rim squashed
	result.weights[EMPTY_EDGE_ALL] = 19852;               // 2 cornered + 2 rim squashed
	result.weights[EMPTY_CORNER_OPEN] = 0;
	result.weights[EMPTY_CORNER_ONE] = 3386;              // 1 rim squashed
	result.weights[EMPTY_CORNER_BOTH] = 13312;            // 1 cornered + 2 rim squashed

	// Zero is the evaluation that shipped: only the two bars are asked whether
	// they can still fill a hole. See docs/empty-patterns.md.
	result.weights[NO_DIAGONAL_THREE] = 0;
	result.weights[NO_L_THREE] = 0;
	return result;
}
