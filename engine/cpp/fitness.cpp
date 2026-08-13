#include "solver.h"
#include <algorithm>
#include <array>
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
    uint64_t chain_moves = 0;  // 0 => one game; otherwise fixed measured exposure
    uint64_t burn_in = 0;      // leading moves left out of the hazard fit
    uint64_t hazard_bins = 0;  // bin width of the hazard table, 0 => no table
    uint64_t probes = 0;       // independent triples sampled at each board
    uint64_t adaptive_low = 0;
    uint64_t adaptive_high = 0;
    int adaptive_occupancy = -1;
    uint64_t adaptive_middle = 0;
    int adaptive_occupancy_high = -1;
    bool probe_occupancy_table = false;
    bool custom_weights = false;
    int crowded_scarcity_weight = 0;
};

struct GameResult {
    uint64_t seed;
    uint64_t moves;  // all moves attempted, including burn-in after restarts
    bool ended;      // legacy mode only: whether the game reached a death
    uint64_t probe_boards = 0;
    uint64_t probe_failures = 0;
    long double probe_sum_p2 = 0.0;
    double probe_seconds = 0.0;
    double total_seconds = 0.0;
    uint64_t measured_exposure = 0;
    uint64_t measured_deaths = 0;
    uint64_t probe_draws = 0;
    long double probe_sum_p = 0.0;
    uint64_t adaptive_high_boards = 0;
    uint64_t adaptive_middle_boards = 0;
    std::array<uint64_t, 82> occupancy_boards{};
    std::array<uint64_t, 82> occupancy_draws{};
    std::array<uint64_t, 82> occupancy_failures{};
    std::array<long double, 82> occupancy_sum_p{};
    std::array<double, 82> occupancy_seconds{};
};

uint64_t splitMix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

Piece randomPiece(std::mt19937_64& rng,
                  std::uniform_int_distribution<int>& piece_dist) {
    return Piece::byIndex(piece_dist(rng));
}

bool hasIndividuallyUnplaceablePiece(GameState game) {
    for (const Piece piece : Piece::getAll()) {
        const auto states = game.nextStates(piece);
        if (!(states.begin() != states.end())) return true;
    }
    return false;
}

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

