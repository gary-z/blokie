#include "solver.h"
#include <cassert>
#include <bitset>
#include <algorithm>
#include <array>

namespace {
	const uint64_t ROW_0 = 0x1FFULL;
	const uint64_t TOP_LEFT_CUBE = 0x7ULL | (0x7ULL << 9) | (0x7ULL << 18);
	const uint64_t ALL_ALLOWED_BITS_IN_A = 0x3FFFFFFFFFFFFFULL;
	const uint64_t ALL_ALLOWED_BITS_IN_B = 0x7FFFFFFULL;
	const uint64_t RIGHT_MOST_COLUMN_B = (1ULL << 8) | (1ULL << 17) | (1ULL << 26);
	const uint64_t RIGHT_MOST_COLUMN_A = RIGHT_MOST_COLUMN_B
		| (1ULL << 35) | (1ULL << 44) | (1ULL << 53);
	const uint64_t LEFT_MOST_COLUMN_A = RIGHT_MOST_COLUMN_A >> 8;
	const uint64_t LEFT_MOST_COLUMN_B = RIGHT_MOST_COLUMN_B >> 8;
	const uint64_t ROW_5 = 0x1FFULL << (5 * 9);
	const uint64_t CUBE_STARTS_A = 0x49ULL | (0x49ULL << 27);
	const uint64_t CUBE_STARTS_B = 0x49ULL;

	uint64_t completedRows(uint64_t bits, uint64_t row_starts) {
		// Reduce each nine-bit row to its first bit, then expand the surviving
		// markers back across their disjoint rows.
		auto runs = bits & (bits >> 1);
		runs &= runs >> 2;
		runs &= runs >> 4;
		runs &= bits >> 8;
		return (runs & row_starts) * ROW_0;
	}

	uint64_t completedCubes(uint64_t bits, uint64_t cube_starts) {
		// Reduce 3x3 cubes to their top-left bits. Multiplication expands those
		// markers back into non-overlapping cube masks.
		const auto horizontal = bits & (bits >> 1) & (bits >> 2);
		const auto completed = horizontal & (horizontal >> 9) & (horizontal >> 18);
		return (completed & cube_starts) * TOP_LEFT_CUBE;
	}

	// Shift the conceptual 81-bit value right across BitBoard's 54/27 split.
	// A piece cell at offset N can use every anchor whose bit N places it on an
	// open square, so this translates open squares back into anchor squares.
	BitBoard shiftOpenToAnchor(BitBoard open, unsigned offset) {
		if (offset == 0) {
			return open;
		}
		if (offset < 54) {
			return BitBoard(
				((open.getA() >> offset) | (open.getB() << (54 - offset))) & ALL_ALLOWED_BITS_IN_A,
				(open.getB() >> offset) & ALL_ALLOWED_BITS_IN_B
			);
		}
		return BitBoard((open.getB() >> (offset - 54)) & ALL_ALLOWED_BITS_IN_A, 0);
	}

	BitBoard translatePiece(BitBoard piece, unsigned offset) {
		if (offset == 0) {
			return piece;
		}
		if (offset < 54) {
			return BitBoard(
				(piece.getA() << offset) & ALL_ALLOWED_BITS_IN_A,
				((piece.getA() >> (54 - offset)) | (piece.getB() << offset)) & ALL_ALLOWED_BITS_IN_B
			);
		}
		return BitBoard(0, (piece.getA() << (offset - 54)) & ALL_ALLOWED_BITS_IN_B);
	}

	BitBoard placementAnchorBounds(unsigned max_row, unsigned max_col) {
		const uint64_t anchor_row = (1ULL << (9 - max_col)) - 1;
		auto bounds = BitBoard::empty();
		for (unsigned row = 0; row <= 8 - max_row; ++row) {
			if (row < 6) {
				bounds = bounds | BitBoard(anchor_row << (row * 9), 0);
			} else {
				bounds = bounds | BitBoard(0, anchor_row << ((row - 6) * 9));
			}
		}
		return bounds;
	}

	// Intersect the anchors allowed by every cell of an arbitrary piece. Game
	// pieces use the precomputed form below; this keeps Piece(BitBoard) generic.
	BitBoard validPlacementAnchors(BitBoard board, BitBoard piece) {
		const auto open = ~board;
		auto anchors = BitBoard::full();
		unsigned max_row = 0;
		unsigned max_col = 0;

		auto piece_a = piece.getA();
		while (piece_a != 0) {
			const unsigned offset = (unsigned)__builtin_ctzll(piece_a);
			max_row = std::max(max_row, offset / 9);
			max_col = std::max(max_col, offset % 9);
			anchors = anchors & shiftOpenToAnchor(open, offset);
			piece_a &= piece_a - 1;
		}

		auto piece_b = piece.getB();
		while (piece_b != 0) {
			const unsigned offset = 54 + (unsigned)__builtin_ctzll(piece_b);
			max_row = std::max(max_row, offset / 9);
			max_col = std::max(max_col, offset % 9);
			anchors = anchors & shiftOpenToAnchor(open, offset);
			piece_b &= piece_b - 1;
		}

		return anchors & placementAnchorBounds(max_row, max_col);
	}

