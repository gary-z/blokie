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
// Ways to clear counted as enough; beyond it the board is not charged. It is set
// as a percentage of occupancy rather than as a constant, because the number of
// clearing placements RISES with occupancy -- a median of 36 above thirty squares
// and about 47 above thirty-five -- so a constant cap charges the fullest and most
// dangerous boards least. At 100 the cap is the occupancy, which holds the
// fraction of boards charged roughly constant as the board fills.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT
#define BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT 100
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

class EvalWeights {
public:
	static constexpr int NUM_WEIGHTS = 14;
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

	static constexpr EvalWeights getDefault();
};

// Played out by makeMoveSimpleDefault, these weights last a measured
// 81,367 sets of three pieces per game. From 2,008 deaths over 163.4M moves of
// fixed-exposure chains, pooled across two seed bases, hazard 1.229e-05. Change a
// weight below and the number no longer describes anything.
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
	result.weights[13] = 200;  // clear opportunity
	return result;
}
