#pragma once
#include <cstdint>

// Charging a crowded board for having no way to clear. Off by default: turning
// it on changes what the engine plays, so the committed WASM and the weights
// below both describe an engine without it. See docs/clear-opportunity.md.
//
// These live in the header rather than in eval.cpp so that the reference
// evaluation in tests/eval-test.cpp is checked against the same numbers. All
// four are overridable from the compiler command line, which is how they were
// swept.
#ifndef BLOKIE_CLEAR_OPPORTUNITY
#define BLOKIE_CLEAR_OPPORTUNITY 0
#endif
// Charge per missing way to clear, per square of crowding.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_WEIGHT
#define BLOKIE_CLEAR_OPPORTUNITY_WEIGHT 150
#endif
// Occupancy at which the term switches on. It pays for the enumeration.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_GATE
#define BLOKIE_CLEAR_OPPORTUNITY_GATE 30
#endif
// Ways to clear counted as enough; beyond it the board is not charged.
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

class EvalWeights {
public:
	static constexpr int NUM_WEIGHTS = 13;
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

	static constexpr EvalWeights getDefault();
};

// Played out by makeMoveSimpleDefault, these weights last a measured
// 46,962 sets of three pieces per game, 95% CI 46,148 to 47,805. From 3,520
// complete games, 162.7M moves, no cutoff. Change a weight below and the
// number no longer describes anything.
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
	return result;
}