	// Stands in for the evaluation of a position a piece does not fit in at
	// all. Small enough that one per piece still cannot overflow a uint64_t,
	// large enough to outweigh any real board evaluation.
	const uint64_t GAME_OVER_PENALTY = UINT64_MAX / (Piece::NUM_PIECES + 1);
}

// === BIT BOARD

BitBoard::BitBoard(uint64_t a, uint64_t b) : a(a), b(b) {}

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
	return (int)std::bitset<64>(a).count() + (int)std::bitset<64>(b).count();
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

namespace {

	const uint64_t A = 1ULL << 0;
	const uint64_t B = 1ULL << 1;
	const uint64_t C = 1ULL << 2;
	const uint64_t D = 1ULL << 3;
	const uint64_t E = 1ULL << 4;
	const uint64_t F = 1ULL << 9;
	const uint64_t G = 1ULL << 10;
	const uint64_t H = 1ULL << 11;
	const uint64_t I = 1ULL << 18;
	const uint64_t J = 1ULL << 19;
	const uint64_t K = 1ULL << 20;
	const uint64_t L = 1ULL << 27;
	const uint64_t M = 1ULL << 36;
	/*
	A B C D E
	F G H
	I J K
	L
	M */
	const uint64_t PIECES[] = {
		// 1 square
		A,

		// 2 squares
		A | B,
		A | F,
		A | G,
		B | F,

		// 3 squares
		A | B | C,
		A | F | I,
		A | G | K,
		C | G | I,
		A | B | F,
		A | F | G,
		B | F | G,
		A | B | G,

		// 4 squares
		A | B | C | D,
		A | F | I | L,
		A | B | F | G,

		// L Shape
		A | F | I | J,
		C | F | G | H,
		A | B | G | J,
		A | B | C | F,

		// Flipped L
		A | B | F | I,
		A | B | C | H,
		B | G | I | J,
		A | F | G | H,


		//  X
		// XXX
		B | F | G | H,
		A | F | G | I,
		B | F | G | J,
		A | B | C | G,

		// XX
		//  XX
		A | B | G | H,
		B | F | G | I,
		A | F | G | J,
		B | C | F | G,

		// 5 squares
		A | B | C | D | E, // #####
		A | F | I | L | M,

		A | F | G | H | I, // #
		A | B | C | G | J, // ###
		B | G | I | J | K, // #
		C | F | G | H | K,

		A | B | C | F | H, // ##
		A | B | G | I | J, // #
		A | B | F | I | J, // ##
		A | C | F | G | H,

		A | B | C | F | I, // #
		A | B | C | H | K, // #
		C | H | I | J | K, // ###
		A | F | I | J | K,

		B | F | G | H | J, // + sign
	};

	struct PiecePlacementData {
		uint64_t bounds_a = 0;
		uint64_t bounds_b = 0;
		std::array<uint8_t, 5> offsets = {};
		uint8_t count = 0;
	};

	// The standard pieces never change. Cache the exact shifts whose open-cell
	// masks must intersect, along with the rectangle in which their anchors fit.
	const auto PIECE_PLACEMENT_DATA = [] {
		std::array<PiecePlacementData, Piece::NUM_PIECES> result;
		for (int index = 0; index < Piece::NUM_PIECES; ++index) {
			auto bits = PIECES[index];
			unsigned max_row = 0;
			unsigned max_col = 0;
			while (bits != 0) {
				const auto offset = (unsigned)__builtin_ctzll(bits);
				result[index].offsets[result[index].count++] = offset;
				max_row = std::max(max_row, offset / 9);
				max_col = std::max(max_col, offset % 9);
				bits &= bits - 1;
			}

			const auto bounds = placementAnchorBounds(max_row, max_col);
			result[index].bounds_a = bounds.getA();
			result[index].bounds_b = bounds.getB();
		}
		return result;
	}();

	uint8_t findPiecePlacementData(BitBoard piece) {
		if (piece.getB() == 0) {
			for (uint8_t index = 0; index < Piece::NUM_PIECES; ++index) {
				if (piece.getA() == PIECES[index]) {
					return index;
				}
			}
		}
		return Piece::NUM_PIECES;
	}

	BitBoard validPlacementAnchors(BitBoard board, const PiecePlacementData &data) {
		const auto open = ~board;
		auto anchors = BitBoard(data.bounds_a, data.bounds_b);
		for (uint8_t index = 0; index < data.count; ++index) {
			anchors = anchors & shiftOpenToAnchor(open, data.offsets[index]);
		}
		return anchors;
	}
}

