#include "solver.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

struct GameResult {
    uint64_t moves;
};

GameResult playOneGame(uint64_t seed, const EvalWeights& weights) {
    // Seeded per-game RNG so results are reproducible regardless of which
    // thread picks up the game_id.
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);

    GameState game(BitBoard::empty());
    uint64_t moves = 0;
    while (!game.isOver()) {
        Piece p0 = Piece::byIndex(piece_dist(rng));
        Piece p1 = Piece::byIndex(piece_dist(rng));
        Piece p2 = Piece::byIndex(piece_dist(rng));
        game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2));
        ++moves;
    }
    return {moves};
}

}  // namespace

int main(int argc, char** argv) {
    int num_games = 8;
    if (argc > 1) {
        num_games = std::atoi(argv[1]);
        if (num_games <= 0) {
            std::fprintf(stderr, "num_games must be positive\n");
            return 1;
        }
    }
    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;
    unsigned num_threads = std::min<unsigned>(hw_threads, (unsigned)num_games);

    const auto weights = EvalWeights::getDefault();

    std::atomic<int> next_game_id{0};
    std::vector<GameResult> results(num_games);
    std::mutex log_mutex;

    std::fprintf(stderr, "Running %d games across %u threads...\n",
                 num_games, num_threads);

    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                int id = next_game_id.fetch_add(1, std::memory_order_relaxed);
                if (id >= num_games) return;

                const auto g_start = std::chrono::steady_clock::now();
                GameResult r = playOneGame((uint64_t)id + 1, weights);
                const auto g_end = std::chrono::steady_clock::now();
                const double g_secs =
                    std::chrono::duration<double>(g_end - g_start).count();

                results[id] = r;
                {
                    std::lock_guard<std::mutex> lk(log_mutex);
                    std::fprintf(stderr,
                                 "[t%u] game %d: %llu moves (%.1fs)\n",
                                 t, id, (unsigned long long)r.moves, g_secs);
                    std::fflush(stderr);
                }
            }
        });
    }

    for (auto& w : workers) w.join();

    const auto end = std::chrono::steady_clock::now();
    const double total_secs =
        std::chrono::duration<double>(end - start).count();

    // Aggregate stats.
    double sum = 0.0;
    double sum_sq = 0.0;
    uint64_t min_moves = UINT64_MAX;
    uint64_t max_moves = 0;
    for (const auto& r : results) {
        sum += (double)r.moves;
        sum_sq += (double)r.moves * (double)r.moves;
        if (r.moves < min_moves) min_moves = r.moves;
        if (r.moves > max_moves) max_moves = r.moves;
    }
    const double mean = sum / num_games;
    const double var = num_games > 1
        ? (sum_sq - num_games * mean * mean) / (num_games - 1)
        : 0.0;
    const double stddev = std::sqrt(std::max(0.0, var));
    const double sem = num_games > 1 ? stddev / std::sqrt((double)num_games) : 0.0;

    std::printf("games=%d  mean=%.1f  stddev=%.1f  sem=%.1f  min=%llu  max=%llu  wall=%.1fs\n",
                num_games, mean, stddev, sem,
                (unsigned long long)min_moves, (unsigned long long)max_moves,
                total_secs);
    return 0;
}
