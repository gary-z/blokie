#include "../bitboard.h"
#include "test-support.h"

#include <cstdint>
#include <string>

namespace {

void testEmptyAndFull() {
	test::require(BitBoard::empty() == BitBoard(0, 0), "empty is zero");
	test::require(BitBoard::full().count() == 81, "full count");
	test::require((BitBoard::empty() & BitBoard::full()) == BitBoard::empty(), "empty & full");
	test::require((BitBoard::empty() | BitBoard::full()) == BitBoard::full(), "empty | full");
	test::require((~BitBoard::empty()) == BitBoard::full(), "not empty");
	test::require((~BitBoard::full()) == BitBoard::empty(), "not full");
}

void testRowColumnCube() {
	for (unsigned i = 0; i < 9; ++i) {
		test::require(BitBoard::row(i).count() == 9, "row count " + std::to_string(i));
		test::require(BitBoard::column(i).count() == 9, "column count " + std::to_string(i));
	}
	for (unsigned r = 0; r < 3; ++r) {
		for (unsigned c = 0; c < 3; ++c) {
			test::require(BitBoard::cube(r, c).count() == 9, "cube count");
		}
	}
	// Rows, columns and cubes are disjoint in the right way.
	test::require((BitBoard::row(0) & BitBoard::row(1)) == BitBoard::empty(), "rows disjoint");
	test::require((BitBoard::column(0) & BitBoard::column(1)) == BitBoard::empty(), "columns disjoint");
	auto center = BitBoard::cube(1, 1);
	test::require((center & BitBoard::cube(0, 0)) == BitBoard::empty(), "cubes disjoint");
	test::require((BitBoard::row(4) & BitBoard::column(4)).count() == 1, "row/col intersect");
}

void testShifts() {
	auto single = test::square(4, 4);
	test::require(single.shiftLeft() == test::square(4, 3), "shiftLeft");
	test::require(single.shiftRight() == test::square(4, 5), "shiftRight");
	test::require(single.shiftUp() == test::square(3, 4), "shiftUp");
	test::require(single.shiftDown() == test::square(5, 4), "shiftDown");
	// Edges stay empty.
	test::require(test::square(0, 0).shiftLeft() == BitBoard::empty(), "shift off left edge");
	test::require(test::square(0, 8).shiftRight() == BitBoard::empty(), "shift off right edge");
	test::require(test::square(0, 0).shiftUp() == BitBoard::empty(), "shift off top");
	test::require(test::square(8, 0).shiftDown() == BitBoard::empty(), "shift off bottom");
	// Composition.
	test::require(single.shiftLeft().shiftRight() == single, "left/right inverse");
	test::require(single.shiftUp().shiftDown() == single, "up/down inverse");
}

void testCountAndAt() {
	test::require(BitBoard::empty().count() == 0, "empty count");
	auto two = test::square(0, 0) | test::square(8, 8);
	test::require(two.count() == 2, "two count");
	test::require(two.at(0, 0), "at 0,0");
	test::require(two.at(8, 8), "at 8,8");
	test::require(!two.at(4, 4), "not at 4,4");
	test::require(!BitBoard::empty(), "empty is falsy");
	test::require(static_cast<bool>(two), "nonempty is truthy");
}

void testLeastSignificantBit() {
	auto two = test::square(0, 1) | test::square(1, 0);
	// Least significant is row-major low bit: (0,1) is offset 1, (1,0) is offset 9.
	test::require(two.leastSignificantBit() == test::square(0, 1), "lsb");
	test::require(BitBoard::empty().leastSignificantBit() == BitBoard::empty(), "lsb empty");
	auto single = test::square(8, 8);
	test::require(single.leastSignificantBit() == single, "lsb single");
}

void testTopDownFlip() {
	test::require(BitBoard::empty().topDownFlip() == BitBoard::empty(), "flip empty");
	test::require(BitBoard::full().topDownFlip() == BitBoard::full(), "flip full");
	auto single = test::square(0, 0);
	test::require(single.topDownFlip() == test::square(8, 0), "flip corner");
	test::require(single.topDownFlip().topDownFlip() == single, "flip involution");
	test::Random random(0xB17B0A5ULL);
	for (int i = 0; i < 100; ++i) {
		auto board = random.board(4);
		test::require(board.topDownFlip().topDownFlip() == board, "flip involution random");
	}
}

void testBitwise() {
	auto a = test::square(0, 0) | test::square(1, 1);
	auto b = test::square(1, 1) | test::square(2, 2);
	test::require((a & b) == test::square(1, 1), "and");
	test::require((a | b).count() == 3, "or count");
	test::require((a - b) == test::square(0, 0), "minus");
	test::require((a - BitBoard::empty()) == a, "minus empty");
}

} // namespace

int main() {
	return test::run({
		{"empty and full", testEmptyAndFull},
		{"row column cube", testRowColumnCube},
		{"shifts", testShifts},
		{"count and at", testCountAndAt},
		{"least significant bit", testLeastSignificantBit},
		{"top down flip", testTopDownFlip},
		{"bitwise", testBitwise},
	});
}