// ====== Piece
Piece::Piece(uint64_t a) : Piece(BitBoard(a, 0)) {}
Piece::Piece(BitBoard bb) : bb(bb), placement_data_index(findPiecePlacementData(bb)) {}
Piece::Piece() : Piece(BitBoard::empty()) {}
Piece::Piece(uint64_t a, uint8_t index) : bb(BitBoard(a, 0)), placement_data_index(index) {}
BitBoard Piece::getBitBoard() const {
	return bb;
}

PieceSet::PieceSet(Piece p1, Piece p2, Piece p3) {
	pieces[0] = p1;
	pieces[1] = p2;
	pieces[2] = p3;
}


PieceIterator::PieceIterator(uint8_t i) : i(i) {}

PieceIterator PieceIteratorGenerator::begin() const {
	return PieceIterator(0);
}

Piece Piece::byIndex(int index) {
	assert(index >= 0 && index < NUM_PIECES);
	return Piece(PIECES[index], static_cast<uint8_t>(index));
}

bool Piece::operator<(Piece other) const {
	return bb < other.bb;
}

PieceIterator PieceIteratorGenerator::end() const {
	return PieceIterator(Piece::NUM_PIECES);
}

Piece PieceIterator::operator*() const {
	return Piece::byIndex(i);
}


PieceIteratorGenerator Piece::getAll() {
	return PieceIteratorGenerator();
}

void PieceIterator::operator++() {
	i++;
}

bool PieceIterator::operator!=(PieceIterator other) const {
	return i != other.i;
}

// ====== Game State
GameState::GameState(BitBoard bb) : bb(bb) {}
bool GameState::isOver() const {
	return bb == BitBoard::full();
}
BitBoard GameState::getBitBoard() const {
	return bb;
}
NextGameStateIteratorGenerator GameState::nextStates(Piece piece) const {
	return NextGameStateIteratorGenerator(*this, piece);
}

ClearsFirstGameStates GameState::nextStatesClearsFirst(Piece piece) const {
	const auto expected_count = bb.count() + piece.getBitBoard().count();
	ClearsFirstGameStates result(piece.getBitBoard());
	const auto generator = nextStates(piece);
	const auto last = generator.end();
	for (auto it = generator.begin(); it != last; ++it) {
		const auto state = *it;
		result.add(state, it.getAnchor(),
			state.getBitBoard().count() < expected_count);
	}
	result.finish();
	return result;
}

