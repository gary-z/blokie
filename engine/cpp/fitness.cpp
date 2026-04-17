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

// Feature extraction for weight-tuning via distillation.
// simpleEval is linear in the 12 tunable weights plus one hardcoded coefficient
// (OccupiedSideSquare = 2000). So eval(B) = f(B) . [w0..w11, 2000].
// We recover the 13-component feature vector by probing simpleEval with basis
// weight vectors: zero gives 2000 * f_sideSquare, then each unit probe gives
// f_k + 2000 * f_sideSquare. No solver changes required.
constexpr int NUM_FEATURES = EvalWeights::NUM_WEIGHTS + 1;  // 13
constexpr int SIDE_SQUARE_COEF = 2000;

void extractFeatures(const GameState& g, int64_t out[NUM_FEATURES]) {
    EvalWeights zero{};  // all weights 0 by default-init
    const uint64_t base = g.simpleEval(zero);
    out[NUM_FEATURES - 1] = (int64_t)(base / SIDE_SQUARE_COEF);
    for (int k = 0; k < EvalWeights::NUM_WEIGHTS; ++k) {
        EvalWeights probe{};
        probe.weights[k] = 1;
        const uint64_t v = g.simpleEval(probe);
        out[k] = (int64_t)v - (int64_t)base;
    }
}

// Teacher signal: E over random 3-piece draws of eval(result_board), where
// result_board is what AI::makeMoveSimple picks with the current weights.
// This is the value produced by one extra round of lookahead. The student
// (distilled weights) is asked to predict this deeper-search value directly.
double computeTeacherV3(const GameState& g, const EvalWeights& weights,
                       int samples, std::mt19937_64& rng) {
    if (g.isOver()) return (double)g.simpleEval(weights);
    std::uniform_int_distribution<int> pd(0, Piece::NUM_PIECES - 1);
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        Piece p0 = Piece::byIndex(pd(rng));
        Piece p1 = Piece::byIndex(pd(rng));
        Piece p2 = Piece::byIndex(pd(rng));
        GameState r = AI::makeMoveSimple(weights, g, PieceSet(p0, p1, p2));
        sum += (double)r.simpleEval(weights);
    }
    return sum / samples;
}

// Survival-curve thresholds (move offsets from a sampled board).
// Fine-grained near the hypothesized "mixing time" (~50-100 moves), coarser
// after, ending at a depth big enough to see the steady-state hazard signal
// (~2e-5/move -> need >=1000 moves for ~2% death probability).
constexpr int SURVIVAL_THRESHOLDS[] = {10, 25, 50, 100, 200, 500, 1000, 1500};
constexpr int NUM_SURVIVAL_THRESHOLDS =
    sizeof(SURVIVAL_THRESHOLDS) / sizeof(SURVIVAL_THRESHOLDS[0]);

// Play a single rollout starting from `start` with the given weights.
// Returns the number of moves before death (>= max_depth if the rollout
// survived the whole budget).
int playRolloutFrom(BitBoard start, uint64_t seed, int max_depth,
                    const EvalWeights& weights) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pd(0, Piece::NUM_PIECES - 1);
    GameState game(start);
    int m = 0;
    while (m < max_depth && !game.isOver()) {
        Piece p0 = Piece::byIndex(pd(rng));
        Piece p1 = Piece::byIndex(pd(rng));
        Piece p2 = Piece::byIndex(pd(rng));
        game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2));
        ++m;
    }
    // On survival, return max_depth+1 so comparisons "alive at thr" correctly
    // classify thresholds in [1..max_depth] but do NOT pretend we know
    // survival past max_depth. (Thresholds > max_depth are always considered
    // uncertain; analysis should ignore them.)
    if (!game.isOver()) return max_depth + 1;
    return m;
}

// Run n_rollouts parallel rollouts from `start` (up to max_depth each) using
// hw_threads workers. Fills `alive_at[k]` with #rollouts still alive at
// move offset SURVIVAL_THRESHOLDS[k].
void surveyBoard(BitBoard start, int n_rollouts, int max_depth,
                 const EvalWeights& weights, uint64_t seed_base,
                 unsigned hw_threads, int alive_at[NUM_SURVIVAL_THRESHOLDS]) {
    std::vector<int> death(n_rollouts, 0);
    std::atomic<int> next_job{0};
    std::vector<std::thread> workers;
    const unsigned T = std::min<unsigned>(hw_threads, (unsigned)n_rollouts);
    workers.reserve(T);
    for (unsigned t = 0; t < T; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                int i = next_job.fetch_add(1, std::memory_order_relaxed);
                if (i >= n_rollouts) return;
                death[i] = playRolloutFrom(start, seed_base + (uint64_t)i,
                                           max_depth, weights);
            }
        });
    }
    for (auto& w : workers) w.join();

    for (int k = 0; k < NUM_SURVIVAL_THRESHOLDS; ++k) {
        const int thr = SURVIVAL_THRESHOLDS[k];
        int alive = 0;
        for (int d : death) if (d >= thr) ++alive;
        alive_at[k] = alive;
    }
}

