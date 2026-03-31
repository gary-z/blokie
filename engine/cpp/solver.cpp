#include "solver.h"
#include <cassert>
#include <bitset>
#include <algorithm>

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
}

// === BIT BOARD

#if USE_SIMD

BitBoard::BitBoard(uint64_t a, uint64_t b) : data(_mm_set_epi64x(b, a)) {}
BitBoard::BitBoard(__m128i d) : data(d) {}

uint64_t BitBoard::getA() const {
#ifdef __SSE4_1__
	return (uint64_t)_mm_extract_epi64(data, 0);
#else
	alignas(16) uint64_t v[2];
	_mm_store_si128((__m128i*)v, data);
	return v[0];
#endif
}

uint64_t BitBoard::getB() const {
#ifdef __SSE4_1__
	return (uint64_t)_mm_extract_epi64(data, 1);
#else
	alignas(16) uint64_t v[2];
	_mm_store_si128((__m128i*)v, data);
	return v[1];
#endif
}

bool BitBoard::operator==(BitBoard other) const {
	__m128i cmp = _mm_cmpeq_epi64(data, other.data);
	// Both lanes must match: extract and AND
	return _mm_movemask_epi8(cmp) == 0xFFFF;
}

bool BitBoard::operator<(BitBoard other) const {
	uint64_t ta = getA(), tb = getB();
	uint64_t oa = other.getA(), ob = other.getB();
	return ta > oa || (ta == oa && tb > ob);
}

bool BitBoard::at(unsigned r, unsigned c) const {
	return (bool)(BitBoard::row(r) & BitBoard::column(c) & (*this));
}

BitBoard BitBoard::operator|(const BitBoard other) const {
	return BitBoard(_mm_or_si128(data, other.data));
}

BitBoard BitBoard::operator&(const BitBoard other) const {
	return BitBoard(_mm_and_si128(data, other.data));
}

BitBoard BitBoard::operator-(const BitBoard other) const {
	// (*this) &~ other = _mm_andnot_si128(other, *this)
	return BitBoard(_mm_andnot_si128(other.data, data));
}

BitBoard BitBoard::operator~() const {
	__m128i mask = _mm_set_epi64x(ALL_ALLOWED_BITS_IN_B, ALL_ALLOWED_BITS_IN_A);
	return BitBoard(_mm_andnot_si128(data, mask));
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
	return BitBoard(_mm_setzero_si128());
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
	uint64_t a = getA(), b = getB();
	return BitBoard((a & ~RIGHT_MOST_COLUMN_A) << 1, (b & ~RIGHT_MOST_COLUMN_B) << 1);
}

BitBoard BitBoard::shiftLeft() const {
	uint64_t a = getA(), b = getB();
	return BitBoard((a & ~LEFT_MOST_COLUMN_A) >> 1, (b & ~LEFT_MOST_COLUMN_B) >> 1);
}

BitBoard BitBoard::shiftDown() const {
	uint64_t a = getA(), b = getB();
	return BitBoard((a << 9) & ALL_ALLOWED_BITS_IN_A,
		((b << 9) | (a & ROW_5) >> 45) & ALL_ALLOWED_BITS_IN_B);
}

BitBoard BitBoard::shiftUp() const {
	uint64_t a = getA(), b = getB();
	return BitBoard((a >> 9) | ((b & 0x01FFULL) << 45), b >> 9);
}

BitBoard BitBoard::leastSignificantBit() const {
	uint64_t a = getA();
	if (a) {
		return BitBoard(a & -a, 0);
	}
	uint64_t b = getB();
	return BitBoard(0, b & -b);
}

int BitBoard::count() const {
	return (int)__builtin_popcountll(getA()) + (int)__builtin_popcountll(getB());
}

BitBoard::operator bool() const {
#ifdef __SSE4_1__
	return !_mm_testz_si128(data, data);
#else
	return getA() | getB();
#endif
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

#else // !USE_SIMD - original scalar implementation

BitBoard::BitBoard(uint64_t a, uint64_t b) : a(a), b(b) {}

uint64_t BitBoard::getA() const { return a; }
uint64_t BitBoard::getB() const { return b; }

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

#endif // USE_SIMD

// ====== Piece
Piece::Piece(uint64_t a) : bb(BitBoard(a, 0)) {}
Piece::Piece(BitBoard bb) : bb(bb) {};
Piece::Piece() : bb(BitBoard::empty()) {};
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
}