uint64_t GameState::simpleEvalImpl(EvalWeights weights, BitBoard bb, uint64_t max) {
	uint64_t result = 0;

	// Occupied cubes and squares. Square weights only depend on a cube's
	// category, so count each category together instead of popcounting all nine
	// cubes independently. The individual tests are still needed for the fixed
	// cost of making each cube nonempty.
	const auto center_cube = BitBoard::cube(1, 1) & bb;
	const auto side_squares = (BitBoard::cube(0, 1) | BitBoard::cube(1, 0) |
		BitBoard::cube(1, 2) | BitBoard::cube(2, 1)) & bb;
	const auto corner_squares = (BitBoard::cube(0, 0) | BitBoard::cube(0, 2) |
		BitBoard::cube(2, 0) | BitBoard::cube(2, 2)) & bb;
	const int occupied_side_cubes =
		static_cast<bool>(BitBoard::cube(0, 1) & bb) +
		static_cast<bool>(BitBoard::cube(1, 0) & bb) +
		static_cast<bool>(BitBoard::cube(1, 2) & bb) +
		static_cast<bool>(BitBoard::cube(2, 1) & bb);
	const int occupied_corner_cubes =
		static_cast<bool>(BitBoard::cube(0, 0) & bb) +
		static_cast<bool>(BitBoard::cube(0, 2) & bb) +
		static_cast<bool>(BitBoard::cube(2, 0) & bb) +
		static_cast<bool>(BitBoard::cube(2, 2) & bb);
	result += static_cast<bool>(center_cube) * weights.getOccupiedCenterCube();
	result += center_cube.count() * weights.getOccupiedCenterSquare();
	result += occupied_side_cubes * weights.getOccupiedSideCube();
	result += side_squares.count() * weights.getOccupiedSideSquare();
	result += occupied_corner_cubes * weights.getOccupiedCornerCube();
	result += corner_squares.count() * weights.getOccupiedCornerSquare();
	if (result >= max) {
		return max;
	}

	const auto open = ~bb;

	{
		const auto blocked_right = open - open.shiftLeft();
		const auto blocked_left = open - open.shiftRight();
		const auto blocked_up = open - open.shiftDown();
		const auto blocked_down = open - open.shiftUp();

		// Every horizontal or vertical run of open squares has one transition
		// at each end. Count just the upper and left ends, then double them.
		// The four aligned boundary masks do not overlap, so their contributions
		// can be unioned before counting as well.
		const int transition_weight = weights.getTransition();
		const int aligned_transition_weight = weights.getTransitionAligned();
		const int base_transition_weight = std::min(transition_weight,
			aligned_transition_weight);
		const int all_transitions = 2 *
			(blocked_up.count() + blocked_left.count());
		result += all_transitions * base_transition_weight;
		if (result >= max) {
			return max;
		}

		const auto aligned_vertical =
			(blocked_up & (BitBoard::row(3) | BitBoard::row(6))) |
			(blocked_down & (BitBoard::row(2) | BitBoard::row(5)));
		const auto aligned_horizontal =
			(blocked_left & (BitBoard::column(3) | BitBoard::column(6))) |
			(blocked_right & (BitBoard::column(2) | BitBoard::column(5)));
		const int aligned_transitions = aligned_vertical.count() +
			aligned_horizontal.count();
		const int transitions = all_transitions - aligned_transitions;
		result += transitions * (transition_weight - base_transition_weight) +
			aligned_transitions *
				(aligned_transition_weight - base_transition_weight);
		if (result >= max) {
			return max;
		}

		// Cornerish squares carry the next strongest signal. Evaluate them before
		// the cheaper-weighted squashed-square features so a losing candidate can
		// stop without calculating either kind of squash.
		int cornered_empty = 0;
		const auto blocked_up_left = blocked_up & blocked_left;
		cornered_empty += (blocked_up_left -
			(BitBoard::row(0) | BitBoard::column(0))).count();
		const auto blocked_up_right = blocked_up & blocked_right;
		cornered_empty += (blocked_up_right -
			(BitBoard::row(0) | BitBoard::column(8))).count();
		const auto blocked_down_left = blocked_down & blocked_left;
		cornered_empty += (blocked_down_left -
			(BitBoard::row(8) | BitBoard::column(0))).count();
		const auto blocked_down_right = blocked_down & blocked_right;
		cornered_empty += (blocked_down_right -
			(BitBoard::row(8) | BitBoard::column(8))).count();
		result += cornered_empty * weights.getCorneredEmpty();
		if (result >= max) {
			return max;
		}

		const auto edges = BitBoard::row(0) | BitBoard::row(8) |
			BitBoard::column(0) | BitBoard::column(8);
		const auto horizontal_squashed = blocked_right & blocked_left;
		const auto verticle_squashed = blocked_up & blocked_down;
		const int squashed_empty = (horizontal_squashed - edges).count() +
			(verticle_squashed - edges).count();
		const int squashed_empty_at_edge =
			(horizontal_squashed & edges).count() +
			(verticle_squashed & edges).count();
		result += squashed_empty * weights.getSquashedEmpty() +
			squashed_empty_at_edge * weights.getSquashedEmptyAtEdge();
	}

	if (result >= max) {
		return max;
	}

	{
		// Deadly pieces.
		const auto open_left =  open.shiftRight();
		const auto open_2_left = open_left.shiftRight();
		const auto open_right = open.shiftLeft();
		const auto open_2_right = open_right.shiftLeft();
		const auto open_up = open.shiftDown();
		const auto open_2_up = open_up.shiftDown();
		const auto open_down = open.shiftUp();
		const auto open_2_down = open_down.shiftUp();

		const auto open_up_left = open_up.shiftRight();
		const auto open_down_left = open_down.shiftRight();
		const auto open_up_right = open_up.shiftLeft();
		const auto open_down_right = open_down.shiftLeft();


		auto fillable_by_horizontal_3_bar =
		(open & open_left & open_right) | (open & open_left & open_2_left) |
		(open & open_right & open_2_right);
		result += (open &~ fillable_by_horizontal_3_bar).count() * weights.get3Bar();

		auto fillable_by_verticle_3_bar = (open & open_up & open_down) |
		(open & open_up & open_2_up) | (open & open_down & open_2_down);
		result += (open &~fillable_by_verticle_3_bar).count() * weights.get3Bar();

		if (result >= max) {
			return max;
		}

		const int crowded_blocks = std::max(0, bb.count() - 20);
		int scarce_deadly_placements = 0;
		const auto score_deadly_piece = [&](BitBoard deadly_piece_placement) {
			const int placements = deadly_piece_placement.count();
			if (placements == 0) {
				result += weights.getDeadlyPiece();
			}
			if (crowded_blocks != 0 && placements < 4) {
				scarce_deadly_placements += 4 - placements;
			}
		};

		// 5 bars
		score_deadly_piece(open & open_left & open_2_left & open_right & open_2_right);
		score_deadly_piece(open & open_up & open_2_up & open_down & open_2_down);

		// L
		score_deadly_piece(open & open_up & open_2_up & open_right & open_2_right);
		score_deadly_piece(open & open_up & open_2_up & open_left & open_2_left);
		score_deadly_piece(open & open_down & open_2_down & open_right & open_2_right);
		score_deadly_piece(open & open_down & open_2_down & open_left & open_2_left);

		// T
		score_deadly_piece(open & open_left & open_right & open_down & open_2_down);
		score_deadly_piece(open & open_left & open_right & open_up & open_2_up);
		score_deadly_piece(open & open_up & open_down & open_left & open_2_left);
		score_deadly_piece(open & open_up & open_down & open_right & open_2_right);

		// +
		score_deadly_piece(open & open_left & open_right & open_up & open_down);

		// 3 star
		score_deadly_piece(open & open_down_left & open_up_right);
		score_deadly_piece(open & open_up_left & open_down_right);

		// C
		score_deadly_piece(open & open_up & open_down & open_up_right & open_down_right);
		score_deadly_piece(open & open_up & open_down & open_up_left & open_down_left);
		score_deadly_piece(open & open_left & open_right & open_up_left & open_up_right);
		score_deadly_piece(open & open_left & open_right & open_down_left & open_down_right);

		// The other pieces in a deal can consume a hard piece's last few legal
		// placements. That scarcity only becomes dangerous on a crowded board;
		// multiplying the two signals intervenes in the short failure cascade
		// without disturbing the already-tuned sparse-board evaluation.
		if (scarce_deadly_placements != 0) {
			result += (uint64_t)scarce_deadly_placements * crowded_blocks
				* weights.getCrowdedPieceScarcity();
		}
	}

	return std::min(result, max);
}

