#include "../solver.h"
#include "test-support.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

struct ScalarBoard {
	bool occupied[9][9] = {};

	explicit ScalarBoard(BitBoard board) {
		for (unsigned row = 0; row < 9; ++row) {
			for (unsigned column = 0; column < 9; ++column) {
				occupied[row][column] = board.at(row, column);
			}
		}
	}

	bool open(int row, int column) const {
		return row >= 0 && row < 9 && column >= 0 && column < 9 &&
			!occupied[row][column];
	}

	bool blocked(int row, int column) const {
		return !open(row, column);
	}
};

using Shape = std::vector<std::pair<int, int>>;

const std::array<Shape, 17> DEADLY_SHAPES = {{
	{{0, 0}, {0, -1}, {0, -2}, {0, 1}, {0, 2}},
	{{0, 0}, {-1, 0}, {-2, 0}, {1, 0}, {2, 0}},
	{{0, 0}, {-1, 0}, {-2, 0}, {0, 1}, {0, 2}},
	{{0, 0}, {-1, 0}, {-2, 0}, {0, -1}, {0, -2}},
	{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}},
	{{0, 0}, {1, 0}, {2, 0}, {0, -1}, {0, -2}},
	{{0, 0}, {0, -1}, {0, 1}, {1, 0}, {2, 0}},
	{{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {-2, 0}},
	{{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, -2}},
	{{0, 0}, {-1, 0}, {1, 0}, {0, 1}, {0, 2}},
	{{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}},
	{{0, 0}, {1, -1}, {-1, 1}},
	{{0, 0}, {-1, -1}, {1, 1}},
	{{0, 0}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
	{{0, 0}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}},
	{{0, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}},
	{{0, 0}, {0, -1}, {0, 1}, {1, -1}, {1, 1}},
}};

