#include "solver.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// SFC32 RNG - mirrors the sfc32 implementation in engine/blokie.js so
// piece sequences match between the native benchmark and the JS benchmark.
struct Sfc32 {
    uint32_t a, b, c, d;
    Sfc32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
        : a(a), b(b), c(c), d(d) {}

    // Returns a number in [0, 1), matching the JS sfc32 output.
    double next() {
        uint32_t t = (a + b) + d;
        d = d + 1;
        a = b ^ (b >> 9);
        b = c + (c << 3);
        c = (c << 21) | (c >> 11);
        c = c + t;
        return (double)t / 4294967296.0;
    }
};

static GameState newGame() {
    return GameState(BitBoard::empty());
}

int main(int argc, char** argv) {
    int num_moves = 10000;
    if (argc > 1) {
        num_moves = std::atoi(argv[1]);
        if (num_moves <= 0) {
            std::fprintf(stderr, "num_moves must be positive\n");
            return 1;
        }
    }

    const auto weights = EvalWeights::getDefault();
    Sfc32 rng(1, 2, 3, 4);

    GameState game = newGame();

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_moves; ++i) {
        Piece pieces[3];
        for (int j = 0; j < 3; ++j) {
            int idx = (int)(rng.next() * Piece::NUM_PIECES);
            pieces[j] = Piece::byIndex(idx);
        }
        PieceSet ps(pieces[0], pieces[1], pieces[2]);
        game = AI::makeMoveSimple(weights, game, ps);
        if (game.isOver()) {
            game = newGame();
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - start).count();

    std::printf("%d moves in %.2f seconds (%.0f moves/sec)\n",
                num_moves, seconds,
                num_moves / seconds);
    return 0;
}
