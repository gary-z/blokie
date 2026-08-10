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

struct Options {
    int num_games = 8;
    unsigned num_threads = 0;  // 0 => one worker per available logical CPU
    uint64_t seed_base = 0;    // 0 => non-deterministic via random_device
    uint64_t max_moves = 0;    // 0 => play every game out
    uint64_t burn_in = 0;      // leading moves left out of the hazard fit
    uint64_t hazard_bins = 0;  // bin width of the hazard table, 0 => no table
};

struct GameResult {
    uint64_t seed;
    uint64_t moves;  // moves attempted: the one it died on, or the cutoff
    bool ended;      // false when the cutoff stopped a game still in play
};

// Deaths and exposure split by how far into a game they happened. The point of
// a cutoff is that the chance of dying settles to a constant once the board has
// forgotten it started empty, and a game stopped early still tells you it
// survived everything it was dealt. This is what shows whether that constant
// really has arrived, and by which move -- read down the hazard column and look
// for where it stops trending.
struct HazardTable {
    uint64_t bin_width = 0;
    std::vector<uint64_t> exposure;
    std::vector<uint64_t> deaths;

    void ensure(size_t bins) {
        if (exposure.size() < bins) {
            exposure.resize(bins, 0);
            deaths.resize(bins, 0);
        }
    }

    void add(const GameResult& r) {
        if (bin_width == 0 || r.moves == 0) return;
        // Attempts are numbered 1..moves, so attempt k sits at offset k - 1.
        const size_t last = (size_t)((r.moves - 1) / bin_width);
        ensure(last + 1);
        for (size_t j = 0; j <= last; ++j) {
            const uint64_t lo = (uint64_t)j * bin_width;
            const uint64_t hi = std::min(lo + bin_width, r.moves);
            exposure[j] += hi - lo;
        }
        if (r.ended) deaths[last]++;
    }

    void merge(const HazardTable& other) {
        ensure(other.exposure.size());
        for (size_t j = 0; j < other.exposure.size(); ++j) {
            exposure[j] += other.exposure[j];
            deaths[j] += other.deaths[j];
        }
    }
};

// Plays until the game ends, or until max_moves have been attempted when
// max_moves is non-zero. A game stopped at the cutoff is right-censored: all it
// reports is that it outlasted every set it was dealt, which is what the hazard
// estimate needs and is all a longer game would have added per move anyway.
GameResult playOneGame(uint64_t seed, const EvalWeights& weights,
                       uint64_t max_moves) {
    // Each game has its own RNG; callers pass a non-deterministic seed by
    // default. Seeds are reported so any individual game can be reproduced.
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);

    GameState game(BitBoard::empty());
    uint64_t moves = 0;
    while (!game.isOver()) {
        if (max_moves != 0 && moves >= max_moves) return {seed, moves, false};
        Piece p0 = Piece::byIndex(piece_dist(rng));
        Piece p1 = Piece::byIndex(piece_dist(rng));
        Piece p2 = Piece::byIndex(piece_dist(rng));
        game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2)).state;
        ++moves;
    }
    return {seed, moves, true};
}