uint64_t GameState::simpleEval(EvalWeights weights, uint64_t max) const {
	const auto result = simpleEvalImpl(weights, bb, max);

	assert(bb == bb.topDownFlip().topDownFlip());
	assert(max != UINT64_MAX || result == simpleEvalImpl(weights, bb.topDownFlip()));

	return result;
}


NextGameStateIterator::NextGameStateIterator(GameState state, Piece piece_arg) :
	original(state), next(piece_arg.getBitBoard()), piece(piece_arg.getBitBoard()),
	anchors(BitBoard::empty()), anchor(0) {
	if (!(piece == BitBoard::empty()) && !(piece == BitBoard::full())) {
		if (piece_arg.placement_data_index < Piece::NUM_PIECES) {
			anchors = validPlacementAnchors(original.getBitBoard(),
				PIECE_PLACEMENT_DATA[piece_arg.placement_data_index]);
		} else {
			anchors = validPlacementAnchors(original.getBitBoard(), piece);
		}
		setNextPlacement();
	}
}

GameState NextGameStateIterator::operator*() const {
	const auto after_add = original.getBitBoard() | next;

	// Reduce all nine columns together. The first expression covers rows 0-2;
	// shifting it by 27 aligns rows 3-5, and b already begins with rows 6-8.
	const auto top_columns = after_add.a & (after_add.a >> 9) & (after_add.a >> 18);
	const auto completed_columns = top_columns & (top_columns >> 27) &
		after_add.b & (after_add.b >> 9) & (after_add.b >> 18) & ROW_0;

	const auto to_clear = BitBoard(
		completedRows(after_add.a, LEFT_MOST_COLUMN_A) |
			completed_columns * LEFT_MOST_COLUMN_A |
			completedCubes(after_add.a, CUBE_STARTS_A),
		completedRows(after_add.b, LEFT_MOST_COLUMN_B) |
			completed_columns * LEFT_MOST_COLUMN_B |
			completedCubes(after_add.b, CUBE_STARTS_B)
	);

	return GameState(after_add - to_clear);
}

bool NextGameStateIterator::operator!=(NextGameStateIterator other) const {
	return !(other.next == next);
}

void NextGameStateIterator::operator++() {
	if (next == BitBoard::empty()) {
		next = BitBoard::full();
		return;
	}
	setNextPlacement();
}

void NextGameStateIterator::setNextPlacement() {
	unsigned offset;
	if (anchors.a != 0) {
		offset = (unsigned)__builtin_ctzll(anchors.a);
		anchors.a &= anchors.a - 1;
	} else if (anchors.b != 0) {
		offset = 54 + (unsigned)__builtin_ctzll(anchors.b);
		anchors.b &= anchors.b - 1;
	} else {
		next = BitBoard::full();
		return;
	}
	anchor = (uint8_t)offset;
	next = translatePiece(piece, offset);
}

NextGameStateIteratorGenerator::NextGameStateIteratorGenerator(
	GameState state, Piece piece) :
	state(state), piece(piece) {
}

NextGameStateIterator NextGameStateIteratorGenerator::begin() const {
	return NextGameStateIterator(state, piece);
}


NextGameStateIterator NextGameStateIteratorGenerator::end() const {
	return NextGameStateIterator(state, Piece(BitBoard::full()));
}

