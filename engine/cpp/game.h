#pragma once
#include "bitboard.h"
#include <array>
#include <cstddef>
#include <cstdint>

class PieceIteratorGenerator;
class Piece {
private:
	// Pieces are left/top normalized and occupy at most five rows, so their
	// shape always fits in BitBoard's first word. Keeping that invariant in the
	// type avoids carrying a permanently-zero second word through the search.
	uint64_t bits;
	uint8_t placement_data_index;
	Piece(uint64_t a, uint8_t placement_data_index);
	friend class NextGameStateIterator;
	friend class GameState;

public:
	explicit Piece(uint64_t a);
	explicit Piece(BitBoard bb);
	Piece();
	BitBoard getBitBoard() const;
	int count() const;
	bool isEmpty() const;
	static PieceIteratorGenerator getAll();
	static constexpr int NUM_PIECES = 47;
	static Piece byIndex(int index);
	bool operator<(Piece other) const;
};

class PieceSet {
public:
	PieceSet(Piece p1, Piece p2, Piece p3);
	Piece pieces[3];
};

class PieceIterator {
private:
	uint8_t i;
	PieceIterator(uint8_t i);
	friend class PieceIteratorGenerator;
public:

	Piece operator*() const;
	bool operator!=(PieceIterator other) const;
	void operator++();
};

class PieceIteratorGenerator {
private:
	PieceIteratorGenerator() {};
	friend class Piece;
public:
	PieceIterator begin() const;
	PieceIterator end() const;
};


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


class NextGameStateIteratorGenerator;
class ClearsFirstGameStates;
// A board partway through a game. No row, column or box on it is completely
// filled: a completed line is cleared the instant it is made, so a board that
// still holds one is not a position the game can be in. nextStates clears what
// it finds rather than checking, so handing one in makes a piece look as though
// it cleared lines it had nothing to do with.
class GameState {
private:
	BitBoard bb;
	static uint64_t simpleEvalImpl(EvalWeights weights, BitBoard bb, uint64_t max = UINT64_MAX);
public:
	explicit GameState(BitBoard bb);
	BitBoard getBitBoard() const;
	NextGameStateIteratorGenerator nextStates(Piece piece) const;
	ClearsFirstGameStates nextStatesClearsFirst(Piece piece) const;
	uint64_t simpleEval(EvalWeights weights, uint64_t max = UINT64_MAX) const;
	// Gives the native optimizer the built-in weights as scalar constants. The
	// generic entry point above remains available for tuning and tests.
	uint64_t simpleEvalDefault(uint64_t max = UINT64_MAX) const;
	bool isOver() const;
};

class NextGameStateIteratorGenerator;
class NextGameStateIterator {
public:

	GameState operator*() const;
	bool operator!=(NextGameStateIterator other) const;
	void operator++();
	BitBoard getPlacement() const { return next; }
	// Valid after dereferencing this iterator.
	bool didClear() const { return cleared; }

	// Where the current placement sits, as the bit the piece's top left corner
	// was shifted to. The placement is the piece translated by it, so this is
	// the placement in one byte.
	uint8_t getAnchor() const { return anchor; }
private:
	explicit NextGameStateIterator(GameState state, Piece piece);
	const GameState original;
	BitBoard next, anchors;
	uint64_t piece;
	uint8_t anchor;
	mutable bool cleared;
	void setNextPlacement();
	friend class NextGameStateIteratorGenerator;
};

class NextGameStateIteratorGenerator {
private:
	const GameState state;
	const Piece piece;
	explicit NextGameStateIteratorGenerator(GameState state, Piece piece);
	friend class GameState;
public:
	NextGameStateIterator begin() const;
	NextGameStateIterator end() const;
};

// Stores the placements that clear followed by the placements that do not,
// preserving placement order within each group. One piece has at most 81
// placements, so a fixed stack buffer avoids allocation in every search node.
class ClearsFirstGameStates {
private:
	struct StoredState {
		uint64_t a;
		uint64_t b;
	};

	static constexpr size_t MAX_STATES = 81;
	std::array<StoredState, MAX_STATES> clears;
	std::array<StoredState, MAX_STATES> no_clears;
	// Where each stored board's placement was anchored, and the piece every one
	// of them is a translation of. A search reads boards at every node it
	// visits and placements only for the handful that win, so keeping the
	// anchor rather than the placement leaves the arrays above the size they
	// were and rebuilds the bitboard on the rare occasion one is asked for.
	std::array<uint8_t, MAX_STATES> clear_anchors;
	std::array<uint8_t, MAX_STATES> no_clear_anchors;
	uint64_t piece;
	uint8_t num_clears;
	uint8_t num_no_clears;

	explicit ClearsFirstGameStates(uint64_t piece);
	void add(GameState state, uint8_t anchor, bool cleared);
	void finish();
	friend class GameState;

public:
	class Iterator {
	private:
		const ClearsFirstGameStates *states;
		size_t index;

		Iterator(const ClearsFirstGameStates *states, size_t index);
		friend class ClearsFirstGameStates;

	public:
		GameState operator*() const;

		// The squares the piece covers on the board it was placed on.
		BitBoard getPlacement() const;
		bool didClear() const;
		bool operator!=(Iterator other) const;
		void operator++();
	};

	Iterator begin() const;
	Iterator end() const;
	size_t size() const;
	GameState operator[](size_t index) const;
};