double percentile(const std::vector<uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    double idx = p * (sorted.size() - 1);
    size_t lo = (size_t)std::floor(idx);
    size_t hi = (size_t)std::ceil(idx);
    double frac = idx - lo;
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [num_games] [options]\n"
        "\n"
        "  --threads N       use N worker threads (default: all available,\n"
        "                    capped at the number of games)\n"
        "  --seed-base S     start per-game seeds at S instead of drawing them\n"
        "                    at random, so a run repeats exactly\n"
        "  --max-moves X     stop a game after X moves instead of playing it\n"
        "                    out. Games stopped this way are right-censored,\n"
        "                    and still count toward the hazard rate below\n"
        "  --burn-in B       leave each game's first B moves out of the hazard\n"
        "                    fit, for when the early board is not yet typical\n"
        "  --hazard-bins W   print deaths and exposure per W moves of depth,\n"
        "                    which is what says whether the hazard is flat\n"
        "\n"
        "stdout gets one line per game: moves, whether it ended, and its seed.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            opt.num_threads = (unsigned)std::strtoul(argv[++i], nullptr, 10);
            if (opt.num_threads == 0) {
                std::fprintf(stderr, "--threads must be positive\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--seed-base") == 0 && i + 1 < argc) {
            opt.seed_base = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--max-moves") == 0 && i + 1 < argc) {
            opt.max_moves = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--burn-in") == 0 && i + 1 < argc) {
            opt.burn_in = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--hazard-bins") == 0 && i + 1 < argc) {
            opt.hazard_bins = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            int n = std::atoi(argv[i]);
            if (n > 0) {
                opt.num_games = n;
            } else {
                std::fprintf(stderr, "unrecognized argument: %s\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
    }
    if (opt.max_moves != 0 && opt.burn_in >= opt.max_moves) {
        std::fprintf(stderr,
                     "--burn-in %llu leaves nothing to measure under "
                     "--max-moves %llu\n",
                     (unsigned long long)opt.burn_in,
                     (unsigned long long)opt.max_moves);
        return 1;
    }

    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;
    const unsigned requested_threads =
        opt.num_threads == 0 ? hw_threads : opt.num_threads;
    unsigned num_threads =
        std::min<unsigned>(requested_threads, (unsigned)opt.num_games);

    const auto weights = EvalWeights::getDefault();

    // Pre-generate per-game seeds so each game's seed is stable.
    std::vector<uint64_t> seeds(opt.num_games);
    if (opt.seed_base == 0) {
        std::random_device rd;
        for (int i = 0; i < opt.num_games; ++i) {
            seeds[i] = ((uint64_t)rd() << 32) | rd();
        }
    } else {
        for (int i = 0; i < opt.num_games; ++i) seeds[i] = opt.seed_base + i;
    }

    std::atomic<int> next_game_id{0};
    std::vector<GameResult> results(opt.num_games);
    std::vector<HazardTable> per_thread(num_threads);
    for (auto& t : per_thread) t.bin_width = opt.hazard_bins;
    std::mutex log_mutex;

    std::fprintf(stderr, "Running %d games across %u threads (%s seeds",
                 opt.num_games, num_threads,
                 opt.seed_base == 0 ? "random" : "deterministic");
    if (opt.max_moves != 0) {
        std::fprintf(stderr, ", cutoff %llu moves",
                     (unsigned long long)opt.max_moves);
    }
    std::fprintf(stderr, ")...\n");

    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                int id = next_game_id.fetch_add(1, std::memory_order_relaxed);
                if (id >= opt.num_games) return;

                const auto g_start = std::chrono::steady_clock::now();
                GameResult r = playOneGame(seeds[id], weights, opt.max_moves);
                const auto g_end = std::chrono::steady_clock::now();
                const double g_secs =
                    std::chrono::duration<double>(g_end - g_start).count();

                results[id] = r;
                per_thread[t].add(r);
                {
                    std::lock_guard<std::mutex> lk(log_mutex);
                    std::fprintf(stderr,
                                 "[t%u] game %d seed=%llu: %llu moves%s (%.1fs)\n",
                                 t, id,
                                 (unsigned long long)r.seed,
                                 (unsigned long long)r.moves,
                                 r.ended ? "" : " (cut off)", g_secs);
                    std::fflush(stderr);
                }
            }
        });
    }

    for (auto& w : workers) w.join();

    const auto end = std::chrono::steady_clock::now();
    const double total_secs =
        std::chrono::duration<double>(end - start).count();

    // Aggregate stats. Deaths and exposure are kept separately from the raw
    // lengths: once games are cut off, the lengths on their own understate how
    // long the engine lasts, while deaths over exposure does not.
    std::vector<GameResult> by_length(results);
    std::sort(by_length.begin(), by_length.end(),
              [](const GameResult& a, const GameResult& b) {
                  return a.moves < b.moves;
              });

    uint64_t deaths = 0;
    uint64_t exposure = 0;
    uint64_t total_moves = 0;
    for (const auto& r : results) {
        total_moves += r.moves;
        if (r.moves > opt.burn_in) exposure += r.moves - opt.burn_in;
        if (r.ended && r.moves > opt.burn_in) ++deaths;
    }

    // Raw per-game results on stdout so downstream tools can analyze.
    std::printf("# moves ended seed\n");
    for (const auto& r : by_length) {
        std::printf("%llu %d %llu\n", (unsigned long long)r.moves,
                    r.ended ? 1 : 0, (unsigned long long)r.seed);
    }

    const int cut_off = (int)std::count_if(
        results.begin(), results.end(),
        [](const GameResult& r) { return !r.ended; });

    std::fprintf(stderr, "\ngames=%d  ended=%d  cut off=%d\n",
                 opt.num_games, opt.num_games - cut_off, cut_off);

    if (cut_off == 0) {
        // Nothing was censored, so the plain summary means what it says.
        std::vector<uint64_t> moves;
        moves.reserve(opt.num_games);
        double sum = 0.0;
        double sum_sq = 0.0;
        for (const auto& r : by_length) {  // already in ascending order
            moves.push_back(r.moves);
            sum += (double)r.moves;
            sum_sq += (double)r.moves * (double)r.moves;
        }
        const double mean = sum / opt.num_games;
        const double var = opt.num_games > 1
            ? (sum_sq - opt.num_games * mean * mean) / (opt.num_games - 1)
            : 0.0;
        const double stddev = std::sqrt(std::max(0.0, var));
        const double sem = opt.num_games > 1
            ? stddev / std::sqrt((double)opt.num_games) : 0.0;
        std::fprintf(stderr,
                     "mean=%.1f  stddev=%.1f  sem=%.1f  cv=%.2f\n"
                     "p25=%.0f  p50=%.0f  p75=%.0f  p90=%.0f  p95=%.0f\n"
                     "min=%llu  max=%llu\n",
                     mean, stddev, sem, mean > 0.0 ? stddev / mean : 0.0,
                     percentile(moves, 0.25), percentile(moves, 0.50),
                     percentile(moves, 0.75), percentile(moves, 0.90),
                     percentile(moves, 0.95),
                     (unsigned long long)moves.front(),
                     (unsigned long long)moves.back());
    } else {
        std::fprintf(stderr,
                     "(%d games were cut off, so the mean and the percentiles "
                     "of the lengths above\n understate the engine and are "
                     "left out. Compare the hazard instead.)\n", cut_off);
    }

    // The hazard is deaths per move survived. It is the quantity a cutoff
    // leaves alone: a game that was stopped still contributes every move it
    // lived through to the denominator, and contributes no death, which is
    // exactly what was observed about it.
    if (opt.burn_in != 0) {
        std::fprintf(stderr, "\nhazard fit over moves past %llu:\n",
                     (unsigned long long)opt.burn_in);
    } else {
        std::fprintf(stderr, "\nhazard fit:\n");
    }
    if (deaths == 0 || exposure == 0) {
        std::fprintf(stderr,
                     "  deaths=%llu  exposure=%llu moves\n"
                     "  no deaths, so there is no rate to report -- raise "
                     "--max-moves or the game count\n",
                     (unsigned long long)deaths,
                     (unsigned long long)exposure);
    } else {
        const double hazard = (double)deaths / (double)exposure;
        // Asymptotic interval on the log rate: sd(log h) = 1/sqrt(deaths).
        const double half = 1.959963985 / std::sqrt((double)deaths);
        const double lo = hazard * std::exp(-half);
        const double hi = hazard * std::exp(half);
        std::fprintf(stderr,
                     "  deaths=%llu  exposure=%llu moves\n"
                     "  hazard=%.4e per move  (95%% CI %.4e .. %.4e)\n"
                     "  implied mean length%s=%.0f moves  (95%% CI %.0f .. %.0f)\n"
                     "  relative error on the hazard = 1/sqrt(deaths) = %.1f%%\n",
                     (unsigned long long)deaths,
                     (unsigned long long)exposure,
                     hazard, lo, hi,
                     opt.burn_in != 0 ? " past burn-in" : "",
                     1.0 / hazard, 1.0 / hi, 1.0 / lo,
                     100.0 / std::sqrt((double)deaths));
        if (deaths < 20) {
            std::fprintf(stderr,
                         "  (under 20 deaths -- the interval above is a rough "
                         "large-sample one)\n");
        }
    }

    if (opt.hazard_bins != 0) {
        HazardTable total;
        total.bin_width = opt.hazard_bins;
        for (const auto& t : per_thread) total.merge(t);
        std::fprintf(stderr, "\nhazard by depth into the game:\n");
        std::fprintf(stderr, "%13s %14s %8s %12s\n",
                     "moves", "exposure", "deaths", "hazard");
        for (size_t j = 0; j < total.exposure.size(); ++j) {
            if (total.exposure[j] == 0) continue;
            const double h = (double)total.deaths[j] / (double)total.exposure[j];
            std::fprintf(stderr, "%6llu-%-6llu %14llu %8llu %12.4e\n",
                         (unsigned long long)(j * opt.hazard_bins),
                         (unsigned long long)((j + 1) * opt.hazard_bins - 1),
                         (unsigned long long)total.exposure[j],
                         (unsigned long long)total.deaths[j], h);
        }
    }

    std::fprintf(stderr, "\nwall=%.1fs  throughput=%.0f move-sets/s\n",
                 total_secs, total_moves / total_secs);
    return 0;
}
