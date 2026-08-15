#include "game.h"
#include <cassert>
#include <bit>
#include <algorithm>
#include <array>
#include <cstring>

// Tell the optimizer something it cannot prove, in whatever spelling the
// compiler building this understands.
//
// [[assume]] is C++23, and the compilers that can build this engine arrived at
// it years apart: GCC in 13, Clang in 19. An attribute an older one has never
// heard of is a warning rather than an error, which sounds harmless until
// -Werror turns it into a failed build -- so writing it plainly means the
// engine only compiles on the newest half of the toolchains that could
// otherwise run it. Every one of them has had the same instruction under its
// own name for far longer.
//
// Dropping the hint where none of the spellings exist is safe. It only ever
// narrowed the range the optimizer assumed for a value that already stays in
// that range, so a compiler that does not get told produces slower code, never
// different answers.
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

namespace {
	using namespace bitboard_detail;

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
				(open.getA() >> offset) | (open.getB() << (54 - offset)),
				open.getB() >> offset
			);
		}
		return BitBoard(open.getB() >> (offset - 54), 0);
	}

	BitBoard translatePiece(uint64_t piece, unsigned offset) {
		if (offset == 0) {
			return BitBoard(piece, 0);
		}
		if (offset < 54) {
			return BitBoard(
				(piece << offset) & ALL_ALLOWED_BITS_IN_A,
				piece >> (54 - offset)
			);
		}
		return BitBoard(0, piece << (offset - 54));
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
			const unsigned offset = (unsigned)std::countr_zero(piece_a);
			max_row = std::max(max_row, offset / 9);
			max_col = std::max(max_col, offset % 9);
			anchors = anchors & shiftOpenToAnchor(open, offset);
			piece_a &= piece_a - 1;
		}

		auto piece_b = piece.getB();
		while (piece_b != 0) {
			const unsigned offset = 54 + (unsigned)std::countr_zero(piece_b);
			max_row = std::max(max_row, offset / 9);
			max_col = std::max(max_col, offset % 9);
			anchors = anchors & shiftOpenToAnchor(open, offset);
			piece_b &= piece_b - 1;
		}

		return anchors & placementAnchorBounds(max_row, max_col);
	}
}

namespace {

	constexpr uint64_t A = 1ULL << 0;
	constexpr uint64_t B = 1ULL << 1;
	constexpr uint64_t C = 1ULL << 2;
	constexpr uint64_t D = 1ULL << 3;
	constexpr uint64_t E = 1ULL << 4;
	constexpr uint64_t F = 1ULL << 9;
	constexpr uint64_t G = 1ULL << 10;
	constexpr uint64_t H = 1ULL << 11;
	constexpr uint64_t I = 1ULL << 18;
	constexpr uint64_t J = 1ULL << 19;
	constexpr uint64_t K = 1ULL << 20;
	constexpr uint64_t L = 1ULL << 27;
	constexpr uint64_t M = 1ULL << 36;
	/*
	A B C D E
	F G H
	I J K
	L
	M */
	constexpr uint64_t PIECES[] = {
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

	constexpr PiecePlacementData makePiecePlacementData(uint64_t bits) {
		PiecePlacementData result;
		unsigned max_row = 0;
		unsigned max_col = 0;
		while (bits != 0) {
			unsigned offset = 0;
			while ((bits & (1ULL << offset)) == 0) ++offset;
			result.offsets[result.count++] = (uint8_t)offset;
			max_row = std::max(max_row, offset / 9);
			max_col = std::max(max_col, offset % 9);
			bits &= bits - 1;
		}

		const uint64_t anchor_row = (1ULL << (9 - max_col)) - 1;
		for (unsigned row = 0; row <= 8 - max_row; ++row) {
			if (row < 6) {
				result.bounds_a |= anchor_row << (row * 9);
			} else {
				result.bounds_b |= anchor_row << ((row - 6) * 9);
			}
		}
		return result;
	}

	// The standard pieces never change. Build the exact shifts whose open-cell
	// masks must intersect, along with the rectangle in which their anchors fit,
	// at compile time so the finished table can live in read-only data.
	constexpr auto PIECE_PLACEMENT_DATA = []() constexpr {
		std::array<PiecePlacementData, Piece::NUM_PIECES> result;
		for (int index = 0; index < Piece::NUM_PIECES; ++index) {
			result[index] = makePiecePlacementData(PIECES[index]);
		}
		return result;
	}();

	uint8_t findPiecePlacementData(uint64_t piece) {
		for (uint8_t index = 0; index < Piece::NUM_PIECES; ++index) {
			if (piece == PIECES[index]) {
				return index;
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
Piece::Piece(uint64_t a) : bits(a), placement_data_index(findPiecePlacementData(a)) {}
Piece::Piece(BitBoard bb) : bits(bb.getA()),
	placement_data_index(findPiecePlacementData(bb.getA())) {
	// BitBoard::full() is the private iterator end sentinel. Every real piece,
	// including shapes reconstructed at the WASM boundary, is normalized into
	// the first word.
	assert(bb.getB() == 0 || bb == BitBoard::full());
	if (bb == BitBoard::full()) {
		placement_data_index = Piece::NUM_PIECES + 1;
	}
}
Piece::Piece() : Piece(uint64_t{0}) {}
Piece::Piece(uint64_t a, uint8_t index) : bits(a), placement_data_index(index) {}
BitBoard Piece::getBitBoard() const {
	return placement_data_index == Piece::NUM_PIECES + 1 ?
		BitBoard::full() : BitBoard(bits, 0);
}
int Piece::count() const {
	return std::popcount(bits);
}
bool Piece::isEmpty() const {
	return bits == 0;
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
	return bits > other.bits;
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
	ClearsFirstGameStates result(piece.bits);
	const auto generator = nextStates(piece);
	const auto last = generator.end();
	for (auto it = generator.begin(); it != last; ++it) {
		const auto state = *it;
		result.add(state, it.getAnchor(), it.didClear());
	}
	result.finish();
	return result;
}


NextGameStateIterator::NextGameStateIterator(GameState state, Piece piece_arg) :
	original(state), next(piece_arg.getBitBoard()), anchors(BitBoard::empty()),
	piece(piece_arg.bits),
	anchor(0), cleared(false) {
	if (piece_arg.placement_data_index == Piece::NUM_PIECES + 1) {
		return;
	}
	if (piece != 0) {
		if (piece_arg.placement_data_index < Piece::NUM_PIECES) {
			anchors = validPlacementAnchors(original.getBitBoard(),
				PIECE_PLACEMENT_DATA[piece_arg.placement_data_index]);
		} else {
			anchors = validPlacementAnchors(original.getBitBoard(), BitBoard(piece, 0));
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

	cleared = static_cast<bool>(to_clear);
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
		offset = (unsigned)std::countr_zero(anchors.a);
		anchors.a &= anchors.a - 1;
	} else if (anchors.b != 0) {
		offset = 54 + (unsigned)std::countr_zero(anchors.b);
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

ClearsFirstGameStates::ClearsFirstGameStates(uint64_t piece) :
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
	std::memcpy(clears.data() + num_clears, no_clears.data(),
		num_no_clears * sizeof(StoredState));
	std::memcpy(clear_anchors.data() + num_clears, no_clear_anchors.data(),
		num_no_clears * sizeof(uint8_t));
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

bool ClearsFirstGameStates::Iterator::didClear() const {
	return index < states->num_clears;
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
