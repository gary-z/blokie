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

// A filled neighbour can be cleared away. The edge of the board cannot. The
// jaggedness features do not draw that distinction: cornered empty *ignores* a
// corner formed by a wall entirely, and squashed empty splits on whether the
// SQUARE sits on the rim rather than on whether the thing blocking it is a wall.
// The four weights at the end of the list free those distinctions, and all four
// default to zero, so the evaluation starts exactly where it was.
// See docs/wall-vs-filled.md.
class EvalWeights {
public:
	// Every weight has a key, and its slot number is written here and nowhere
	// else, so that a value cannot drift onto the wrong weight.
	enum Slot : int {
		OCCUPIED_SIDE_CUBE = 0,
		SQUASHED_EMPTY = 1,
		CORNERED_EMPTY = 2,
		TRANSITION = 3,
		DEADLY_PIECE = 4,
		THREE_BAR = 5,
		OCCUPIED_CENTER_CUBE = 6,
		OCCUPIED_CORNER_CUBE = 7,
		TRANSITION_ALIGNED = 8,
		SQUASHED_EMPTY_AT_EDGE = 9,
		OCCUPIED_CENTER_SQUARE = 10,
		OCCUPIED_CORNER_SQUARE = 11,
		CROWDED_PIECE_SCARCITY = 12,
		CLEAR_OPPORTUNITY = 13,
		// Extra charge when the blocking is a wall rather than a filled square.
		SQUASHED_AGAINST_WALL = 14,
		CORNERED_ONE_WALL = 15,
		CORNERED_TWO_WALLS = 16,
		// Extra charge for a square shut in on three sides whose one open side
		// runs off the board two steps later.
		THREE_SIDED_SHALLOW_ESCAPE = 17,
	};

	static constexpr int NUM_WEIGHTS = THREE_SIDED_SHALLOW_ESCAPE + 1;
	static constexpr int MAX_WEIGHT = 40000;

	int weights[NUM_WEIGHTS] = {0};

	constexpr EvalWeights() = default;

	constexpr int getOccupiedSideSquare() const { return 2000; }
	constexpr int getOccupiedSideCube() const { return weights[0]; }
	constexpr int getSquashedEmpty() const { return weights[1]; }
	constexpr int getCorneredEmpty() const { return weights[2]; }
	constexpr int getTransition() const { return weights[3]; }
	constexpr int getDeadlyPiece() const { return weights[4]; }
	constexpr int get3Bar() const { return weights[5]; }
	constexpr int getOccupiedCenterCube() const { return weights[6]; }
	constexpr int getOccupiedCornerCube() const { return weights[7]; }
	constexpr int getTransitionAligned() const { return weights[8]; }
	constexpr int getSquashedEmptyAtEdge() const { return weights[9]; }
	constexpr int getOccupiedCornerSquare() const { return weights[11]; }
	constexpr int getOccupiedCenterSquare() const { return weights[10]; }
	// Nonlinear penalty for scarce hard-piece placements on crowded boards.
	constexpr int getCrowdedPieceScarcity() const { return weights[12]; }
	// Charge per missing way to clear, per square of crowding. Zero switches the
	// term off exactly, which is how its controls were measured.
	constexpr int getClearOpportunity() const { return weights[13]; }

	// A pair of opposite blocked sides where one of them is the board edge. The
	// square is necessarily on the rim, so this is charged on top of
	// getSquashedEmptyAtEdge().
	constexpr int getSquashedAgainstWall() const {
		return weights[SQUASHED_AGAINST_WALL];
	}
	// A pair of adjacent blocked sides involving the board edge. Neither is
	// counted by getCorneredEmpty(), which only sees corners made of two real
	// filled squares, so these are charges the evaluation did not have. A corner
	// made with a wall can never be freed by a clear.
	constexpr int getCorneredOneWall() const { return weights[CORNERED_ONE_WALL]; }
	constexpr int getCorneredTwoWalls() const { return weights[CORNERED_TWO_WALLS]; }
	// Shut in on three sides, with the open side leading off the board two steps
	// on -- a dead end rather than a way out.
	constexpr int getThreeSidedShallowEscape() const {
		return weights[THREE_SIDED_SHALLOW_ESCAPE];
	}

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
	result.weights[0] = 1358;  // occupied side cube
	result.weights[1] = 524;   // squashed empty
	result.weights[2] = 6540;  // cornered empty
	result.weights[3] = 4450;  // transition
	result.weights[4] = 18185; // deadly piece
	result.weights[5] = 2665;  // three bar
	result.weights[6] = 204;   // occupied center cube
	result.weights[7] = 908;   // occupied corner cube
	result.weights[8] = 1776;  // aligned transition
	result.weights[9] = 3386;  // squashed empty at edge
	result.weights[10] = 1607; // occupied center square
	result.weights[11] = 3067; // occupied corner square
	result.weights[12] = 200;  // crowded-piece scarcity
	result.weights[13] = 335;  // clear opportunity
	// Zero is the evaluation that shipped: a wall blocks a square exactly as a
	// filled square does, a corner made with a wall is not charged at all, and a
	// three-sided square is not asked where its open side leads.
	result.weights[SQUASHED_AGAINST_WALL] = 0;
	result.weights[CORNERED_ONE_WALL] = 0;
	result.weights[CORNERED_TWO_WALLS] = 0;
	result.weights[THREE_SIDED_SHALLOW_ESCAPE] = 0;
	return result;
}