int runRolloutSurvey(const char* out_path, int num_games, int stride,
                     int rollouts_per_board, int rollout_depth,
                     uint64_t cap, const EvalWeights& weights,
                     uint64_t seed_base, unsigned hw_threads) {
    std::FILE* out = std::fopen(out_path, "w");
    if (!out) {
        std::fprintf(stderr, "failed to open %s for write\n", out_path);
        return 1;
    }
    std::fprintf(out, "game_idx,move_idx,f0,f1,f2,f3,f4,f5,f6,f7,f8,f9,"
                     "f10,f11,f_sideSquare,current_eval,n_rollouts");
    for (int k = 0; k < NUM_SURVIVAL_THRESHOLDS; ++k) {
        std::fprintf(out, ",s%d", SURVIVAL_THRESHOLDS[k]);
    }
    std::fprintf(out, "\n");

    std::mt19937_64 outer_seeder;
    if (seed_base == 0) {
        std::random_device rd;
        outer_seeder.seed(((uint64_t)rd() << 32) | rd());
    } else {
        outer_seeder.seed(seed_base);
    }

    const auto start = std::chrono::steady_clock::now();
    int boards_done = 0;

    for (int gi = 0; gi < num_games; ++gi) {
        const uint64_t game_seed = outer_seeder();
        std::mt19937_64 game_rng(game_seed);
        std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);

        GameState game(BitBoard::empty());
        uint64_t move = 0;

        while (!game.isOver() && (cap == 0 || move < cap)) {
            Piece p0 = Piece::byIndex(piece_dist(game_rng));
            Piece p1 = Piece::byIndex(piece_dist(game_rng));
            Piece p2 = Piece::byIndex(piece_dist(game_rng));
            game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2));
            ++move;
            if (move % (uint64_t)stride != 0) continue;
            if (game.isOver()) break;

            // Survey this board.
            int64_t feats[NUM_FEATURES];
            extractFeatures(game, feats);
            const uint64_t cur = game.simpleEval(weights);

            int alive_at[NUM_SURVIVAL_THRESHOLDS];
            const auto b_start = std::chrono::steady_clock::now();
            const uint64_t rseed = (game_seed * 1315423911ULL) ^ (move * 2654435761ULL);
            surveyBoard(game.getBitBoard(), rollouts_per_board, rollout_depth,
                        weights, rseed, hw_threads, alive_at);
            const double b_secs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - b_start).count();

            std::fprintf(out, "%d,%llu", gi, (unsigned long long)move);
            for (int k = 0; k < NUM_FEATURES; ++k)
                std::fprintf(out, ",%lld", (long long)feats[k]);
            std::fprintf(out, ",%llu,%d", (unsigned long long)cur, rollouts_per_board);
            for (int k = 0; k < NUM_SURVIVAL_THRESHOLDS; ++k)
                std::fprintf(out, ",%d", alive_at[k]);
            std::fprintf(out, "\n");
            std::fflush(out);
            ++boards_done;
            std::fprintf(stderr,
                         "[rollout] g%d m%llu  s50=%d/%d s200=%d/%d s1000=%d/%d  (%.1fs)\n",
                         gi, (unsigned long long)move,
                         alive_at[2], rollouts_per_board,   // s50
                         alive_at[4], rollouts_per_board,   // s200
                         alive_at[6], rollouts_per_board,   // s1000
                         b_secs);
        }
    }
    std::fclose(out);
    const double wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "[rollout] surveyed %d boards, wall=%.1fs -> %s\n",
                 boards_done, wall, out_path);
    return 0;
}

