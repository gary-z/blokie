#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// Compile-time overrides for the clear-opportunity term.
#ifndef BLOKIE_CLEAR_OPPORTUNITY_PLACEMENT_GATE
#define BLOKIE_CLEAR_OPPORTUNITY_PLACEMENT_GATE 686
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT
#define BLOKIE_CLEAR_OPPORTUNITY_CAP_PERCENT 70
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MIN_SQUARES 4
#endif
#ifndef BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES
#define BLOKIE_CLEAR_OPPORTUNITY_MAX_SQUARES 5
#endif

// What each term of the evaluation costs, one named field per term. Nothing
// lines up by position: a weight is written and read by name, so a term added
// in the middle cannot shift the ones after it, and a caller that means to set
// one cannot silently set its neighbour instead.
struct EvalWeights {
	int occupied_side_cube = 0;
	int squashed_empty = 0;
	int cornered_empty = 0;
	int transition = 0;
	int deadly_piece = 0;
	int three_bar = 0;
	int occupied_center_cube = 0;
	int occupied_corner_cube = 0;
	int transition_aligned = 0;
	int squashed_empty_at_edge = 0;
	int occupied_center_square = 0;
	int occupied_corner_square = 0;
	int crowded_piece_scarcity = 0;
	int clear_opportunity = 0;

	// The unit the tuned weights are scaled against, so it is a constant rather
	// than a field: tuning it would only rescale the rest.
	static constexpr int OCCUPIED_SIDE_SQUARE = 2000;
	static constexpr int MAX_WEIGHT = 40000;

	// The fields under the names the tools take them by. Walking this is how a
	// tool reaches every weight without a second list of them to keep in step.
	struct Field {
		std::string_view name;
		int EvalWeights::*value;
	};
	static constexpr Field FIELDS[] = {
		{"occupied_side_cube", &EvalWeights::occupied_side_cube},
		{"squashed_empty", &EvalWeights::squashed_empty},
		{"cornered_empty", &EvalWeights::cornered_empty},
		{"transition", &EvalWeights::transition},
		{"deadly_piece", &EvalWeights::deadly_piece},
		{"three_bar", &EvalWeights::three_bar},
		{"occupied_center_cube", &EvalWeights::occupied_center_cube},
		{"occupied_corner_cube", &EvalWeights::occupied_corner_cube},
		{"transition_aligned", &EvalWeights::transition_aligned},
		{"squashed_empty_at_edge", &EvalWeights::squashed_empty_at_edge},
		{"occupied_center_square", &EvalWeights::occupied_center_square},
		{"occupied_corner_square", &EvalWeights::occupied_corner_square},
		{"crowded_piece_scarcity", &EvalWeights::crowded_piece_scarcity},
		{"clear_opportunity", &EvalWeights::clear_opportunity},
	};
	static constexpr int NUM_WEIGHTS = static_cast<int>(std::size(FIELDS));

	// The field of this name, or null when no field has it.
	static constexpr const Field *find(std::string_view name) {
		for (const auto &field : FIELDS) {
			if (field.name == name) {
				return &field;
			}
		}
		return nullptr;
	}

	static constexpr EvalWeights getDefault();

	bool operator==(const EvalWeights &other) const = default;
};

namespace eval_detail {
// No two rows of FIELDS share a name or a field.
constexpr bool fieldsAreDistinct() {
	for (int i = 0; i < EvalWeights::NUM_WEIGHTS; ++i) {
		for (int j = i + 1; j < EvalWeights::NUM_WEIGHTS; ++j) {
			if (EvalWeights::FIELDS[i].name == EvalWeights::FIELDS[j].name ||
				EvalWeights::FIELDS[i].value == EvalWeights::FIELDS[j].value) {
				return false;
			}
		}
	}
	return true;
}
}

// Together these say FIELDS names every weight exactly once. The fields are the
// only members with storage, so a field added without its row leaves the class
// larger than the rows account for, and a row copied from its neighbour and
// left pointing at the old field is not distinct.
static_assert(sizeof(EvalWeights) == EvalWeights::NUM_WEIGHTS * sizeof(int),
	"every EvalWeights field needs a row in FIELDS");
static_assert(eval_detail::fieldsAreDistinct(),
	"every row of FIELDS needs its own name and field");

constexpr EvalWeights EvalWeights::getDefault() {
	return EvalWeights{
		.occupied_side_cube = 1358,
		.squashed_empty = 524,
		.cornered_empty = 6540,
		.transition = 4450,
		.deadly_piece = 18185,
		.three_bar = 2665,
		.occupied_center_cube = 204,
		.occupied_corner_cube = 908,
		.transition_aligned = 1776,
		.squashed_empty_at_edge = 3386,
		.occupied_center_square = 1607,
		.occupied_corner_square = 3067,
		.crowded_piece_scarcity = 200,
		.clear_opportunity = 335,
	};
}
