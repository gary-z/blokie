#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifndef NO_SIMD
#ifdef __SSE2__
#define USE_SIMD 1
#include <emmintrin.h>  // SSE2
#ifdef __SSE4_1__
#include <smmintrin.h>  // SSE4.1 for _mm_testz_si128, _mm_extract_epi64
#endif
#endif
#endif

class GameState;
class NextGameStateIterator;


class BitBoard {
	// Represents a 9x9 board where each square is 1 or 0.
	// The board has 9 rows numbered 0 <= r < 9.
	// The board has 9 columns numbered 0 <= c < 9.
	// The board has 9 3x3 "cubes" indexed with (r, c). 0 <= r,c < 3.

private:
#if USE_SIMD
	__m128i data;
#else
	uint64_t a, b;
#endif
	friend class NextGameStateIterator;
	friend class GameState;
public:
	explicit BitBoard(uint64_t a, uint64_t b);
#if USE_SIMD
	explicit BitBoard(__m128i d);
#endif
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

	uint64_t getA() const;
	uint64_t getB() const;
};

class PieceIteratorGenerator;
class Piece {
private:
	BitBoard bb;

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
	static constexpr int NUM_WEIGHTS = 12;
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

	static EvalWeights getDefault();
};


class NextGameStateIteratorGenerator;
class GameState {
private:
	BitBoard bb;
	static uint64_t simpleEvalImpl(EvalWeights weights, BitBoard bb, uint64_t max = UINT64_MAX);
public:
	explicit GameState(BitBoard bb);
	BitBoard getBitBoard() const;
	NextGameStateIteratorGenerator nextStates(Piece piece) const;
	std::vector<GameState> nextStatesClearsFirst(Piece piece) const;
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
	BitBoard next, left;
	bool canPlace() const;
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

class AI {
public:
	// Return the state with the lowest score after placing the 3 pieces.
	static GameState makeMoveSimple(EvalWeights weights, GameState state, PieceSet piece_set);

	// Similar to makeMoveSimple, but considers possible placements of the 4th piece.
	static GameState makeMoveLookahead(EvalWeights weights, GameState state, PieceSet piece_set);

	static bool canClearWith2PiecesOrFewer(GameState state, PieceSet piece_set);
};