uint64_t referenceEval(BitBoard bit_board, const EvalWeights &weights) {
	const ScalarBoard board(bit_board);
	uint64_t result = 0;

	for (int cube_row = 0; cube_row < 3; ++cube_row) {
		for (int cube_column = 0; cube_column < 3; ++cube_column) {
			int count = 0;
			for (int row = cube_row * 3; row < cube_row * 3 + 3; ++row) {
				for (int column = cube_column * 3;
					column < cube_column * 3 + 3; ++column) {
					count += board.occupied[row][column] ? 1 : 0;
				}
			}
			if (count == 0) {
				continue;
			}
			if (cube_row == 1 && cube_column == 1) {
				result += static_cast<uint64_t>(weights.weights[6]);
				result += static_cast<uint64_t>(count) * weights.weights[10];
			} else if (cube_row == 1 || cube_column == 1) {
				result += static_cast<uint64_t>(weights.weights[0]);
				result += static_cast<uint64_t>(count) * 2000;
			} else {
				result += static_cast<uint64_t>(weights.weights[7]);
				result += static_cast<uint64_t>(count) * weights.weights[11];
			}
		}
	}

	int squashed = 0;
	int squashed_at_edge = 0;
	int cornered = 0;
	int transitions = 0;
	int aligned_transitions = 0;
	for (int row = 0; row < 9; ++row) {
		for (int column = 0; column < 9; ++column) {
			if (!board.open(row, column)) {
				continue;
			}
			const bool right = board.blocked(row, column + 1);
			const bool left = board.blocked(row, column - 1);
			const bool up = board.blocked(row - 1, column);
			const bool down = board.blocked(row + 1, column);
			const bool edge = row == 0 || row == 8 || column == 0 || column == 8;
			const int squashed_here = (left && right ? 1 : 0) +
				(up && down ? 1 : 0);
			if (edge) {
				squashed_at_edge += squashed_here;
			} else {
				squashed += squashed_here;
			}

			cornered += up && left && row != 0 && column != 0 ? 1 : 0;
			cornered += up && right && row != 0 && column != 8 ? 1 : 0;
			cornered += down && left && row != 8 && column != 0 ? 1 : 0;
			cornered += down && right && row != 8 && column != 8 ? 1 : 0;

			const auto add_transition = [&](bool blocked, bool aligned) {
				if (blocked) {
					(aligned ? aligned_transitions : transitions)++;
				}
			};
			add_transition(up, row == 3 || row == 6);
			add_transition(down, row == 2 || row == 5);
			add_transition(left, column == 3 || column == 6);
			add_transition(right, column == 2 || column == 5);
		}
	}
	result += static_cast<uint64_t>(squashed) * weights.weights[1];
	result += static_cast<uint64_t>(squashed_at_edge) * weights.weights[9];
	result += static_cast<uint64_t>(cornered) * weights.weights[2];
	result += static_cast<uint64_t>(transitions) * weights.weights[3];
	result += static_cast<uint64_t>(aligned_transitions) * weights.weights[8];

	int unfillable_by_three_bar = 0;
	for (int row = 0; row < 9; ++row) {
		for (int column = 0; column < 9; ++column) {
			if (!board.open(row, column)) {
				continue;
			}
			bool horizontal = false;
			bool vertical = false;
			for (int offset = -2; offset <= 0; ++offset) {
				horizontal = horizontal || (board.open(row, column + offset) &&
					board.open(row, column + offset + 1) &&
					board.open(row, column + offset + 2));
				vertical = vertical || (board.open(row + offset, column) &&
					board.open(row + offset + 1, column) &&
					board.open(row + offset + 2, column));
			}
			unfillable_by_three_bar += horizontal ? 0 : 1;
			unfillable_by_three_bar += vertical ? 0 : 1;
		}
	}
	result += static_cast<uint64_t>(unfillable_by_three_bar) * weights.weights[5];

	int scarce_placements = 0;
	for (const auto &shape : DEADLY_SHAPES) {
		int placements = 0;
		for (int row = 0; row < 9; ++row) {
			for (int column = 0; column < 9; ++column) {
				bool fits = true;
				for (const auto &[row_offset, column_offset] : shape) {
					fits = fits && board.open(row + row_offset,
						column + column_offset);
				}
				placements += fits ? 1 : 0;
			}
		}
		if (placements == 0) {
			result += static_cast<uint64_t>(weights.weights[4]);
		}
		if (bit_board.count() > 20 && placements < 4) {
			scarce_placements += 4 - placements;
		}
	}
	const int crowded_blocks = std::max(0, bit_board.count() - 20);
	result += static_cast<uint64_t>(scarce_placements) * crowded_blocks *
		weights.weights[12];

	if (bit_board.count() >= BLOKIE_CLEAR_OPPORTUNITY_GATE) {
		int ways = 0;
		for (int index = 0; index < Piece::NUM_PIECES; ++index) {
			const Piece piece = Piece::byIndex(index);
			if (piece.count() < BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES ||
				piece.count() > BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES) {
				continue;
			}
			// The shape as a list of cells, so placements can be tried by
			// hand rather than through the iterator the evaluation uses.
			const BitBoard shape = piece.getBitBoard();
			Shape cells;
			for (int row = 0; row < 9; ++row) {
				for (int column = 0; column < 9; ++column) {
					if (shape.at(row, column)) {
						cells.emplace_back(row, column);
					}
				}
			}
			for (int row_offset = 0; row_offset < 9; ++row_offset) {
				for (int column_offset = 0; column_offset < 9;
					++column_offset) {
					ScalarBoard filled = board;
					bool fits = true;
					for (const auto &[row, column] : cells) {
						const int r = row + row_offset;
						const int c = column + column_offset;
						if (!filled.open(r, c)) {
							fits = false;
							break;
						}
						filled.occupied[r][c] = true;
					}
					if (!fits) {
						continue;
					}
					bool cleared = false;
					for (int i = 0; i < 9 && !cleared; ++i) {
						int in_row = 0;
						int in_column = 0;
						for (int k = 0; k < 9; ++k) {
							in_row += filled.occupied[i][k] ? 1 : 0;
							in_column += filled.occupied[k][i] ? 1 : 0;
						}
						cleared = in_row == 9 || in_column == 9;
					}
					for (int cube_row = 0; cube_row < 3 && !cleared;
						++cube_row) {
						for (int cube_column = 0; cube_column < 3 && !cleared;
							++cube_column) {
							int in_cube = 0;
							for (int row = 0; row < 3; ++row) {
								for (int column = 0; column < 3; ++column) {
									in_cube += filled.occupied
										[cube_row * 3 + row]
										[cube_column * 3 + column] ? 1 : 0;
								}
							}
							cleared = in_cube == 9;
						}
					}
					ways += cleared ? 1 : 0;
				}
			}
		}
		const int cap = bit_board.count() *
			BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT / 100;
		const int missing = std::max(0, cap - ways);
		result += static_cast<uint64_t>(missing) * crowded_blocks *
			weights.getClearOpportunity();
	}
	return result;
}

