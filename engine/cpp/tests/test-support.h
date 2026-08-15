#pragma once

#include "../solver.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {

class Failure : public std::runtime_error {
public:
	explicit Failure(const std::string &message) : std::runtime_error(message) {}
};

inline void require(bool condition, const std::string &message) {
	if (!condition) {
		throw Failure(message);
	}
}

inline BitBoard square(unsigned row, unsigned column) {
	return BitBoard::row(row) & BitBoard::column(column);
}

inline BitBoard boardFromJs(uint64_t a, uint64_t b, uint64_t c) {
	return BitBoard(a | (b << 27), c);
}

inline std::string describe(BitBoard board) {
	return "a=" + std::to_string(board.getA()) + ", b=" +
		std::to_string(board.getB()) + "\n" + board.str();
}

inline BitBoard clearCompletedLines(BitBoard board) {
	auto to_clear = BitBoard::empty();
	for (unsigned index = 0; index < 9; ++index) {
		const auto row = BitBoard::row(index);
		const auto column = BitBoard::column(index);
		const auto cube = BitBoard::cube(index / 3, index % 3);
		if ((board & row) == row) {
			to_clear = to_clear | row;
		}
		if ((board & column) == column) {
			to_clear = to_clear | column;
		}
		if ((board & cube) == cube) {
			to_clear = to_clear | cube;
		}
	}
	return board - to_clear;
}

struct PlacementResult {
	BitBoard placement;
	BitBoard board;
};

// Deliberately simple placement oracle. It knows nothing about the solver's
// bit shifts or its cached standard-piece masks: it walks the 9x9 cells and
// clears completed lines one at a time.
inline std::vector<PlacementResult> referencePlacements(BitBoard board, Piece piece) {
	const auto shape = piece.getBitBoard();
	if (shape == BitBoard::empty()) {
		return {{BitBoard::empty(), clearCompletedLines(board)}};
	}
	if (shape == BitBoard::full()) {
		return {};
	}

	std::vector<std::pair<unsigned, unsigned>> cells;
	unsigned max_row = 0;
	unsigned max_column = 0;
	for (unsigned row = 0; row < 9; ++row) {
		for (unsigned column = 0; column < 9; ++column) {
			if (shape.at(row, column)) {
				cells.emplace_back(row, column);
				max_row = std::max(max_row, row);
				max_column = std::max(max_column, column);
			}
		}
	}

	std::vector<PlacementResult> result;
	for (unsigned anchor_row = 0; anchor_row + max_row < 9; ++anchor_row) {
		for (unsigned anchor_column = 0;
			anchor_column + max_column < 9; ++anchor_column) {
			auto placement = BitBoard::empty();
			for (const auto &[row, column] : cells) {
				placement = placement | square(anchor_row + row,
					anchor_column + column);
			}
			if (!(placement & board)) {
				result.push_back({placement,
					clearCompletedLines(board | placement)});
			}
		}
	}
	return result;
}

inline std::vector<PlacementResult> actualPlacements(GameState state, Piece piece) {
	std::vector<PlacementResult> result;
	auto states = state.nextStates(piece);
	for (auto iterator = states.begin(), end = states.end(); iterator != end;
		++iterator) {
		result.push_back({iterator.getPlacement(), (*iterator).getBitBoard()});
	}
	return result;
}

inline void requireSamePlacements(BitBoard board, Piece piece,
	const std::string &context) {
	const auto expected = referencePlacements(board, piece);
	const auto actual = actualPlacements(GameState(board), piece);
	require(actual.size() == expected.size(), context + ": expected " +
		std::to_string(expected.size()) + " placements, got " +
		std::to_string(actual.size()));
	for (size_t index = 0; index < expected.size(); ++index) {
		require(actual[index].placement == expected[index].placement,
			context + ": placement differs at index " + std::to_string(index));
		require(actual[index].board == expected[index].board,
			context + ": result differs at index " + std::to_string(index) +
			"\nexpected " + describe(expected[index].board) +
			"actual " + describe(actual[index].board));
	}
}

class Random {
public:
	explicit Random(uint64_t seed) : state(seed) {}

	uint64_t next() {
		state += 0x9E3779B97F4A7C15ULL;
		auto value = state;
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
		return value ^ (value >> 31);
	}

	unsigned below(unsigned limit) {
		return static_cast<unsigned>(next() % limit);
	}

	BitBoard board(unsigned occupied_out_of_eight) {
		auto result = BitBoard::empty();
		for (unsigned row = 0; row < 9; ++row) {
			for (unsigned column = 0; column < 9; ++column) {
				if ((next() & 7U) < occupied_out_of_eight) {
					result = result | square(row, column);
				}
			}
		}
		return result;
	}

private:
	uint64_t state;
};

using Test = std::pair<const char *, std::function<void()>>;

inline int run(const std::vector<Test> &tests) {
	int failures = 0;
	for (const auto &[name, body] : tests) {
		try {
			body();
			std::cout << "ok - " << name << '\n';
		} catch (const std::exception &error) {
			++failures;
			std::cerr << "FAIL - " << name << ": " << error.what() << '\n';
		}
	}
	if (failures != 0) {
		std::cerr << failures << " test(s) failed\n";
		return 1;
	}
	std::cout << tests.size() << " tests passed\n";
	return 0;
}

} // namespace test
