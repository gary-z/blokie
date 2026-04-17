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
#include <string>
#include <thread>
#include <vector>

namespace {

struct GameResult {
    uint64_t seed;
    uint64_t moves;
    bool died;  // true if board filled (natural death); false if capped
};

GameResult playOneGame(uint64_t seed, const EvalWeights& weights, uint64_t cap) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);

    GameState game(BitBoard::empty());
    uint64_t moves = 0;
    while (!game.isOver() && (cap == 0 || moves < cap)) {
        Piece p0 = Piece::byIndex(piece_dist(rng));
        Piece p1 = Piece::byIndex(piece_dist(rng));
        Piece p2 = Piece::byIndex(piece_dist(rng));
        game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2));
        ++moves;
    }
    return {seed, moves, game.isOver()};
}

double percentile(std::vector<uint64_t> sorted, double p) {
    if (sorted.empty()) return 0.0;
    double idx = p * (sorted.size() - 1);
    size_t lo = (size_t)std::floor(idx);
    size_t hi = (size_t)std::ceil(idx);
    double frac = idx - lo;
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

bool parseWeights(const char* s, EvalWeights& out) {
    // Accept comma- or space-separated ints; need exactly NUM_WEIGHTS values.
    std::string buf(s);
    for (char& c : buf) if (c == ',') c = ' ';
    int idx = 0;
    const char* p = buf.c_str();
    char* endp;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        long v = std::strtol(p, &endp, 10);
        if (endp == p) return false;
        if (idx >= EvalWeights::NUM_WEIGHTS) return false;
        out.weights[idx++] = (int)v;
        p = endp;
    }
    return idx == EvalWeights::NUM_WEIGHTS;
}

}  // namespace

int main(int argc, char** argv) {
    int num_games = 8;
    uint64_t seed_base = 0;   // 0 => non-deterministic
    uint64_t cap = 0;         // 0 => no cap
    EvalWeights weights = EvalWeights::getDefault();
    bool weights_overridden = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--seed-base") && i + 1 < argc) {
            seed_base = std::strtoull(argv[++i], nullptr, 10);
        } else if (!std::strcmp(a, "--cap") && i + 1 < argc) {
            cap = std::strtoull(argv[++i], nullptr, 10);
        } else if (!std::strcmp(a, "--weights") && i + 1 < argc) {
            if (!parseWeights(argv[++i], weights)) {
                std::fprintf(stderr, "--weights expects %d comma/space-separated ints\n",
                             EvalWeights::NUM_WEIGHTS);
                return 1;
            }
            weights_overridden = true;
        } else {
            int n = std::atoi(a);
            if (n > 0) num_games = n;
        }
    }

    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;
    unsigned num_threads = std::min<unsigned>(hw_threads, (unsigned)num_games);

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

    std::fprintf(stderr,
                 "Running %d games across %u threads (%s seeds, cap=%llu, weights=%s)...\n",
                 num_games, num_threads,
                 seed_base == 0 ? "random" : "deterministic",
                 (unsigned long long)cap,
                 weights_overridden ? "custom" : "default");

    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                int id = next_game_id.fetch_add(1, std::memory_order_relaxed);
                if (id >= num_games) return;

                const auto g_start = std::chrono::steady_clock::now();
                GameResult r = playOneGame(seeds[id], weights, cap);
                const auto g_end = std::chrono::steady_clock::now();
                const double g_secs =
                    std::chrono::duration<double>(g_end - g_start).count();

                results[id] = r;
                {
                    std::lock_guard<std::mutex> lk(log_mutex);
                    std::fprintf(stderr,
                                 "[t%u] game %d seed=%llu: %llu moves %s (%.1fs)\n",
                                 t, id,
                                 (unsigned long long)r.seed,
                                 (unsigned long long)r.moves,
                                 r.died ? "died" : "capped",
                                 g_secs);
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
    uint64_t deaths = 0;
    double exposure = 0.0;  // sum of min(moves, cap) = sum of moves since we already enforce cap
    double sum_sq = 0.0;
    uint64_t min_moves = UINT64_MAX;
    uint64_t max_moves = 0;
    for (const auto& r : results) {
        moves.push_back(r.moves);
        exposure += (double)r.moves;
        sum_sq += (double)r.moves * (double)r.moves;
        if (r.died) deaths++;
        if (r.moves < min_moves) min_moves = r.moves;
        if (r.moves > max_moves) max_moves = r.moves;
    }
    std::sort(moves.begin(), moves.end());
    const double mean = exposure / num_games;
    const double var = num_games > 1
        ? (sum_sq - num_games * mean * mean) / (num_games - 1)
        : 0.0;
    const double stddev = std::sqrt(std::max(0.0, var));
    const double sem = num_games > 1 ? stddev / std::sqrt((double)num_games) : 0.0;

    // Per-game output on stdout: "moves\tdied".  The `died` column (0/1) lets
    // downstream tools compute MLE under right-censoring without re-parsing.
    for (const auto& r : results) {
        std::printf("%llu\t%d\n", (unsigned long long)r.moves, r.died ? 1 : 0);
    }

    // MLE under constant-hazard model with right censoring:
    //   lambda_hat = deaths / total_exposure
    //   mean_hat   = 1 / lambda_hat = total_exposure / deaths
    //   SE(mean_hat) ~= mean_hat / sqrt(deaths)
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
    if (deaths > 0) {
        const double mean_hat = exposure / (double)deaths;
        const double se_mean_hat = mean_hat / std::sqrt((double)deaths);
        std::fprintf(stderr,
                     "deaths=%llu  censored=%llu  exposure=%.0f\n"
                     "lambda_hat=%.3e  mean_hat=%.1f  SE(mean_hat)=%.1f\n",
                     (unsigned long long)deaths,
                     (unsigned long long)(num_games - deaths),
                     exposure,
                     (double)deaths / exposure, mean_hat, se_mean_hat);
    } else {
        std::fprintf(stderr,
                     "deaths=0  all %d games censored at cap=%llu (no MLE)\n",
                     num_games, (unsigned long long)cap);
    }
    return 0;
}