ClearsFirstGameStates::ClearsFirstGameStates(BitBoard piece) :
	piece(piece), num_clears(0), num_no_clears(0) {}

void ClearsFirstGameStates::add(GameState state, uint8_t anchor, bool cleared) {
	assert(size() < MAX_STATES);
	const auto board = state.getBitBoard();
	auto &count = cleared ? num_clears : num_no_clears;
	auto &destination = cleared ? clears : no_clears;
	auto &destination_anchors = cleared ? clear_anchors : no_clear_anchors;
	destination_anchors[count] = anchor;
	destination[count++] = {board.getA(), board.getB()};
}

void ClearsFirstGameStates::finish() {
	for (uint8_t index = 0; index < num_no_clears; ++index) {
		clears[num_clears + index] = no_clears[index];
		clear_anchors[num_clears + index] = no_clear_anchors[index];
	}
}

ClearsFirstGameStates::Iterator::Iterator(
	const ClearsFirstGameStates *states, size_t index) :
	states(states), index(index) {}

GameState ClearsFirstGameStates::Iterator::operator*() const {
	const auto &stored = states->clears[index];
	return GameState(BitBoard(stored.a, stored.b));
}

BitBoard ClearsFirstGameStates::Iterator::getPlacement() const {
	return translatePiece(states->piece, states->clear_anchors[index]);
}

bool ClearsFirstGameStates::Iterator::operator!=(Iterator other) const {
	return index != other.index;
}

void ClearsFirstGameStates::Iterator::operator++() {
	++index;
}

ClearsFirstGameStates::Iterator ClearsFirstGameStates::begin() const {
	return Iterator(this, 0);
}

ClearsFirstGameStates::Iterator ClearsFirstGameStates::end() const {
	return Iterator(this, size());
}

size_t ClearsFirstGameStates::size() const {
	return static_cast<size_t>(num_clears) + num_no_clears;
}

GameState ClearsFirstGameStates::operator[](size_t index) const {
	assert(index < size());
	return *Iterator(this, index);
}

// ===== Eval Weights
EvalWeights EvalWeights::getDefault() {
	EvalWeights r;
	// 1358 524 6540 4450 18185 2665 204 908 1776 3386 1607 3067 200
	r.weights[0] = 1358; // CUBE;
	r.weights[1] = 524; // SQUASHED_EMPTY;
	r.weights[2] = 6540; // CORNERED_EMPTY;
	r.weights[3] = 4450; // ALTERNATING;
	r.weights[4] = 18185; // DEADLY_PIECE;
	r.weights[5] = 2665; // THREE_BAR.
	r.weights[6] = 204; // 3bar
	r.weights[7] = 908; // Occupied corner cube
	r.weights[8] = 1776; // alternating aligned
	r.weights[9] = 3386; // squashed at edge
	r.weights[10] = 1607; // occupied center square
	r.weights[11] = 3067; // occupied corner square
	r.weights[12] = 200; // hard-piece placement scarcity on crowded boards
	return r;
}
int EvalWeights::getOccupiedSideSquare() const {
	return 2000;
}

int EvalWeights::getOccupiedSideCube() const {
	return weights[0];
}
int EvalWeights::getSquashedEmpty() const {
	return weights[1];
}
int EvalWeights::getCorneredEmpty() const {
	return weights[2];
}
int EvalWeights::getTransition() const {
	return weights[3];
}
int EvalWeights::getDeadlyPiece() const {
	return weights[4];
}
int EvalWeights::get3Bar() const {
	return weights[5];
}
int EvalWeights::getOccupiedCenterCube() const {
	return weights[6];
}
int EvalWeights::getOccupiedCornerCube() const {
	return weights[7];
}
int EvalWeights::getTransitionAligned() const {
	return weights[8];
}
int EvalWeights::getSquashedEmptyAtEdge() const {
	return weights[9];
}
int EvalWeights::getOccupiedCenterSquare() const {
	return weights[10];
}
int EvalWeights::getOccupiedCornerSquare() const {
	return weights[11];
}
int EvalWeights::getCrowdedPieceScarcity() const {
	return weights[12];
}

namespace {
	struct PiecePermutations {
		std::array<std::array<Piece, 3>, 6> values;
		int count = 0;
	};

	// This search enumerates every placement rather than stopping at the first
	// solution. Putting the flexible piece first lets the more constrained next
	// piece reject occupied partial boards before the third-level expansion.
	int countPlacements(GameState game, Piece piece) {
		const auto states = game.nextStates(piece);
		int count = 0;
		for (auto current = states.begin(), finish = states.end();
			current != finish; ++current) {
			++count;
		}
		return count;
	}