// In the legacy mode, plays until the game ends or max_moves is reached. In
// fixed-exposure mode, restarts after a death until chain_moves measured moves
// have accumulated; burn-in is applied again to every new game in the chain.
GameResult playOneGame(uint64_t seed, const Options& opt) {
    // Each game has its own RNG; callers pass a non-deterministic seed by
    // default. Seeds are reported so any individual game can be reproduced.
    std::mt19937_64 rng(seed);
    // Probing must not consume the trajectory's piece stream. Deriving a
    // second seed is reproducible and leaves an unprobed run bit-for-bit
    // comparable with a probed run using the same seed.
    std::mt19937_64 probe_rng(splitMix64(seed ^ 0x52414f424c41434bULL));
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);
    std::uniform_int_distribution<int> probe_piece_dist(
        0, Piece::NUM_PIECES - 1);
    EvalWeights weights = EvalWeights::getDefault();
    if (opt.custom_weights) {
        weights.weights[12] = opt.crowded_scarcity_weight;
    }

    GameState game(BitBoard::empty());
    uint64_t moves = 0;
    uint64_t depth = 0;
    GameResult result;
    result.seed = seed;
    const auto game_start = std::chrono::steady_clock::now();
    while (opt.chain_moves != 0 || !game.isOver()) {
        if (opt.chain_moves != 0 &&
            result.measured_exposure >= opt.chain_moves) {
            break;
        }
        if (opt.chain_moves == 0 && opt.max_moves != 0 &&
            moves >= opt.max_moves) {
            break;
        }

        const bool measured = depth >= opt.burn_in;
        if ((opt.probes != 0 || opt.adaptive_low != 0) && measured) {
            const bool needs_occupancy = opt.probe_occupancy_table ||
                (opt.adaptive_low != 0 && opt.adaptive_occupancy >= 0);
            const int occupied = needs_occupancy
                ? game.getBitBoard().count() : 0;
            const auto probe_start = std::chrono::steady_clock::now();
            uint64_t probes = opt.probes;
            if (opt.adaptive_low != 0) {
                bool high = false;
                bool middle = false;
                if (opt.adaptive_occupancy >= 0) {
                    high = occupied >= (opt.adaptive_occupancy_high >= 0
                        ? opt.adaptive_occupancy_high
                        : opt.adaptive_occupancy);
                    if (high) {
                        probes = opt.adaptive_high;
                    } else if (opt.adaptive_occupancy_high >= 0 &&
                               occupied >= opt.adaptive_occupancy) {
                        probes = opt.adaptive_middle;
                        middle = true;
                    } else {
                        probes = opt.adaptive_low;
                    }
                } else {
                    high = hasIndividuallyUnplaceablePiece(game);
                    probes = high ? opt.adaptive_high : opt.adaptive_low;
                }
                if (high) ++result.adaptive_high_boards;
                else if (middle) ++result.adaptive_middle_boards;
            }
            uint64_t failures = 0;
            for (uint64_t probe = 0; probe < probes; ++probe) {
                const Piece probe_p0 = randomPiece(probe_rng, probe_piece_dist);
                const Piece probe_p1 = randomPiece(probe_rng, probe_piece_dist);
                const Piece probe_p2 = randomPiece(probe_rng, probe_piece_dist);
                const PieceSet sample(probe_p0, probe_p1, probe_p2);
                if (!AI::tripleFits(game, sample)) ++failures;
            }
            const auto probe_end = std::chrono::steady_clock::now();
            const long double p = (long double)failures / probes;
            ++result.probe_boards;
            result.probe_draws += probes;
            result.probe_failures += failures;
            result.probe_sum_p += p;
            result.probe_sum_p2 += p * p;
            result.probe_seconds +=
                std::chrono::duration<double>(probe_end - probe_start).count();
            if (opt.probe_occupancy_table) {
                ++result.occupancy_boards[occupied];
                result.occupancy_draws[occupied] += probes;
                result.occupancy_failures[occupied] += failures;
                result.occupancy_sum_p[occupied] += p;
                result.occupancy_seconds[occupied] +=
                    std::chrono::duration<double>(probe_end - probe_start).count();
            }
        }

        Piece p0 = randomPiece(rng, piece_dist);
        Piece p1 = randomPiece(rng, piece_dist);
        Piece p2 = randomPiece(rng, piece_dist);
        const PieceSet dealt(p0, p1, p2);
        game = opt.custom_weights
            ? AI::makeMoveSimple(weights, game, dealt).state
            : AI::makeMoveSimpleDefault(game, dealt).state;
        ++moves;
        ++depth;
        if (measured) ++result.measured_exposure;

        if (opt.chain_moves != 0 && game.isOver()) {
            if (measured) ++result.measured_deaths;
            game = GameState(BitBoard::empty());
            depth = 0;
        }
    }
    result.moves = moves;
    result.ended = opt.chain_moves == 0 && game.isOver();
    if (opt.chain_moves == 0) {
        result.measured_exposure = moves > opt.burn_in
            ? moves - opt.burn_in : 0;
        result.measured_deaths = result.ended && moves > opt.burn_in ? 1 : 0;
    }
    result.total_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - game_start).count();
    return result;
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
        "  --chain-moves N   run each independent chain for exactly N measured\n"
        "                    moves, restarting after deaths and applying the\n"
        "                    burn-in again after every restart\n"
        "  --burn-in B       leave each game's first B moves out of the hazard\n"
        "                    fit, for when the early board is not yet typical\n"
        "  --hazard-bins W   print deaths and exposure per W moves of depth,\n"
        "                    which is what says whether the hazard is flat\n"
        "  --probe M         sample M independent triples at every measured\n"
        "                    board, using a separate reproducible RNG stream\n"
        "  --probe-adaptive LOW HIGH\n"
        "                    use HIGH probes when any standard piece cannot fit\n"
        "                    alone, and LOW otherwise; both must be positive\n"
        "  --probe-occupancy LOW HIGH SQUARES\n"
        "                    use HIGH probes at SQUARES or more occupied cells,\n"
        "                    and LOW otherwise; allocation uses only the board\n"
        "  --probe-occupancy-bands LOW MID HIGH MID_SQUARES HIGH_SQUARES\n"
        "                    use three occupancy-only probe allocations\n"
        "  --probe-occupancy-table\n"
        "                    report probe risk and cost by occupied squares\n"
        "  --crowded-scarcity-weight W\n"
        "                    use the generic evaluator with weights[12]=W;\n"
        "                    pass 200 and 0 for a fair scarcity-term A/B test\n"
        "\n"
        "stdout gets one line per chain. Its first three fields retain the old\n"
        "moves/ended/seed format; probe runs append their sufficient stats.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    bool probe_policy_seen = false;
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
        } else if (std::strcmp(argv[i], "--chain-moves") == 0 && i + 1 < argc) {
            opt.chain_moves = std::strtoull(argv[++i], nullptr, 10);
            if (opt.chain_moves == 0) {
                std::fprintf(stderr, "--chain-moves must be positive\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--burn-in") == 0 && i + 1 < argc) {
            opt.burn_in = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--hazard-bins") == 0 && i + 1 < argc) {
            opt.hazard_bins = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--probe") == 0 && i + 1 < argc) {
            if (probe_policy_seen) {
                std::fprintf(stderr, "probe policies are mutually exclusive\n");
                return 1;
            }
            probe_policy_seen = true;
            opt.probes = std::strtoull(argv[++i], nullptr, 10);
            if (opt.probes == 0) {
                std::fprintf(stderr, "--probe must be positive\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--probe-adaptive") == 0 &&
                   i + 2 < argc) {
            if (probe_policy_seen) {
                std::fprintf(stderr, "probe policies are mutually exclusive\n");
                return 1;
            }
            probe_policy_seen = true;
            opt.adaptive_low = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_high = std::strtoull(argv[++i], nullptr, 10);
            if (opt.adaptive_low == 0 || opt.adaptive_high == 0) {
                std::fprintf(stderr,
                    "--probe-adaptive counts must both be positive\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--probe-occupancy") == 0 &&
                   i + 3 < argc) {
            if (probe_policy_seen) {
                std::fprintf(stderr, "probe policies are mutually exclusive\n");
                return 1;
            }
            probe_policy_seen = true;
            opt.adaptive_low = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_high = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_occupancy = (int)std::strtol(argv[++i], nullptr, 10);
            if (opt.adaptive_low == 0 || opt.adaptive_high == 0 ||
                opt.adaptive_occupancy < 0 || opt.adaptive_occupancy > 81) {
                std::fprintf(stderr,
                    "--probe-occupancy needs positive counts and a threshold "
                    "from 0 to 81\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--probe-occupancy-bands") == 0 &&
                   i + 5 < argc) {
            if (probe_policy_seen) {
                std::fprintf(stderr, "probe policies are mutually exclusive\n");
                return 1;
            }
            probe_policy_seen = true;
            opt.adaptive_low = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_middle = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_high = std::strtoull(argv[++i], nullptr, 10);
            opt.adaptive_occupancy = (int)std::strtol(argv[++i], nullptr, 10);
            opt.adaptive_occupancy_high =
                (int)std::strtol(argv[++i], nullptr, 10);
            if (opt.adaptive_low == 0 || opt.adaptive_middle == 0 ||
                opt.adaptive_high == 0 || opt.adaptive_occupancy < 0 ||
                opt.adaptive_occupancy_high > 81 ||
                opt.adaptive_occupancy_high <= opt.adaptive_occupancy) {
                std::fprintf(stderr,
                    "--probe-occupancy-bands needs positive counts and "
                    "increasing thresholds from 0 to 81\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--probe-occupancy-table") == 0) {
            opt.probe_occupancy_table = true;
        } else if (std::strcmp(argv[i], "--crowded-scarcity-weight") == 0 &&
                   i + 1 < argc) {
            const long weight = std::strtol(argv[++i], nullptr, 10);
            if (weight < 0 || weight > EvalWeights::MAX_WEIGHT) {
                std::fprintf(stderr,
                    "--crowded-scarcity-weight must be between 0 and %d\n",
                    EvalWeights::MAX_WEIGHT);
                return 1;
            }
            opt.custom_weights = true;
            opt.crowded_scarcity_weight = (int)weight;
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
    if (opt.max_moves != 0 && opt.chain_moves != 0) {
        std::fprintf(stderr,
                     "--max-moves and --chain-moves are mutually exclusive\n");
        return 1;
    }
    if (opt.probes != 0 && opt.adaptive_low != 0) {
        std::fprintf(stderr,
                     "--probe and --probe-adaptive are mutually exclusive\n");
        return 1;
    }
    if (opt.probe_occupancy_table && opt.probes == 0 &&
        opt.adaptive_low == 0) {
        std::fprintf(stderr,
                     "--probe-occupancy-table requires a probe option\n");
        return 1;
    }
    if (opt.chain_moves != 0 && opt.hazard_bins != 0) {
        std::fprintf(stderr,
                     "--hazard-bins describes one game's depth and cannot be "
                     "combined with restarting --chain-moves\n");
        return 1;
    }

    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;
    const unsigned requested_threads =
        opt.num_threads == 0 ? hw_threads : opt.num_threads;
    unsigned num_threads =
        std::min<unsigned>(requested_threads, (unsigned)opt.num_games);

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

    std::fprintf(stderr, "Running %d %s across %u threads (%s seeds",
                 opt.num_games, opt.chain_moves == 0 ? "games" : "chains",
                 num_threads,
                 opt.seed_base == 0 ? "random" : "deterministic");
    if (opt.max_moves != 0) {
        std::fprintf(stderr, ", cutoff %llu moves",
                     (unsigned long long)opt.max_moves);
    }
    if (opt.chain_moves != 0) {
        std::fprintf(stderr, ", fixed exposure %llu moves/chain",
                     (unsigned long long)opt.chain_moves);
    }
    if (opt.probes != 0) {
        std::fprintf(stderr, ", %llu probes/board",
                     (unsigned long long)opt.probes);
    }
    if (opt.adaptive_low != 0) {
        if (opt.adaptive_middle != 0) {
            std::fprintf(stderr, ", adaptive probes %llu/%llu/%llu by occupancy",
                (unsigned long long)opt.adaptive_low,
                (unsigned long long)opt.adaptive_middle,
                (unsigned long long)opt.adaptive_high);
        } else {
            std::fprintf(stderr, ", adaptive probes %llu/%llu%s",
                (unsigned long long)opt.adaptive_low,
                (unsigned long long)opt.adaptive_high,
                opt.adaptive_occupancy >= 0 ? " by occupancy" : "");
        }
    }
    if (opt.custom_weights) {
        std::fprintf(stderr, ", generic evaluator weights[12]=%d",
                     opt.crowded_scarcity_weight);
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

                GameResult r = playOneGame(seeds[id], opt);

                results[id] = r;
                per_thread[t].add(r);
                {
                    std::lock_guard<std::mutex> lk(log_mutex);
                    if (opt.chain_moves != 0) {
                        std::fprintf(stderr,
                            "[t%u] chain %d seed=%llu: %llu measured moves, "
                            "%llu deaths (%.1fs)\n",
                            t, id, (unsigned long long)r.seed,
                            (unsigned long long)r.measured_exposure,
                            (unsigned long long)r.measured_deaths,
                            r.total_seconds);
                    } else {
                        std::fprintf(stderr,
                            "[t%u] game %d seed=%llu: %llu moves%s (%.1fs)\n",
                            t, id, (unsigned long long)r.seed,
                            (unsigned long long)r.moves,
                            r.ended ? "" : " (cut off)", r.total_seconds);
                    }
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
        exposure += r.measured_exposure;
        deaths += r.measured_deaths;
    }

    // Raw per-game or per-chain results on stdout so downstream tools can
    // analyze independent units.
    std::printf("# moves ended seed probe_boards probe_failures "
                "probe_sum_p2 probe_seconds total_seconds exposure deaths "
                "probe_draws probe_sum_p adaptive_high_boards "
                "adaptive_middle_boards\n");
    std::printf("# options burn_in=%llu chain_moves=%llu\n",
                (unsigned long long)opt.burn_in,
                (unsigned long long)opt.chain_moves);
    if (opt.probes != 0) {
        std::printf("# probe M=%llu burn_in=%llu\n",
                    (unsigned long long)opt.probes,
                    (unsigned long long)opt.burn_in);
    }
    if (opt.adaptive_low != 0) {
        std::printf("# probe adaptive low=%llu middle=%llu high=%llu "
                    "burn_in=%llu occupancy=%d occupancy_high=%d\n",
                    (unsigned long long)opt.adaptive_low,
                    (unsigned long long)opt.adaptive_middle,
                    (unsigned long long)opt.adaptive_high,
                    (unsigned long long)opt.burn_in,
                    opt.adaptive_occupancy, opt.adaptive_occupancy_high);
    }
    for (const auto& r : by_length) {
        std::printf("%llu %d %llu %llu %llu %.17Lg %.9f %.9f %llu %llu "
                    "%llu %.17Lg %llu %llu\n",
                    (unsigned long long)r.moves, r.ended ? 1 : 0,
                    (unsigned long long)r.seed,
                    (unsigned long long)r.probe_boards,
                    (unsigned long long)r.probe_failures,
                    r.probe_sum_p2, r.probe_seconds, r.total_seconds,
                    (unsigned long long)r.measured_exposure,
                    (unsigned long long)r.measured_deaths,
                    (unsigned long long)r.probe_draws, r.probe_sum_p,
                    (unsigned long long)r.adaptive_high_boards,
                    (unsigned long long)r.adaptive_middle_boards);
    }

    const int cut_off = (int)std::count_if(
        results.begin(), results.end(),
        [](const GameResult& r) { return !r.ended; });

    if (opt.chain_moves != 0) {
        std::fprintf(stderr,
                     "\nchains=%d  measured exposure=%llu  deaths=%llu\n",
                     opt.num_games, (unsigned long long)exposure,
                     (unsigned long long)deaths);
    } else {
        std::fprintf(stderr, "\ngames=%d  ended=%d  cut off=%d\n",
                     opt.num_games, opt.num_games - cut_off, cut_off);
    }

    if (opt.chain_moves != 0) {
        std::fprintf(stderr,
                     "(fixed-exposure chains restart after deaths; game-length "
                     "summaries do not apply.)\n");
    } else if (cut_off == 0) {
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
                     "  no deaths, so there is no rate to report -- raise %s\n",
                     (unsigned long long)deaths,
                     (unsigned long long)exposure,
                     opt.chain_moves == 0
                        ? "--max-moves or the game count"
                        : "--chain-moves or the chain count");
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

    if (opt.probes != 0 || opt.adaptive_low != 0) {
        uint64_t probe_boards = 0;
        uint64_t probe_failures = 0;
        long double probe_sum_p2 = 0.0;
        double probe_seconds = 0.0;
        double worker_seconds = 0.0;
        uint64_t probe_draws = 0;
        long double probe_sum_p = 0.0;
        uint64_t adaptive_high_boards = 0;
        uint64_t adaptive_middle_boards = 0;
        std::array<uint64_t, 82> occupancy_boards{};
        std::array<uint64_t, 82> occupancy_draws{};
        std::array<uint64_t, 82> occupancy_failures{};
        std::array<long double, 82> occupancy_sum_p{};
        std::array<double, 82> occupancy_seconds{};
        for (const auto& r : results) {
            probe_boards += r.probe_boards;
            probe_failures += r.probe_failures;
            probe_sum_p2 += r.probe_sum_p2;
            probe_seconds += r.probe_seconds;
            worker_seconds += r.total_seconds;
            probe_draws += r.probe_draws;
            probe_sum_p += r.probe_sum_p;
            adaptive_high_boards += r.adaptive_high_boards;
            adaptive_middle_boards += r.adaptive_middle_boards;
            if (opt.probe_occupancy_table) {
                for (size_t occupied = 0; occupied < occupancy_boards.size();
                     ++occupied) {
                    occupancy_boards[occupied] += r.occupancy_boards[occupied];
                    occupancy_draws[occupied] += r.occupancy_draws[occupied];
                    occupancy_failures[occupied] +=
                        r.occupancy_failures[occupied];
                    occupancy_sum_p[occupied] += r.occupancy_sum_p[occupied];
                    occupancy_seconds[occupied] +=
                        r.occupancy_seconds[occupied];
                }
            }
        }

        if (probe_boards == 0) {
            std::fprintf(stderr, "\nprobe estimate: no boards past burn-in\n");
        } else {
            const long double h_probe = probe_sum_p / probe_boards;
            const long double observed_mean_p2 =
                probe_sum_p2 / probe_boards;

            // Treat games/chains as the independent units. This cluster-robust
            // standard error is for the pooled ratio sum(p)/sum(boards), and
            // remains valid when a death makes chains different lengths.
            long double residual_sum_sq = 0.0;
            size_t chains = 0;
            for (const auto& r : results) {
                if (r.probe_boards == 0) continue;
                const long double chain_sum_p =
                    r.probe_sum_p;
                const long double residual =
                    chain_sum_p - h_probe * r.probe_boards;
                residual_sum_sq += residual * residual;
                ++chains;
            }
            long double se = 0.0;
            if (chains > 1) {
                se = std::sqrt((long double)chains / (chains - 1) *
                    residual_sum_sq) / probe_boards;
            }
            const long double lo = std::max(0.0L,
                h_probe - 1.959963985L * se);
            const long double hi = h_probe + 1.959963985L * se;

            std::fprintf(stderr,
                "\nprobe estimate over %llu boards:\n"
                "  failures=%llu / %llu sampled triples (%.3Lf/board)\n"
                "  h_probe=%.6Le per move  (chain-clustered 95%% CI %.6Le .. %.6Le)\n",
                (unsigned long long)probe_boards,
                (unsigned long long)probe_failures,
                (unsigned long long)probe_draws,
                (long double)probe_draws / probe_boards,
                h_probe, lo, hi);

            if (opt.adaptive_low != 0) {
                std::fprintf(stderr,
                    "  (variable M: h_probe averages each board's failure "
                    "fraction; failures/draws is diagnostic only)\n");
            }

            if (opt.probes > 1) {
                // If q=K/M, E[q^2|p] = p^2 + p(1-p)/M. Solving
                // for E[p^2] needs M-1 in the denominator. With M=1 the two
                // variance components cannot be identified separately.
                const long double true_mean_p2 = observed_mean_p2 -
                    (h_probe - observed_mean_p2) / (opt.probes - 1);
                const long double board_variance = std::max(0.0L,
                    true_mean_p2 - h_probe * h_probe);
                std::fprintf(stderr, "  Var_B[p]=%.6Le\n", board_variance);
            } else {
                std::fprintf(stderr, opt.adaptive_low != 0
                    ? "  Var_B[p]=unavailable in the variable-M summary\n"
                    : "  Var_B[p]=unavailable at M=1 (probe noise and board "
                      "variance are not separately identifiable)\n");
            }

            if (opt.adaptive_low != 0) {
                std::fprintf(stderr,
                    "  adaptive high-probe boards=%llu (%.4Lf%%)%s\n",
                    (unsigned long long)adaptive_high_boards,
                    100.0L * adaptive_high_boards / probe_boards,
                    opt.adaptive_occupancy >= 0 ? " by occupancy" : "");
                if (opt.adaptive_middle != 0) {
                    std::fprintf(stderr,
                        "  adaptive middle-probe boards=%llu (%.4Lf%%) "
                        "by occupancy\n",
                        (unsigned long long)adaptive_middle_boards,
                        100.0L * adaptive_middle_boards / probe_boards);
                }
            }

            const double share = worker_seconds > 0.0
                ? probe_seconds / worker_seconds : 0.0;
            std::fprintf(stderr,
                "  probing=%.3fs of %.3fs aggregate worker time (%.2f%%); "
                "implied cost ratio %.4fx\n",
                probe_seconds, worker_seconds, 100.0 * share,
                share < 1.0 ? 1.0 / (1.0 - share) : INFINITY);

            if (opt.probe_occupancy_table) {
                std::fprintf(stderr,
                    "\nprobe risk and cost by occupied squares:\n"
                    "%8s %12s %14s %10s %14s\n",
                    "occupied", "boards", "h_probe", "failures",
                    "ns/probe");
                for (size_t occupied = 0; occupied < occupancy_boards.size();
                     ++occupied) {
                    if (occupancy_boards[occupied] == 0) continue;
                    const long double h = occupancy_sum_p[occupied] /
                        occupancy_boards[occupied];
                    const double ns_per_probe = occupancy_draws[occupied] == 0
                        ? 0.0 : 1.0e9 * occupancy_seconds[occupied] /
                            occupancy_draws[occupied];
                    std::fprintf(stderr,
                        "%8zu %12llu %14.6Le %10llu %14.2f\n",
                        occupied,
                        (unsigned long long)occupancy_boards[occupied], h,
                        (unsigned long long)occupancy_failures[occupied],
                        ns_per_probe);
                }
            }
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
