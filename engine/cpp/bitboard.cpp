#include "bitboard.h"
#include <cassert>
#include <bit>
#include <string>

// Portable optimizer assumption.
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(assume) >= 202207L
#    define BLOKIE_ASSUME(condition) [[assume(condition)]]
#  endif
#endif
#ifndef BLOKIE_ASSUME
#  if defined(__clang__)
#    define BLOKIE_ASSUME(condition) __builtin_assume(condition)
#  elif defined(_MSC_VER)
#    define BLOKIE_ASSUME(condition) __assume(condition)
#  elif defined(__GNUC__)
#    define BLOKIE_ASSUME(condition) \
		do { if (!(condition)) __builtin_unreachable(); } while (false)
#  else
#    define BLOKIE_ASSUME(condition) ((void)0)
#  endif
#endif

using namespace bitboard_detail;

BitBoard::BitBoard(uint64_t a, uint64_t b) : a(a), b(b) {
	// The second word holds rows 6-8.
	assert(b <= ALL_ALLOWED_BITS_IN_B);
	BLOKIE_ASSUME(b <= ALL_ALLOWED_BITS_IN_B);
}

bool BitBoard::operator==(BitBoard other) const {
	return a == other.a && b == other.b;
}

bool BitBoard::operator<(BitBoard other) const {
	return a > other.a || (a == other.a && b > other.b);
}

bool BitBoard::at(unsigned r, unsigned c) const {
	return (bool)(BitBoard::row(r) & BitBoard::column(c) & (*this));
}

BitBoard BitBoard::operator|(const BitBoard other) const {
	return BitBoard(a | other.a, b | other.b);
}

BitBoard BitBoard::operator&(const BitBoard other) const {
	return BitBoard(a & other.a, b & other.b);
}

BitBoard BitBoard::operator-(const BitBoard other) const {
	return BitBoard(a & ~other.a, b & ~other.b);
}

BitBoard BitBoard::operator~() const {
	return BitBoard((~a) & ALL_ALLOWED_BITS_IN_A, (~b) & ALL_ALLOWED_BITS_IN_B);
}

BitBoard BitBoard::topDownFlip() const {
	auto result = BitBoard::empty();
	for (int r = 0; r < 9; ++r) {
		auto r_mirror = 8 - r;
		auto bits = (*this) & BitBoard::row(r_mirror);
		while (r_mirror != r) {
			if (r_mirror > r) {
				r_mirror--;
				bits = bits.shiftUp();
			}
			else {
				r_mirror++;
				bits = bits.shiftDown();
			}
		}
		result = result | bits;
	}
	return result;
}

BitBoard BitBoard::empty() {
	return BitBoard(0, 0);
}

BitBoard BitBoard::full() {
	return BitBoard(ALL_ALLOWED_BITS_IN_A, ALL_ALLOWED_BITS_IN_B);
}

BitBoard BitBoard::row(unsigned r) {
	assert(r < 9);
	if (r <= 5) {
		return BitBoard(ROW_0 << (r * 9), 0);
	}
	else {
		return BitBoard(0, ROW_0 << ((r - 6) * 9));
	}
}

BitBoard BitBoard::column(unsigned c) {
	assert(c < 9);
	int to_shift = 8 - c;
	return BitBoard(RIGHT_MOST_COLUMN_A >> to_shift, RIGHT_MOST_COLUMN_B >> to_shift);
}

BitBoard BitBoard::cube(unsigned r, unsigned c) {
	assert(c < 3);
	assert(r < 3);
	if (r < 2) {
		return BitBoard(TOP_LEFT_CUBE << (3 * c + 27 * r), 0);
	}
	else {
		return BitBoard(0, TOP_LEFT_CUBE << 3 * c);
	}
}


BitBoard BitBoard::shiftRight() const {
	return BitBoard((a & ~RIGHT_MOST_COLUMN_A) << 1, (b & ~RIGHT_MOST_COLUMN_B) << 1);
}

BitBoard BitBoard::shiftLeft() const {
	return BitBoard((a & ~LEFT_MOST_COLUMN_A) >> 1, (b & ~LEFT_MOST_COLUMN_B) >> 1);
}

BitBoard BitBoard::shiftDown() const {
	return BitBoard((a << 9) & ALL_ALLOWED_BITS_IN_A,
		((b << 9) | (a & ROW_5) >> 45) & ALL_ALLOWED_BITS_IN_B);
}

BitBoard BitBoard::shiftUp() const {
	return BitBoard((a >> 9) | ((b & 0x01FFULL) << 45), b >> 9);
}

BitBoard BitBoard::leastSignificantBit() const {
	if (a) {
		return BitBoard(a &- a, 0);
	}
	return BitBoard(0, b &- b);
}

int BitBoard::count() const {
	return std::popcount(a) + std::popcount(b);
}

BitBoard::operator bool() const {
	return a | b;
}

std::string BitBoard::str() const {
	std::string result;
	for (int r = 0; r < 9; ++r) {
		for (int c = 0; c < 9; ++c) {
			result += at(r, c) ? '#' : '.';
		}
		result += "\n";
	}
	return result;
}
