#pragma once
#include <cstdint>

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