int runDistill(const char* out_path, int num_games, int stride, int samples,
               uint64_t cap, const EvalWeights& weights, uint64_t seed_base) {
    std::FILE* out = std::fopen(out_path, "w");
    if (!out) {
        std::fprintf(stderr, "failed to open %s for write\n", out_path);
        return 1;
    }
    // Header: 13 features, teacher value, current eval, game_idx, move_idx.
    std::fprintf(out, "f0,f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f_sideSquare,"
                     "teacher,current_eval,game_idx,move_idx\n");

    // Outer seed stream (non-deterministic unless --seed-base given).
    std::mt19937_64 outer_seeder;
    if (seed_base == 0) {
        std::random_device rd;
        outer_seeder.seed(((uint64_t)rd() << 32) | rd());
    } else {
        outer_seeder.seed(seed_base);
    }

    const auto start = std::chrono::steady_clock::now();
    int64_t total_rows = 0;

    for (int gi = 0; gi < num_games; ++gi) {
        const uint64_t game_seed = outer_seeder();
        std::mt19937_64 game_rng(game_seed);
        std::mt19937_64 teacher_rng(game_seed ^ 0x9E3779B97F4A7C15ULL);
        std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);

        GameState game(BitBoard::empty());
        uint64_t move = 0;
        int64_t rows_this_game = 0;
        const auto g_start = std::chrono::steady_clock::now();

        while (!game.isOver() && (cap == 0 || move < cap)) {
            Piece p0 = Piece::byIndex(piece_dist(game_rng));
            Piece p1 = Piece::byIndex(piece_dist(game_rng));
            Piece p2 = Piece::byIndex(piece_dist(game_rng));
            game = AI::makeMoveSimple(weights, game, PieceSet(p0, p1, p2));
            ++move;

            if (move % (uint64_t)stride != 0) continue;
            if (game.isOver()) break;

            int64_t feats[NUM_FEATURES];
            extractFeatures(game, feats);
            const uint64_t cur = game.simpleEval(weights);
            const double teacher = computeTeacherV3(game, weights, samples, teacher_rng);

            for (int k = 0; k < NUM_FEATURES; ++k) {
                std::fprintf(out, "%lld,", (long long)feats[k]);
            }
            std::fprintf(out, "%.3f,%llu,%d,%llu\n",
                         teacher, (unsigned long long)cur, gi,
                         (unsigned long long)move);
            ++rows_this_game;
        }

        const double g_secs = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - g_start).count();
        std::fflush(out);
        total_rows += rows_this_game;
        std::fprintf(stderr,
                     "[distill] game %d  moves=%llu  rows=%lld  (%.1fs)\n",
                     gi, (unsigned long long)move, (long long)rows_this_game, g_secs);
    }

    std::fclose(out);
    const double wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "[distill] total rows=%lld wall=%.1fs -> %s\n",
                 (long long)total_rows, wall, out_path);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int num_games = 8;
    uint64_t seed_base = 0;   // 0 => non-deterministic
    uint64_t cap = 0;         // 0 => no cap
    EvalWeights weights = EvalWeights::getDefault();
    bool weights_overridden = false;

    const char* distill_out = nullptr;
    int distill_games = 5;
    int distill_stride = 25;     // sample 1 of every N boards visited
    int distill_samples = 30;    // piece-triples per teacher estimate

    const char* rollout_out = nullptr;
    int rollout_games = 5;
    int rollout_stride = 1000;
    int rollout_n = 200;
    int rollout_depth = 1500;

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
        } else if (!std::strcmp(a, "--distill-out") && i + 1 < argc) {
            distill_out = argv[++i];
        } else if (!std::strcmp(a, "--distill-games") && i + 1 < argc) {
            distill_games = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--distill-stride") && i + 1 < argc) {
            distill_stride = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--distill-samples") && i + 1 < argc) {
            distill_samples = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--rollout-out") && i + 1 < argc) {
            rollout_out = argv[++i];
        } else if (!std::strcmp(a, "--rollout-games") && i + 1 < argc) {
            rollout_games = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--rollout-stride") && i + 1 < argc) {
            rollout_stride = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--rollout-n") && i + 1 < argc) {
            rollout_n = std::atoi(argv[++i]);
        } else if (!std::strcmp(a, "--rollout-depth") && i + 1 < argc) {
            rollout_depth = std::atoi(argv[++i]);
        } else {
            int n = std::atoi(a);
            if (n > 0) num_games = n;
        }
    }

    if (distill_out) {
        std::fprintf(stderr,
                     "[distill] games=%d stride=%d samples=%d cap=%llu "
                     "weights=%s -> %s\n",
                     distill_games, distill_stride, distill_samples,
                     (unsigned long long)cap,
                     weights_overridden ? "custom" : "default",
                     distill_out);
        return runDistill(distill_out, distill_games, distill_stride,
                          distill_samples, cap, weights, seed_base);
    }

    if (rollout_out) {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        std::fprintf(stderr,
                     "[rollout] games=%d stride=%d n=%d depth=%d cap=%llu "
                     "weights=%s threads=%u -> %s\n",
                     rollout_games, rollout_stride, rollout_n, rollout_depth,
                     (unsigned long long)cap,
                     weights_overridden ? "custom" : "default",
                     hw, rollout_out);
        return runRolloutSurvey(rollout_out, rollout_games, rollout_stride,
                                rollout_n, rollout_depth, cap, weights,
                                seed_base, hw);
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
