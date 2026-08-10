#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class GameState;
class NextGameStateIterator;
class ClearsFirstGameStates;


class BitBoard {
	// Represents a 9x9 board where each square is 1 or 0.
	// The board has 9 rows numbered 0 <= r < 9.
	// The board has 9 columns numbered 0 <= c < 9.
	// The board has 9 3x3 "cubes" indexed with (r, c). 0 <= r,c < 3.

private:
	uint64_t a, b;
	friend class NextGameStateIterator;
	friend class GameState;
public:
	explicit BitBoard(uint64_t a, uint64_t b);
	explicit operator bool() const;
	bool operator==(BitBoard other) const;
	bool operator<(BitBoard other) const;
	static BitBoard empty();
	static BitBoard full();

	// Return a board with 9 bits in the column/row/cube turned on.
	static BitBoard row(unsigned r);
	static BitBoard column(unsigned c);
	static BitBoard cube(unsigned r, unsigned c);

	// Is the bit at (r, c) on?
	bool at(unsigned r, unsigned c) const;

	// Bitwise operators.
	BitBoard operator|(BitBoard other) const;
	BitBoard operator&(BitBoard other) const;
	BitBoard operator~() const;

	// Same as (*this) &~ other.
	BitBoard operator-(BitBoard other) const;

	// Shift the entire board one space in the direction indicated.
	// When shifting left, the right most column will be empty.
	BitBoard shiftLeft() const;
	BitBoard shiftRight() const;
	BitBoard shiftUp() const;
	BitBoard shiftDown() const;

	BitBoard leastSignificantBit() const;

	// Swap row 0 with 8, 1 with 7, 2 with 6, 3 with 5.
	BitBoard topDownFlip() const;

	// How many bits are 1.
	int count() const;

	std::string str() const;

	uint64_t getA() const { return a; }
	uint64_t getB() const { return b; }
};

class PieceIteratorGenerator;
class Piece {
private:
	BitBoard bb;
	uint8_t placement_data_index;
	Piece(uint64_t a, uint8_t placement_data_index);
	friend class NextGameStateIterator;

public:
	explicit Piece(uint64_t a);
	explicit Piece(BitBoard bb);
	Piece();
	BitBoard getBitBoard() const;
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

	EvalWeights() = default;

	int getOccupiedSideSquare() const;
	int getOccupiedSideCube() const;
	int getSquashedEmpty() const;
	int getCorneredEmpty() const;
	int getTransition() const;
	int getDeadlyPiece() const;
	int get3Bar() const;
	int getOccupiedCenterCube() const;
	int getOccupiedCornerCube() const;
	int getTransitionAligned() const;
	int getSquashedEmptyAtEdge() const;
	int getOccupiedCornerSquare() const;
	int getOccupiedCenterSquare() const;
	// Nonlinear penalty for scarce hard-piece placements on crowded boards.
	int getCrowdedPieceScarcity() const;

	static EvalWeights getDefault();
};


class NextGameStateIteratorGenerator;
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
	bool isOver() const;
};

class NextGameStateIteratorGenerator;
class NextGameStateIterator {
public:

	GameState operator*() const;
	bool operator!=(NextGameStateIterator other) const;
	void operator++();
	BitBoard getPlacement() const { return next; }
private:
	explicit NextGameStateIterator(GameState state, Piece piece);
	const GameState original;
	BitBoard next, piece, anchors;
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
	uint8_t num_clears;
	uint8_t num_no_clears;

	ClearsFirstGameStates();
	void add(GameState state, bool cleared);
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
		bool operator!=(Iterator other) const;
		void operator++();
	};

	Iterator begin() const;
	Iterator end() const;
	size_t size() const;
	GameState operator[](size_t index) const;
};

class AI {
public:
	// Return the state with the lowest score after placing the 3 pieces.
	static GameState makeMoveSimple(EvalWeights weights, GameState state, PieceSet piece_set);

	// Similar to makeMoveSimple, but considers possible placements of the 4th piece.
	static GameState makeMoveLookahead(EvalWeights weights, GameState state, PieceSet piece_set);

	// How many of the three slots hold a piece. Blank slots sort last, so this
	// counts the leading run of a sorted piece set and is only meaningful for
	// one. A blank slot is placed by doing nothing, so it is also the number of
	// slots whose ordering can change anything.
	static int countPieces(const PieceSet &piece_set);

	// Is there an order and a pair of placements that clears a line before the
	// third piece is played? When there is not, every way of playing all three
	// pieces clears at most on the last one, so the board they end on is the
	// union of the three placements however they are ordered -- and a search
	// that walks one ordering has already seen every board the other five
	// could reach. The searches above use that to skip the other orderings.
	static bool canClearWith2PiecesOrFewer(GameState state, PieceSet piece_set);
};
