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

class EvalWeights {
public:
	// Every weight has a key and its slot number is written here and nowhere
	// else: the getters below and getDefault() both index with these names.
	//
	// Slots 1, 2, 3, 5, 8 and 9 are unused. They held jaggedness, the aligned
	// jaggedness rate, the three-bar fit, cornered empty and squashed empty, all
	// of which are gone -- one signal replaced them. The surviving weights keep
	// their old numbers so that references to weights[13] and the recorded
	// vectors for the terms that remain still mean what they said. Nothing reads
	// the unused slots; setting one has no effect.
	enum Slot : int {
		OCCUPIED_SIDE_CUBE = 0,
		DEADLY_PIECE = 4,
		OCCUPIED_CENTER_CUBE = 6,
		OCCUPIED_CORNER_CUBE = 7,
		OCCUPIED_CENTER_SQUARE = 10,
		OCCUPIED_CORNER_SQUARE = 11,
		CROWDED_PIECE_SCARCITY = 12,
		CLEAR_OPPORTUNITY = 13,
		// How much room an empty square has left, as one weight per value of the
		// count: how many PLACEMENTS of a three-square piece still cover it.
		// Nineteen values, because six pieces -- two bars and four L shapes, the
		// diagonal staircases excluded -- have three placements each over a given
		// square.
		THREE_WAYS_0 = 14,
		THREE_WAYS_18 = THREE_WAYS_0 + 18,
	};

	static constexpr int NUM_WEIGHTS = THREE_WAYS_18 + 1;
	static constexpr int MAX_WEIGHT = 120000;

	int weights[NUM_WEIGHTS] = {0};

	constexpr EvalWeights() = default;

	constexpr int getOccupiedSideSquare() const { return 2000; }
	constexpr int getOccupiedSideCube() const { return weights[OCCUPIED_SIDE_CUBE]; }
	constexpr int getDeadlyPiece() const { return weights[DEADLY_PIECE]; }
	constexpr int getOccupiedCenterCube() const { return weights[OCCUPIED_CENTER_CUBE]; }
	constexpr int getOccupiedCornerCube() const { return weights[OCCUPIED_CORNER_CUBE]; }
	constexpr int getOccupiedCornerSquare() const { return weights[OCCUPIED_CORNER_SQUARE]; }
	constexpr int getOccupiedCenterSquare() const { return weights[OCCUPIED_CENTER_SQUARE]; }
	// Nonlinear penalty for scarce hard-piece placements on crowded boards.
	constexpr int getCrowdedPieceScarcity() const { return weights[CROWDED_PIECE_SCARCITY]; }
	// Charge per missing way to clear, per square of crowding. Zero switches the
	// term off exactly, which is how its controls were measured.
	constexpr int getClearOpportunity() const { return weights[CLEAR_OPPORTUNITY]; }

	// The charge for an empty square with `ways` placements left, 0 to 18. It
	// falls with room: 0 ways is a hole nothing three squares long can reach.
	constexpr int getThreeWays(int ways) const { return weights[THREE_WAYS_0 + ways]; }

	static constexpr EvalWeights getDefault();
};

// EXPERIMENT. One signal -- how much room an empty square has left -- in place of
// jaggedness, the three-bar fit, cornered empty and squashed empty. The room
// table below starts as the least-squares fit to the four features it replaces,
// so this is close to the shipped evaluation but NOT identical to it, and the
// 91,670 sets a game that the previous weights measured does not describe it.
// Nothing here is confirmed. See docs/three-way-fill.md.
constexpr EvalWeights EvalWeights::getDefault() {
	EvalWeights result;
	result.weights[OCCUPIED_SIDE_CUBE] = 1358;
	result.weights[DEADLY_PIECE] = 18185;
	result.weights[OCCUPIED_CENTER_CUBE] = 204;
	result.weights[OCCUPIED_CORNER_CUBE] = 908;
	result.weights[OCCUPIED_CENTER_SQUARE] = 1607;
	result.weights[OCCUPIED_CORNER_SQUARE] = 3067;
	result.weights[CROWDED_PIECE_SCARCITY] = 200;
	result.weights[CLEAR_OPPORTUNITY] = 335;

	// The room table. These are not a measurement of what a hole is worth; they
	// are the mean charge the four removed features put on an empty square with
	// that many placements left, over 102,326 empty squares from real play, which
	// is the least-squares closest this shape can start to the evaluation it
	// replaces. The four were almost a function of this count already: past
	// twelve placements they charged a square nothing at all.
	result.weights[THREE_WAYS_0 + 0] = 36760;
	result.weights[THREE_WAYS_0 + 1] = 22470;
	result.weights[THREE_WAYS_0 + 2] = 21080;
	result.weights[THREE_WAYS_0 + 3] = 16260;
	result.weights[THREE_WAYS_0 + 4] = 12080;
	result.weights[THREE_WAYS_0 + 5] = 9710;
	result.weights[THREE_WAYS_0 + 6] = 9250;
	result.weights[THREE_WAYS_0 + 7] = 8390;
	result.weights[THREE_WAYS_0 + 8] = 4770;
	result.weights[THREE_WAYS_0 + 9] = 4490;
	result.weights[THREE_WAYS_0 + 10] = 4190;
	result.weights[THREE_WAYS_0 + 11] = 3600;
	result.weights[THREE_WAYS_0 + 12] = 2780;
	result.weights[THREE_WAYS_0 + 13] = 0;
	result.weights[THREE_WAYS_0 + 14] = 0;
	result.weights[THREE_WAYS_0 + 15] = 0;
	result.weights[THREE_WAYS_0 + 16] = 0;
	result.weights[THREE_WAYS_0 + 17] = 0;
	result.weights[THREE_WAYS_0 + 18] = 0;
	return result;
}
