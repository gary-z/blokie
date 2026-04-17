#include "solver.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

struct GameResult {
    uint64_t seed;
    uint64_t moves;
};

GameResult playOneGame(uint64_t seed, const EvalWeights& weights) {
    // Each game has its own RNG; callers pass a non-deterministic seed by
    // default. Seeds are reported so any individual game can be reproduced.
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
    return {seed, moves};
}

double percentile(std::vector<uint64_t> sorted, double p) {
    if (sorted.empty()) return 0.0;
    double idx = p * (sorted.size() - 1);
    size_t lo = (size_t)std::floor(idx);
    size_t hi = (size_t)std::ceil(idx);
    double frac = idx - lo;
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

}  // namespace

int main(int argc, char** argv) {
    int num_games = 8;
    uint64_t seed_base = 0;  // 0 => non-deterministic via random_device
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed-base") == 0 && i + 1 < argc) {
            seed_base = std::strtoull(argv[++i], nullptr, 10);
        } else {
            int n = std::atoi(argv[i]);
            if (n > 0) num_games = n;
        }
    }

    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;
    unsigned num_threads = std::min<unsigned>(hw_threads, (unsigned)num_games);

    const auto weights = EvalWeights::getDefault();

    // Pre-generate per-game seeds so each game's seed is stable.
    std::vector<uint64_t> seeds(num_games);
    if (seed_base == 0) {
        std::random_device rd;
        for (int i = 0; i < num_games; ++i) {
            seeds[i] = ((uint64_t)rd() << 32) | rd();
        }
    } else {
        for (int i = 0; i < num_games; ++i) seeds[i] = seed_base + i;
    }

    std::atomic<int> next_game_id{0};
    std::vector<GameResult> results(num_games);
    std::mutex log_mutex;

    std::fprintf(stderr, "Running %d games across %u threads (%s seeds)...\n",
                 num_games, num_threads,
                 seed_base == 0 ? "random" : "deterministic");

    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                int id = next_game_id.fetch_add(1, std::memory_order_relaxed);
                if (id >= num_games) return;

                const auto g_start = std::chrono::steady_clock::now();
                GameResult r = playOneGame(seeds[id], weights);
                const auto g_end = std::chrono::steady_clock::now();
                const double g_secs =
                    std::chrono::duration<double>(g_end - g_start).count();

                results[id] = r;
                {
                    std::lock_guard<std::mutex> lk(log_mutex);
                    std::fprintf(stderr,
                                 "[t%u] game %d seed=%llu: %llu moves (%.1fs)\n",
                                 t, id,
                                 (unsigned long long)r.seed,
                                 (unsigned long long)r.moves, g_secs);
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
    std::vector<uint64_t> moves;
    moves.reserve(num_games);
    double sum = 0.0;
    double sum_sq = 0.0;
    uint64_t min_moves = UINT64_MAX;
    uint64_t max_moves = 0;
    for (const auto& r : results) {
        moves.push_back(r.moves);
        sum += (double)r.moves;
        sum_sq += (double)r.moves * (double)r.moves;
        if (r.moves < min_moves) min_moves = r.moves;
        if (r.moves > max_moves) max_moves = r.moves;
    }
    std::sort(moves.begin(), moves.end());
    const double mean = sum / num_games;
    const double var = num_games > 1
        ? (sum_sq - num_games * mean * mean) / (num_games - 1)
        : 0.0;
    const double stddev = std::sqrt(std::max(0.0, var));
    const double sem = num_games > 1 ? stddev / std::sqrt((double)num_games) : 0.0;

    // Raw per-game move counts on stdout so downstream tools can analyze.
    for (uint64_t m : moves) {
        std::printf("%llu\n", (unsigned long long)m);
    }

    std::fprintf(stderr,
                 "\ngames=%d  mean=%.1f  stddev=%.1f  sem=%.1f\n"
                 "p25=%.0f  p50=%.0f  p75=%.0f  p90=%.0f  p95=%.0f\n"
                 "min=%llu  max=%llu  wall=%.1fs\n",
                 num_games, mean, stddev, sem,
                 percentile(moves, 0.25), percentile(moves, 0.50),
                 percentile(moves, 0.75), percentile(moves, 0.90),
                 percentile(moves, 0.95),
                 (unsigned long long)min_moves, (unsigned long long)max_moves,
                 total_secs);
    return 0;
}
