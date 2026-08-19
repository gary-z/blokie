#pragma once
#include <cstdint>
#include <string>

class GameState;
class NextGameStateIterator;

namespace bitboard_detail {
inline constexpr uint64_t ROW_0 = 0x1FFULL;
inline constexpr uint64_t TOP_LEFT_CUBE = 0x7ULL | (0x7ULL << 9) | (0x7ULL << 18);
inline constexpr uint64_t ALL_ALLOWED_BITS_IN_A = 0x3FFFFFFFFFFFFFULL;
inline constexpr uint64_t ALL_ALLOWED_BITS_IN_B = 0x7FFFFFFULL;
inline constexpr uint64_t RIGHT_MOST_COLUMN_B =
	(1ULL << 8) | (1ULL << 17) | (1ULL << 26);
inline constexpr uint64_t RIGHT_MOST_COLUMN_A = RIGHT_MOST_COLUMN_B
	| (1ULL << 35) | (1ULL << 44) | (1ULL << 53);
inline constexpr uint64_t LEFT_MOST_COLUMN_A = RIGHT_MOST_COLUMN_A >> 8;
inline constexpr uint64_t LEFT_MOST_COLUMN_B = RIGHT_MOST_COLUMN_B >> 8;
inline constexpr uint64_t ROW_5 = 0x1FFULL << (5 * 9);
inline constexpr uint64_t CUBE_STARTS_A = 0x49ULL | (0x49ULL << 27);
inline constexpr uint64_t CUBE_STARTS_B = 0x49ULL;
}

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
	// Needed to add masks together bitwise, a bit-plane at a time.
	BitBoard operator^(BitBoard other) const;

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