EvalWeights indexedWeights() {
	EvalWeights weights;
	for (int index = 0; index < EvalWeights::NUM_WEIGHTS; ++index) {
		weights.weights[index] = 101 + index * 37;
	}
	return weights;
}

void testWeightMapping() {
	const auto weights = indexedWeights();
	test::require(weights.getOccupiedSideSquare() == 2000, "side square constant");
	test::require(weights.getOccupiedSideCube() == weights.weights[0], "side cube");
	test::require(weights.getSquashedEmpty() == weights.weights[1], "squashed");
	test::require(weights.getCorneredEmpty() == weights.weights[2], "cornered");
	test::require(weights.getTransition() == weights.weights[3], "transition");
	test::require(weights.getDeadlyPiece() == weights.weights[4], "deadly");
	test::require(weights.get3Bar() == weights.weights[5], "three bar");
	test::require(weights.getOccupiedCenterCube() == weights.weights[6], "center cube");
	test::require(weights.getOccupiedCornerCube() == weights.weights[7], "corner cube");
	test::require(weights.getTransitionAligned() == weights.weights[8], "aligned");
	test::require(weights.getSquashedEmptyAtEdge() == weights.weights[9], "edge squash");
	test::require(weights.getOccupiedCenterSquare() == weights.weights[10], "center square");
	test::require(weights.getOccupiedCornerSquare() == weights.weights[11], "corner square");
	test::require(weights.getCrowdedPieceScarcity() == weights.weights[12], "crowding");
	test::require(weights.getClearOpportunity() == weights.weights[13],
		"clear opportunity");
}

void testDefaultWeights() {
	const std::array<int, EvalWeights::NUM_WEIGHTS> expected = {
		1358, 524, 6540, 4450, 18185, 2665, 204,
		908, 1776, 3386, 1607, 3067, 200, 200,
	};
	const auto actual = EvalWeights::getDefault();
	for (int index = 0; index < EvalWeights::NUM_WEIGHTS; ++index) {
		test::require(actual.weights[index] == expected[index],
			"default weight " + std::to_string(index));
	}
}

std::vector<BitBoard> evaluationBoards() {
	std::vector<BitBoard> boards = {BitBoard::empty(), BitBoard::full()};
	boards.push_back(test::square(0, 0));
	boards.push_back(test::square(0, 4));
	boards.push_back(test::square(4, 4));
	boards.push_back(BitBoard::row(4) - test::square(4, 4));
	boards.push_back((BitBoard::row(4) | BitBoard::column(4)) - test::square(4, 4));
	boards.push_back((BitBoard::row(4) | BitBoard::column(4) |
		BitBoard::cube(1, 1)) - test::square(4, 4));

	test::Random random(0xE7A1C0DEULL);
	for (unsigned density = 0; density <= 8; ++density) {
		for (int sample = 0; sample < 80; ++sample) {
			boards.push_back(random.board(density));
		}
	}
	return boards;
}