	PiecePermutations piecePermutationsMostFlexibleFirst(
		GameState game, PieceSet piece_set, bool need_all) {
		std::sort(piece_set.pieces, piece_set.pieces + 3);
		const int num_pieces = AI::countPieces(piece_set);
		PiecePermutations result;
		do {
			std::copy_n(piece_set.pieces, 3, result.values[result.count].begin());
			++result.count;
		} while (need_all && std::next_permutation(
			piece_set.pieces, piece_set.pieces + num_pieces));

		// Count on the original board once. Blank slots stay at the end because
		// num_pieces excludes them from both the counts and the comparisons.
		//
		// The counts are looked up by matching a piece against this list, so it
		// is a copy rather than result.values[0]: both sorts below move that
		// entry, and a lookup table the sort reorders under itself answers
		// differently depending on how far along the sort is. A comparator that
		// does not answer the same way twice is undefined behaviour, and the
		// answer really did depend on the standard library -- libstdc++ and the
		// libc++ the WASM build uses disagreed, so the shipped solver and the
		// one the weights are trained against played different games.
		const std::array<Piece, 3> counted_order = result.values[0];
		std::array<int, 3> placement_counts = {};
		for (int index = 0; index < num_pieces; ++index) {
			placement_counts[index] = countPlacements(game, counted_order[index]);
		}
		const auto placementsFor = [&](Piece piece) {
			for (int index = 0; index < num_pieces; ++index) {
				if (piece.getBitBoard() == counted_order[index].getBitBoard()) {
					return placement_counts[index];
				}
			}
			return 0;
		};
		if (result.count == 1) {
			std::sort(result.values[0].begin(),
				result.values[0].begin() + num_pieces,
				[&](Piece left, Piece right) {
					const int left_count = placementsFor(left);
					const int right_count = placementsFor(right);
					return left_count != right_count
						? left_count > right_count
						: left < right;
				});
			return result;
		}
		std::sort(result.values.begin(), result.values.begin() + result.count,
			[&](const std::array<Piece, 3> &left,
				const std::array<Piece, 3> &right) {
				for (int index = 0; index < num_pieces; ++index) {
					const int left_count = placementsFor(left[index]);
					const int right_count = placementsFor(right[index]);
					if (left_count != right_count) {
						return left_count > right_count;
					}
					if (left[index] < right[index]) return true;
					if (right[index] < left[index]) return false;
				}
				return false;
			});
		return result;
	}
}


// ====== AI
GameState AI::makeMoveLookahead(EvalWeights weights, GameState game, PieceSet piece_set) {
	uint64_t bestScore = UINT64_MAX;
	auto bestNext = GameState(BitBoard::full());

	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);
	const auto permutations = piecePermutationsMostFlexibleFirst(
		game, piece_set, can_clear_with_2_pieces);

	// Foreach permutation of the pieces.
	for (int permutation = 0; permutation < permutations.count; ++permutation) {
		const bool is_first_permutation = permutation == 0;
		const auto p0 = permutations.values[permutation][0];
		const auto p1 = permutations.values[permutation][1];
		const auto p2 = permutations.values[permutation][2];
		for (const auto after_p0 : game.nextStatesClearsFirst(p0)) {
			for (const auto after_p1 : after_p0.nextStatesClearsFirst(p1)) {
				const auto after_p1_max_count = game.getBitBoard().count() +
					p0.getBitBoard().count() +
					p1.getBitBoard().count();
				// Nothing cleared, so these two placements land on the same
				// board played the other way round, in an ordering the loop
				// also walks. Off on the first ordering. See makeMoveSimple.
				if (!is_first_permutation && p1 < p0 &&
					after_p1.getBitBoard().count() == after_p1_max_count) {
					continue;
				}

				for (const auto after_p2 : after_p1.nextStates(p2)) {
					// No clears anywhere, so this board is the union of three
					// disjoint placements and the first ordering
					// already reached it.
					if (!is_first_permutation &&
						after_p2.getBitBoard().count() == game.getBitBoard().count()
						+ p0.getBitBoard().count() +
						p1.getBitBoard().count() +
						p2.getBitBoard().count()
						) {
						continue;
					}

					uint64_t total_after_p2 = 0;
					bool is_1x1 = true;
					for (const auto p3 : Piece::getAll()) {
						if (is_1x1) {
							// Be pessimistic and pretend we won't get a 1x1.
							is_1x1 = false;
							continue;
						}

						uint64_t best_after_p3 = UINT64_MAX;
						for (const auto after_p3 : after_p2.nextStates(p3)) {
							best_after_p3 = std::min(best_after_p3,
								after_p3.simpleEval(weights));
						}
						if (best_after_p3 == UINT64_MAX) {
							// p3 does not fit anywhere, which is the game
							// ending. Charge a large but finite penalty:
							// summing UINT64_MAX would wrap around and make
							// the position look like the best one on offer.
							best_after_p3 = GAME_OVER_PENALTY;
						}
						total_after_p2 += best_after_p3;
						if (total_after_p2 > bestScore) {
							// after_p3 is worse than the existing candidate already.
							break;
						}
					}

					if (total_after_p2 < bestScore) {
						bestScore = total_after_p2;
						bestNext = after_p2;
					}
				}
			}
		}
	}

	return bestNext;
}

