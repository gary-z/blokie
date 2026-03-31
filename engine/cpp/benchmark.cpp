#include "solver.h"
#include <chrono>
#include <cstdio>
#include <cstdint>

// sfc32 PRNG matching the JS implementation
struct SFC32 {
	uint32_t a, b, c, d;
	SFC32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) : a(a), b(b), c(c), d(d) {}
	double next() {
		uint32_t t = (a + b) + d;
		d = d + 1;
		a = b ^ (b >> 9);
		b = c + (c << 3);
		c = (c << 21) | (c >> 11);
		c = c + t;
		return (double)(t) / 4294967296.0;
	}
};

int main() {
	const int NUM_MOVES = 10000;
	auto weights = EvalWeights::getDefault();
	SFC32 rng(1, 2, 3, 4);

	GameState game(BitBoard::empty());

	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < NUM_MOVES; ++i) {
		Piece pieces[3];
		for (int j = 0; j < 3; ++j) {
			int idx = (int)(rng.next() * Piece::NUM_PIECES);
			if (idx >= Piece::NUM_PIECES) idx = Piece::NUM_PIECES - 1;
			pieces[j] = Piece::byIndex(idx);
		}

		PieceSet ps(pieces[0], pieces[1], pieces[2]);
		game = AI::makeMoveSimple(weights, game, ps);

		if (game.isOver()) {
			game = GameState(BitBoard::empty());
		}
	}

	auto end = std::chrono::high_resolution_clock::now();
	double elapsed = std::chrono::duration<double>(end - start).count();

	printf("%d moves in %.2f seconds (%.0f moves/sec)\n", NUM_MOVES, elapsed, NUM_MOVES / elapsed);
#if USE_SIMD
	printf("SIMD: enabled\n");
#else
	printf("SIMD: disabled\n");
#endif

	return 0;
}