void testScalarReference() {
	const auto boards = evaluationBoards();
	std::vector<EvalWeights> weight_sets = {
		EvalWeights(), EvalWeights::getDefault(), indexedWeights(),
	};
	for (int selected = 0; selected < EvalWeights::NUM_WEIGHTS; ++selected) {
		EvalWeights weights;
		weights.weights[selected] = 1;
		weight_sets.push_back(weights);
	}

	for (size_t board_index = 0; board_index < boards.size(); ++board_index) {
		for (size_t weights_index = 0; weights_index < weight_sets.size();
			++weights_index) {
			const auto expected = referenceEval(boards[board_index],
				weight_sets[weights_index]);
			const auto actual = GameState(boards[board_index]).simpleEval(
				weight_sets[weights_index]);
			test::require(actual == expected,
				"board " + std::to_string(board_index) + ", weights " +
				std::to_string(weights_index) + ": expected " +
				std::to_string(expected) + ", got " + std::to_string(actual));
		}
	}
}

void testCrowdingThreshold() {
	EvalWeights with_crowding;
	with_crowding.weights[12] = 1;
	EvalWeights without_crowding;

	// Find a deterministic 21-cell geometry that really does make at least one
	// hard shape scarce. Occupancy alone is intentionally not enough to trigger
	// this feature.
	test::Random random(0xC20D1A6ULL);
	auto board = BitBoard::empty();
	for (int attempt = 0; attempt < 10000; ++attempt) {
		auto candidate = BitBoard::empty();
		while (candidate.count() < 21) {
			candidate = candidate | test::square(random.below(9), random.below(9));
		}
		if (referenceEval(candidate, with_crowding) >
			referenceEval(candidate, without_crowding)) {
			board = candidate;
			break;
		}
	}
	test::require(board.count() == 21, "found a scarce 21-cell geometry");

	const auto one_cell = board.leastSignificantBit();
	const auto twenty_cells = board - one_cell;
	const auto at_twenty = GameState(twenty_cells).simpleEval(with_crowding) -
		GameState(twenty_cells).simpleEval(without_crowding);
	test::require(at_twenty == 0, "crowding is inactive at 20 occupied squares");

	const auto actual = GameState(board).simpleEval(with_crowding) -
		GameState(board).simpleEval(without_crowding);
	const auto expected = referenceEval(board, with_crowding) -
		referenceEval(board, without_crowding);
	test::require(actual == expected && actual > 0,
		"scarcity and occupancy interact above the threshold");
}

void testEvaluationCutoff() {
	const auto weights = EvalWeights::getDefault();
	test::Random random(0xC070FFULL);
	for (int sample = 0; sample < 200; ++sample) {
		const auto board = random.board(random.below(9));
		const auto expected = referenceEval(board, weights);
		const std::array<uint64_t, 6> cutoffs = {
			0, 1, expected / 2, expected, expected + 1,
			std::numeric_limits<uint64_t>::max(),
		};
		for (const auto cutoff : cutoffs) {
			const auto actual = GameState(board).simpleEval(weights, cutoff);
			test::require(actual == std::min(expected, cutoff),
				"cutoff " + std::to_string(cutoff) + " on sample " +
				std::to_string(sample));
			test::require(GameState(board).simpleEvalDefault(cutoff) == actual,
				"constexpr default cutoff " + std::to_string(cutoff) +
					" on sample " + std::to_string(sample));
		}
	}
}

void testVerticalSymmetry() {
	const auto weights = EvalWeights::getDefault();
	test::Random random(0xF11F5ULL);
	for (int sample = 0; sample < 300; ++sample) {
		const auto board = random.board(random.below(9));
		test::require(GameState(board).simpleEval(weights) ==
			GameState(board.topDownFlip()).simpleEval(weights),
			"top-down symmetry sample " + std::to_string(sample));
	}
}

} // namespace

int main() {
	return test::run({
		{"evaluation weight mapping", testWeightMapping},
		{"default evaluation weights", testDefaultWeights},
		{"scalar evaluation reference", testScalarReference},
		{"nonlinear crowding threshold", testCrowdingThreshold},
		{"evaluation cutoff", testEvaluationCutoff},
		{"evaluation vertical symmetry", testVerticalSymmetry},
	});
}