MoveResult AI::makeMoveSimple(const EvalWeights weights, GameState game, PieceSet piece_set) {
	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);
	const auto permutations = piecePermutationsMostFlexibleFirst(
		game, piece_set, can_clear_with_2_pieces);

	MoveResult best;

	// The three levels below are walked with explicit iterators rather than a
	// range-for because a winning candidate needs the placement each level is
	// holding, and only the iterator knows it. Reading one costs a shift, so they
	// are read where a winner is recorded rather than at every node.
	for (int permutation = 0; permutation < permutations.count; ++permutation) {
		const bool is_first_permutation = permutation == 0;
		const auto p0 = permutations.values[permutation][0];
		const auto p1 = permutations.values[permutation][1];
		const auto p2 = permutations.values[permutation][2];
		const auto states_0 = game.nextStatesClearsFirst(p0);
		const auto last_0 = states_0.end();
		for (auto it_0 = states_0.begin(); it_0 != last_0; ++it_0) {
			const auto after_p0 = *it_0;
			const auto states_1 = after_p0.nextStatesClearsFirst(p1);
			const auto last_1 = states_1.end();
			for (auto it_1 = states_1.begin(); it_1 != last_1; ++it_1) {
				const auto after_p1 = *it_1;
				const auto after_p1_max_count = game.getBitBoard().count() +
					p0.getBitBoard().count() +
					p1.getBitBoard().count();
				// Nothing cleared, so these two placements land on the same
				// board played the other way round -- and the other way round
				// is an ordering the loop also walks, in which p0 and p1 are
				// the right way up and this test does not fire. Skip the half
				// of those pairs that are back to front.
				//
				// Never on the first ordering, which the third-level test below
				// takes as having seen every board no clear can move. That used
				// to come for free: the orderings were walked in sorted order,
				// so p1 < p0 could not hold on the first one. Ordering them by
				// how many placements a piece has does not keep p0 and p1
				// sorted, and a first ordering that skipped half its pairs
				// would take those boards down with it.
				if (!is_first_permutation && p1 < p0 &&
					after_p1.getBitBoard().count() == after_p1_max_count) {
					continue;
				}
				const auto states_2 = after_p1.nextStates(p2);
				const auto last_2 = states_2.end();
				for (auto it_2 = states_2.begin(); it_2 != last_2; ++it_2) {
					const auto after_p2 = *it_2;
					// Nothing cleared at any point, so all three placements are
					// disjoint and this board is their union however they were
					// ordered. The pieces start sorted, so the first ordering
					// reaches every one of those boards with nothing skipped.
					if (!is_first_permutation &&
						after_p2.getBitBoard().count() == after_p1_max_count + p2.getBitBoard().count()
						) {
						continue;
					}
					const auto score = after_p2.simpleEval(weights, best.evaluation);
					if (score < best.evaluation) {
						best.evaluation = score;
						best.state = after_p2;
						best.placements[0] = it_0.getPlacement();
						best.placements[1] = it_1.getPlacement();
						best.placements[2] = it_2.getPlacement();
					}
				}
			}
		}
	}

	return best;
}

int AI::countPieces(const PieceSet &piece_set) {
	int num_pieces = 3;
	while (num_pieces > 0 &&
		piece_set.pieces[num_pieces - 1].getBitBoard() == BitBoard::empty()) {
		num_pieces--;
	}
	return num_pieces;
}

bool AI::canClearWith2PiecesOrFewer(GameState game, PieceSet piece_set) {
	// Determine if we need to check permutations.
	for (int i = 0; i < 3; ++i) {
		const auto p0 = piece_set.pieces[i];
		const auto block_count_if_p0_does_not_clear =
			game.getBitBoard().count() + p0.getBitBoard().count();
		for (const auto after_p0 : game.nextStates(p0)) {
			// A piece that clears on its own counts, whether or not any of the
			// other two still fit afterwards. Leaving this to the inner loop
			// would miss the case where the clear is the only thing that makes
			// room, but nothing is left that fits in it.
			if (after_p0.getBitBoard().count() < block_count_if_p0_does_not_clear) {
				return true;
			}
			for (int j = 0; j < 3; ++j) {
				if (i == j) {
					continue;
				}
				const auto p1 = piece_set.pieces[j];
				const auto block_count_if_no_clear = game.getBitBoard().count() +
					p0.getBitBoard().count() +
					p1.getBitBoard().count();
				for (const auto after_p1 : after_p0.nextStates(p1)) {
					if (after_p1.getBitBoard().count() < block_count_if_no_clear) {
						return true;
					}
				}
			}
		}
	}
	return false;
}