Piece Piece::byIndex(int index) {
	assert(index >= 0 && index < NUM_PIECES);
	return Piece(PIECES[index]);
}

bool Piece::operator<(Piece other) const {
	return bb < other.bb;
}

PieceIterator PieceIteratorGenerator::end() const {
	return PieceIterator(Piece::NUM_PIECES);
}

Piece PieceIterator::operator*() const {
	return Piece(PIECES[i]);
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

std::vector<GameState> GameState::nextStatesClearsFirst(Piece piece) const {
	const auto expected_count = bb.count() + piece.getBitBoard().count();
	std::vector<GameState> clears, no_clears;
	for (const auto state : nextStates(piece)) {
		if (state.getBitBoard().count() < expected_count) {
			clears.push_back(state);
		} else {
			no_clears.push_back(state);
		}
	}
	clears.insert(clears.end(), no_clears.begin(), no_clears.end());
	return clears;
}

uint64_t GameState::simpleEvalImpl(EvalWeights weights, BitBoard bb, uint64_t max) {
	uint64_t result = 0;

	// Occupied cubes.
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			const auto cube = BitBoard::cube(r, c) & bb;
			if (cube) {
				if (r == 1 && c == 1) {
					result += weights.getOccupiedCenterCube();
					result += cube.count() * weights.getOccupiedCenterSquare();
				} else if (r == 1 || c == 1) {
					result += weights.getOccupiedSideCube();
					result += cube.count() * weights.getOccupiedSideSquare();
				} else {
					result += weights.getOccupiedCornerCube();
					result += cube.count() * weights.getOccupiedCornerSquare();
				}

			}
		}
	}

	if (result >= max) {
		return max;
	}

	const auto open = ~bb;

	{
		const auto blocked_right = open - open.shiftLeft();
		const auto blocked_left = open - open.shiftRight();
		const auto blocked_up = open - open.shiftDown();
		const auto blocked_down = open - open.shiftUp();

		const auto edges = BitBoard::row(0) | BitBoard::row(8) | BitBoard::column(0) | BitBoard::column(8);

		// Sandwiched squares.
		{
			const auto horizontal_squashed = (blocked_right & blocked_left);
			result += (horizontal_squashed -edges).count() * weights.getSquashedEmpty();
			result += (horizontal_squashed &edges).count() * weights.getSquashedEmptyAtEdge();
		}

		{
			const auto verticle_squashed = (blocked_up & blocked_down);
			result += (verticle_squashed - edges).count() * weights.getSquashedEmpty();
			result += (verticle_squashed & edges).count() * weights.getSquashedEmptyAtEdge();
		}

		// Cornerish.
		const auto blocked_up_left = blocked_up & blocked_left;
		result += (blocked_up_left - (BitBoard::row(0) | BitBoard::column(0))).count() * weights.getCorneredEmpty();

		const auto blocked_up_right = blocked_up & blocked_right;
		result += (blocked_up_right - (BitBoard::row(0) | BitBoard::column(8))).count() * weights.getCorneredEmpty();

		const auto blocked_down_left = blocked_down & blocked_left;
		result += (blocked_down_left - (BitBoard::row(8) | BitBoard::column(0))).count() * weights.getCorneredEmpty();

		const auto blocked_down_right = blocked_down & blocked_right;
		result += (blocked_down_right - (BitBoard::row(8) | BitBoard::column(8))).count() * weights.getCorneredEmpty();


		{
			const auto aligned_rows = BitBoard::row(3) | BitBoard::row(6);
			result += (blocked_up - aligned_rows).count() * weights.getTransition();
			result += (blocked_up & aligned_rows).count() * weights.getTransitionAligned();
		}
		{
			const auto aligned_rows = BitBoard::row(2) | BitBoard::row(5);
			result += (blocked_down - aligned_rows).count() * weights.getTransition();
			result += (blocked_down & aligned_rows).count() * weights.getTransitionAligned();
		}
		{
			const auto aligned_cols = BitBoard::column(3) | BitBoard::column(6);
			result += (blocked_left - aligned_cols).count() * weights.getTransition();
			result += (blocked_left & aligned_cols).count() * weights.getTransitionAligned();
		}
		{
			const auto aligned_cols = BitBoard::column(2) | BitBoard::column(5);
			result += (blocked_right - aligned_cols).count() * weights.getTransition();
			result += (blocked_right & aligned_cols).count() * weights.getTransitionAligned();
		}
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

		const auto deadly_piece_placements = {
			// 5 bars
			open & open_left & open_2_left & open_right & open_2_right,
			open & open_up & open_2_up & open_down & open_2_down,

			// L
			open & open_up & open_2_up & open_right & open_2_right,
			open & open_up & open_2_up & open_left & open_2_left,
			open & open_down & open_2_down & open_right & open_2_right,
			open & open_down & open_2_down & open_left & open_2_left,

			// T
			open & open_left & open_right & open_down & open_2_down,
			open & open_left & open_right & open_up & open_2_up,
			open & open_up & open_down & open_left & open_2_left,
			open & open_up & open_down & open_right & open_2_right,

			// +
			open & open_left & open_right & open_up & open_down,

			// 3 star
			open & open_down_left & open_up_right,
			open & open_up_left & open_down_right,

			// C
			open & open_up & open_down & open_up_right & open_down_right,
			open & open_up & open_down & open_up_left & open_down_left,
			open & open_left & open_right & open_up_left & open_up_right,
			open & open_left & open_right & open_down_left & open_down_right,
		};

		for (const auto deadly_piece_placement: deadly_piece_placements) {
			if (!deadly_piece_placement) {
				result += weights.getDeadlyPiece();
			}
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


NextGameStateIterator::NextGameStateIterator(GameState state, Piece piece) :
	original(state), next(piece.getBitBoard()), left(piece.getBitBoard()) {
	if (!(piece.getBitBoard() == BitBoard::full()) && !canPlace()) {
		operator++();
	}
}

GameState NextGameStateIterator::operator*() const {
	const auto after_add = original.getBitBoard() | next;
	uint64_t after_a = after_add.getA();
	uint64_t after_b = after_add.getB();
	uint64_t clear_a = 0, clear_b = 0;

	// Clear columns that are completely filled.
	for (int i = 0; i < 9; i++) {
		{
			const auto a_col_bits = RIGHT_MOST_COLUMN_A >> i;
			const auto b_col_bits = RIGHT_MOST_COLUMN_B >> i;
			if ((after_a & a_col_bits) == a_col_bits &&
				(after_b & b_col_bits) == b_col_bits) {
				clear_a |= a_col_bits;
				clear_b |= b_col_bits;
			}
		}
	}

	// Clear rows that are completely filled.
	for (int i = 0; i < 6; ++i) {
		const auto row_bits = ROW_0 << (9 * i);
		if ((after_a & row_bits) == row_bits) {
			clear_a |= row_bits;
		}
	}
	for (int i = 0; i < 3; ++i) {
		const auto row_bits = ROW_0 << (9 * i);
		if ((after_b & row_bits) == row_bits) {
			clear_b |= row_bits;
		}
	}

	// Clear 3x3s that are completely filled.
	for (int r = 0; r < 2; r++) {
		for (int c = 0; c < 3; c++) {
			const auto cube_bits = TOP_LEFT_CUBE << (c * 3 + 27 * r);
			if ((after_a & cube_bits) == cube_bits) {
				clear_a |= cube_bits;
			}
			if (r == 0 && (after_b & cube_bits) == cube_bits) {
				clear_b |= cube_bits;
			}
		}
	}

	return GameState(BitBoard(after_a & ~clear_a, after_b & ~clear_b));
}

bool NextGameStateIterator::operator!=(NextGameStateIterator other) const {
	return !(other.next == next);
}

void NextGameStateIterator::operator++() {
	if (next == BitBoard::empty()) {
		next = BitBoard::full();
		return;
	}

	// We try placing the piece in each row.
	// We try the left most columns first.
	do {
		// We've reached the right edge.
		if (next & BitBoard::column(8)) {

			// We've reached the bottom edge too.
			if (left & BitBoard::row(8)) {
				next = BitBoard::full();
				break;
			}

			// Start at the first column of the next row.
			left = left.shiftDown();
			next = left;
		}
		else {
			next = next.shiftRight();
		}
	} while (!canPlace());
}


bool NextGameStateIterator::canPlace() const {
	return !(next & original.getBitBoard());
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

// ===== Eval Weights
EvalWeights EvalWeights::getDefault() {
	EvalWeights r;
	// 1358 524 6540 4450 18185 2665 204 908 1776 3386 1607 3067
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


// ====== AI
GameState AI::makeMoveLookahead(EvalWeights weights, GameState game, PieceSet piece_set) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);

	uint64_t bestScore = UINT64_MAX;
	auto bestNext = GameState(BitBoard::full());

	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);

	// Foreach permutation of the pieces.
	bool is_first_permutation = true;
	do {
		const auto p0 = piece_set.pieces[0];
		const auto p1 = piece_set.pieces[1];
		const auto p2 = piece_set.pieces[2];
		for (const auto after_p0 : game.nextStatesClearsFirst(p0)) {
			for (const auto after_p1 : after_p0.nextStatesClearsFirst(p1)) {
				const auto after_p1_max_count = game.getBitBoard().count() +
					p0.getBitBoard().count() +
					p1.getBitBoard().count();
				if (p1 < p0 && after_p1.getBitBoard().count() == after_p1_max_count) {
					// Tried this permutation before.
					continue;
				}

				for (const auto after_p2 : after_p1.nextStates(p2)) {
					if (!is_first_permutation &&
						after_p2.getBitBoard().count() == game.getBitBoard().count()
						+ p0.getBitBoard().count() +
						p1.getBitBoard().count() +
						p2.getBitBoard().count()
						) {
						// No clears. This position was seen in a previous permutation.
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
		is_first_permutation = false;
	} while (can_clear_with_2_pieces && std::next_permutation(piece_set.pieces, piece_set.pieces + 3));

	return bestNext;
}

GameState AI::makeMoveSimple(const EvalWeights weights, GameState game, PieceSet piece_set) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);
	const bool is_two_piece_set = piece_set.pieces[2].getBitBoard() == BitBoard::empty();

	const auto can_clear_with_2_pieces = AI::canClearWith2PiecesOrFewer(game, piece_set);

	uint64_t bestScore = UINT64_MAX;
	auto bestNext = GameState(BitBoard::full());

	bool is_first_permutation = true;
	do {
		const auto p0 = piece_set.pieces[0];
		const auto p1 = piece_set.pieces[1];
		const auto p2 = piece_set.pieces[2];
		for (const auto after_p0 : game.nextStatesClearsFirst(p0)) {
			for (const auto after_p1 : after_p0.nextStatesClearsFirst(p1)) {
				const auto after_p1_max_count = game.getBitBoard().count() +
					p0.getBitBoard().count() +
					p1.getBitBoard().count();
				if (p1 < p0 && after_p1.getBitBoard().count() == after_p1_max_count) {
					// Tried this permutation before.
					continue;
				}
				for (const auto after_p2 : after_p1.nextStates(p2)) {
					if (!is_first_permutation &&
						after_p2.getBitBoard().count() == after_p1_max_count + p2.getBitBoard().count()
						) {
						// Tried this permutation before.
						continue;
					}
					const auto score = after_p2.simpleEval(weights, bestScore);
					if (score < bestScore) {
						bestScore = score;
						bestNext = after_p2;
					}
				}
			}
		}
		is_first_permutation = false;
	} while (can_clear_with_2_pieces &&
		std::next_permutation(piece_set.pieces, piece_set.pieces + (is_two_piece_set ? 2 : 3))
	);

	return bestNext;
}

bool AI::canClearWith2PiecesOrFewer(GameState game, PieceSet piece_set) {
	// Determine if we need to check permutations.
	for (int i = 0; i < 3; ++i) {
		const auto p0 = piece_set.pieces[i];
		for (const auto after_p0 : game.nextStates(p0)) {
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
