#pragma once
#include "game.h"
#include <cstdint>

// What a search settled on: the board it would leave, and the placements that
// reach it, in the order they are played. Replaying them in that order is
// therefore always legal, which orderings that depend on an earlier clear
// making room are not.
//
// The evaluation is UINT64_MAX when nothing was found, which is the search
// being unable to place every piece it was given. The board is then full and
// every placement empty, so a caller that only plays moves reads the result the
// same way whether or not it checks.
class MoveResult {
public:
	GameState state = GameState(BitBoard::full());
	BitBoard placements[3] = {
		BitBoard::empty(), BitBoard::empty(), BitBoard::empty()};
	uint64_t evaluation = UINT64_MAX;
};

class AI {
public:
	// Return the state with the lowest score after placing the 3 pieces.
	static MoveResult makeMoveSimple(EvalWeights weights, GameState state, PieceSet piece_set);
	// Same search specialized for getDefault(), so its inlined evaluation can
	// use immediate weights instead of loading an EvalWeights object.
	static MoveResult makeMoveSimpleDefault(GameState state, PieceSet piece_set);

	// Whether all three pieces can be placed in some order. Unlike the move
	// search, this stops at the first legal line of play and does no evaluation.
	// It is useful for sampling the probability that a dealt set ends a game.
	static bool tripleFits(GameState state, PieceSet piece_set);

	// How many of the three slots hold a piece. Blank slots sort last, so this
	// counts the leading run of a sorted piece set and is only meaningful for
	// one. A blank slot is placed by doing nothing, so it is also the number of
	// slots whose ordering can change anything.
	static int countPieces(const PieceSet &piece_set);

	// Is there an order and a pair of placements that clears a line before the
	// third piece is played? When there is not, every way of playing all three
	// pieces clears at most on the last one, so the board they end on is the
	// union of the three placements however they are ordered -- and a search
	// that walks one ordering has already seen every board the other five
	// could reach. The searches above use that to skip the other orderings.
	static bool canClearWith2PiecesOrFewer(GameState state, PieceSet piece_set);
};
