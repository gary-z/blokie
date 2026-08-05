// Measures how long the engine survives, and why it stops.
//
// The fitness harness answers the same question by playing games to the end and
// averaging their length. That is the honest measurement and it is ruinously
// slow: a game lasts tens of thousands of moves, each game contributes exactly
// one observation, and the observation is a draw from something close to an
// exponential, so the error on the mean only falls as 1/sqrt(games). Telling a
// 5% change from noise takes hundreds of games and most of a day.
//
// What this harness measures instead is the hazard: the probability that the
// deal arriving at the current board is one the engine cannot play in full.
// A game ends on the first such deal, so
//
//     mean game length = 1 / (average hazard of the boards the engine visits)
//
// and the average on the right can be sampled every move rather than once per
// game. Two things make that worth doing:
//
//   - The hazard of a board is computable exactly. Deals come from a known
//     finite set, and asking "can all of these be placed somewhere" is far
//     cheaper than asking "where do they go", because it may stop at the first
//     arrangement that works instead of scoring every one. So a board yields a
//     number rather than a coin flip, which is where the variance goes.
//
//   - That number does not depend on the evaluation weights. Whether a deal
//     fits is a fact about the board; the weights only choose which boards get
//     visited. So the hazard is a fixed ruler, and a change to the weights is
//     measured by the danger of the boards it leaves behind.
//
// Deaths are still counted, and the two estimates of mean game length are
// reported side by side: they are measuring the same quantity, so a run where
// they disagree by more than their error bars is a run to distrust.
//
// See docs/game-length.md.

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

// === Playability
//
// Everything here asks whether pieces fit, never where they should go. A deal
// the engine cannot place in full is the end of the game, and that is the only
// property of a deal this file needs.

bool fitsAnywhere(const GameState &state, Piece piece) {
    const auto placements = state.nextStates(piece);
    return placements.begin() != placements.end();
}

bool samePiece(Piece a, Piece b) {
    return a.getBitBoard() == b.getBitBoard();
}

// Which of the pool's pieces fit on this board, as a bit per pool index.
uint64_t fitMask(const GameState &state, const std::vector<Piece> &pool) {
    uint64_t mask = 0;
    for (size_t i = 0; i < pool.size(); ++i) {
        if (fitsAnywhere(state, pool[i])) {
            mask |= 1ULL << i;
        }
    }
    return mask;
}

// Is there an order and a set of placements that puts every one of these pieces
// on the board? This is exactly the condition AI::makeMoveSimple searches for --
// it returns a full board when it fails -- so a deal this says no to is a deal
// the game ends on.
//
// Written as a plain search, and used to check the faster counting below rather
// than to do the counting itself.
bool dealPlayable(const GameState &state, const Piece *pieces, int n) {
    if (n == 0) {
        return true;
    }
    for (int i = 0; i < n; ++i) {
        // Two identical pieces lead to identical searches.
        bool already_tried = false;
        for (int j = 0; j < i; ++j) {
            if (samePiece(pieces[i], pieces[j])) {
                already_tried = true;
                break;
            }
        }
        if (already_tried) {
            continue;
        }

        Piece rest[3];
        int num_rest = 0;
        for (int j = 0; j < n; ++j) {
            if (j != i) {
                rest[num_rest++] = pieces[j];
            }
        }

        for (const auto after : state.nextStates(pieces[i])) {
            if (dealPlayable(after, rest, n - 1)) {
                return true;
            }
        }
    }
    return false;
}

// === The hazard of a board
//
// The exact fraction of deals that end the game here. Deals are `deal_size`
// pieces drawn independently from `pool`, which is how the games below are
// dealt, so this is the probability the next deal is the last one.
//
// Counting them one at a time would mean a search per deal -- 103,823 of them
// for a three piece deal from the full pool. Instead the work is done over
// pieces rather than over deals: for every pair of pieces, which pieces still
// fit once both of those are down. A three piece deal is playable exactly when
// one of its three pieces still fits after the other two, so that table answers
// every deal at once, and it is a table of 1,128 entries rather than 103,823.
//
// Each entry stops as soon as every piece in the pool has been shown to fit,
// which on a board with room on it is after the first placement it looks at.

struct BoardHazard {
    double probability = 0.0;
    // Pool pieces with nowhere to go on this board. The cheap end of the same
    // question, kept to see how well it stands in for the expensive end.
    int unfittable = 0;
    // The pair table stopped early somewhere, so `probability` is an upper
    // bound rather than the exact value. See `--pair-cap`.
    bool capped = false;
};

// Which pieces fit once `first` and `second` are both on the board, in
// whichever order lets them both down.
uint64_t maskAfterPair(const GameState &state, const std::vector<Piece> &pool,
                       Piece first, Piece second, uint64_t full_mask,
                       int board_cap, bool *capped) {
    uint64_t mask = 0;
    int boards_seen = 0;
    bool anything_cleared = false;
    const int blocks_if_nothing_clears = state.getBitBoard().count()
        + first.getBitBoard().count() + second.getBitBoard().count();

    for (const auto after_first : state.nextStates(first)) {
        for (const auto after_both : after_first.nextStates(second)) {
            if (after_both.getBitBoard().count() < blocks_if_nothing_clears) {
                anything_cleared = true;
            }
            mask |= fitMask(after_both, pool);
            if (mask == full_mask) {
                return mask;
            }
            if (++boards_seen >= board_cap) {
                *capped = true;
                return mask;
            }
        }
    }

    // Played the other way round the two pieces land on the same board, unless
    // one of them completed a line on the way -- so the second order is only
    // worth walking when something cleared, or when the first order never got
    // both pieces down at all.
    if (samePiece(first, second) || (!anything_cleared && mask != 0)) {
        return mask;
    }

    for (const auto after_second : state.nextStates(second)) {
        for (const auto after_both : after_second.nextStates(first)) {
            mask |= fitMask(after_both, pool);
            if (mask == full_mask) {
                return mask;
            }
            if (++boards_seen >= board_cap) {
                *capped = true;
                return mask;
            }
        }
    }
    return mask;
}

// Which pieces fit once `first` is on the board.
uint64_t maskAfterOne(const GameState &state, const std::vector<Piece> &pool,
                      Piece first, uint64_t full_mask) {
    uint64_t mask = 0;
    for (const auto after_first : state.nextStates(first)) {
        mask |= fitMask(after_first, pool);
        if (mask == full_mask) {
            return mask;
        }
    }
    return mask;
}

BoardHazard boardHazard(const GameState &state, const std::vector<Piece> &pool,
                        int deal_size, int pair_cap) {
    const size_t n = pool.size();
    const uint64_t full_mask =
        n == 64 ? ~0ULL : ((1ULL << n) - 1);

    BoardHazard result;
    const uint64_t fits_now = fitMask(state, pool);
    for (size_t i = 0; i < n; ++i) {
        if (!(fits_now & (1ULL << i))) {
            result.unfittable++;
        }
    }

    if (deal_size == 1) {
        result.probability = (double)result.unfittable / (double)n;
        return result;
    }

    std::vector<uint64_t> after_one(n);
    for (size_t i = 0; i < n; ++i) {
        after_one[i] = maskAfterOne(state, pool, pool[i], full_mask);
    }

    if (deal_size == 2) {
        uint64_t fatal_ordered = 0;
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i; j < n; ++j) {
                const bool playable = (after_one[i] & (1ULL << j))
                    || (after_one[j] & (1ULL << i));
                if (!playable) {
                    fatal_ordered += (i == j) ? 1 : 2;
                }
            }
        }
        result.probability = (double)fatal_ordered / ((double)n * (double)n);
        return result;
    }

    // deal_size == 3.
    std::vector<uint64_t> after_pair(n * n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i; j < n; ++j) {
            const uint64_t mask = maskAfterPair(state, pool, pool[i], pool[j],
                                                full_mask, pair_cap, &result.capped);
            after_pair[i * n + j] = mask;
            after_pair[j * n + i] = mask;
        }
    }

    uint64_t fatal_ordered = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i; j < n; ++j) {
            for (size_t k = j; k < n; ++k) {
                const bool playable = (after_pair[i * n + j] & (1ULL << k))
                    || (after_pair[i * n + k] & (1ULL << j))
                    || (after_pair[j * n + k] & (1ULL << i));
                if (playable) {
                    continue;
                }
                // How many of the n^3 ordered deals this unordered one stands for.
                if (i == j && j == k) {
                    fatal_ordered += 1;
                } else if (i == j || j == k) {
                    fatal_ordered += 3;
                } else {
                    fatal_ordered += 6;
                }
            }
        }
    }
    result.probability =
        (double)fatal_ordered / ((double)n * (double)n * (double)n);
    return result;
}

// === Statistics

double percentile(const std::vector<uint64_t> &sorted, double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double idx = p * (double)(sorted.size() - 1);
    const size_t lo = (size_t)std::floor(idx);
    const size_t hi = (size_t)std::ceil(idx);
    return sorted[lo] * (1.0 - (idx - lo)) + sorted[hi] * (idx - lo);
}

// Exact Poisson confidence interval for a count, via the chi-square quantiles
// its bounds are usually written with. Wilson-Hilferty is close enough here:
// the interval is a sanity bar on a plot, not a hypothesis test.
double chiSquareQuantile(double p, double dof) {
    if (dof <= 0.0) {
        return 0.0;
    }
    // Inverse normal CDF, Acklam's rational approximation.
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double p_low = 0.02425;
    double z;
    if (p < p_low) {
        const double q = std::sqrt(-2.0 * std::log(p));
        z = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
            / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p <= 1.0 - p_low) {
        const double q = p - 0.5;
        const double r = q * q;
        z = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
            / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        z = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
            / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    const double h = 2.0 / (9.0 * dof);
    const double base = 1.0 - h + z * std::sqrt(h);
    return dof * base * base * base;
}

struct Interval {
    double lo;
    double hi;
};

// A 95% interval for the true rate behind `deaths` deaths in `moves` moves.
Interval poissonRateInterval(uint64_t deaths, uint64_t moves) {
    if (moves == 0) {
        return {0.0, 0.0};
    }
    const double lo = deaths == 0
        ? 0.0
        : 0.5 * chiSquareQuantile(0.025, 2.0 * (double)deaths);
    const double hi = 0.5 * chiSquareQuantile(0.975, 2.0 * (double)(deaths + 1));
    return {lo / (double)moves, hi / (double)moves};
}

// === Configuration

struct Config {
    uint64_t move_budget = 200000;
    uint64_t death_budget = 0;      // 0 = run until the move budget is spent.
    uint64_t max_game_moves = 0;    // 0 = play every game to its end.
    unsigned threads = 0;           // 0 = one per core.
    uint64_t seed_base = 0;         // 0 = draw seeds from random_device.
    uint64_t hazard_every = 0;      // 0 = do not measure the hazard at all.
    int deal_size = 3;
    // The deals the hazard is measured against, when they are not the deals
    // being played. Measuring the real pool answers "how long does this last".
    // Measuring a harder one answers "how much punishment do the boards this
    // leaves behind take" -- the same boards, so the engine still plays its
    // real game, but a bigger number and so a cheaper one to pin down. Only
    // about five times bigger in practice: the engine keeps the board clean
    // enough that even a deal of nothing but five square pieces usually fits.
    std::string hazard_pool_spec = "";  // Empty = the pool being played.
    int hazard_deal_size = 0;           // 0 = the deal size being played.
    int pair_cap = 4096;
    std::string pool_spec = "all";
    EvalWeights weights = EvalWeights::getDefault();
    uint64_t self_check = 0;
    bool exhaustive_check = false;
    bool json = false;
};

// The pieces a deal is drawn from. Shrinking the pool is the difficulty knob:
// the engine dies far more often when every piece is a big one, so a run costs
// seconds instead of hours. What it buys and what it costs is in
// docs/game-length.md.
bool parsePool(const std::string &spec, std::vector<int> *out) {
    out->clear();
    if (spec == "all") {
        for (int i = 0; i < Piece::NUM_PIECES; ++i) {
            out->push_back(i);
        }
        return true;
    }
    // Sizes, by where Piece::byIndex puts them.
    if (spec == "no-singles") {  // Drop the 1x1 and the dominoes.
        for (int i = 5; i < Piece::NUM_PIECES; ++i) {
            out->push_back(i);
        }
        return true;
    }
    if (spec == "big") {  // Four and five square pieces.
        for (int i = 13; i < Piece::NUM_PIECES; ++i) {
            out->push_back(i);
        }
        return true;
    }
    if (spec == "brutal") {  // Five square pieces only.
        for (int i = 32; i < Piece::NUM_PIECES; ++i) {
            out->push_back(i);
        }
        return true;
    }

    size_t pos = 0;
    while (pos < spec.size()) {
        size_t comma = spec.find(',', pos);
        if (comma == std::string::npos) {
            comma = spec.size();
        }
        const int index = std::atoi(spec.substr(pos, comma - pos).c_str());
        if (index < 0 || index >= Piece::NUM_PIECES) {
            return false;
        }
        out->push_back(index);
        pos = comma + 1;
    }
    return !out->empty() && out->size() <= 64;
}

// === Sampling

struct HazardSample {
    uint64_t move_index;  // How far into its game the board was.
    uint32_t game_index;  // Which of this thread's games it came from.
    double hazard;
    int unfittable;
};

struct ThreadResult {
    uint64_t moves = 0;
    std::vector<uint64_t> game_lengths;  // Games that ended in a death.
    uint64_t censored_games = 0;         // Games cut short by a budget or cap.
    uint64_t censored_moves = 0;
    std::vector<HazardSample> hazard_samples;
    double hazard_seconds = 0.0;
    uint64_t capped_boards = 0;
};

// Buckets for the hazard as a function of how far into the game the board is.
// Spaced by powers of about three, because the question is whether the shape
// changes over orders of magnitude, not over hundreds of moves.
const uint64_t BUCKET_EDGES[] = {0, 10, 30, 100, 300, 1000, 3000, 10000, 30000,
                                 100000, 300000};
const size_t NUM_BUCKETS = sizeof(BUCKET_EDGES) / sizeof(BUCKET_EDGES[0]);

size_t bucketOf(uint64_t move_index) {
    size_t bucket = 0;
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        if (move_index >= BUCKET_EDGES[i]) {
            bucket = i;
        }
    }
    return bucket;
}

void runWorker(const Config &config, const std::vector<Piece> &pool,
               const std::vector<Piece> &hazard_pool, int hazard_deal_size,
               std::atomic<uint64_t> *moves_taken,
               std::atomic<uint64_t> *deaths_taken, uint64_t seed,
               ThreadResult *out) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<size_t> piece_dist(0, pool.size() - 1);

    GameState game(BitBoard::empty());
    uint64_t move_index = 0;
    uint32_t game_index = 0;

    for (;;) {
        const uint64_t taken = moves_taken->fetch_add(1, std::memory_order_relaxed);
        if (taken >= config.move_budget) {
            break;
        }
        if (config.death_budget > 0
            && deaths_taken->load(std::memory_order_relaxed) >= config.death_budget) {
            break;
        }

        if (config.hazard_every > 0 && taken % config.hazard_every == 0) {
            const auto start = std::chrono::steady_clock::now();
            const BoardHazard hazard =
                boardHazard(game, hazard_pool, hazard_deal_size, config.pair_cap);
            out->hazard_seconds +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (hazard.capped) {
                out->capped_boards++;
            }
            out->hazard_samples.push_back(
                {move_index, game_index, hazard.probability, hazard.unfittable});
        }

        Piece deal[3];
        for (int i = 0; i < config.deal_size; ++i) {
            deal[i] = pool[piece_dist(rng)];
        }
        game = AI::makeMoveSimple(config.weights, game,
                                  PieceSet(deal[0], deal[1], deal[2]));
        out->moves++;
        move_index++;

        if (game.isOver()) {
            out->game_lengths.push_back(move_index);
            deaths_taken->fetch_add(1, std::memory_order_relaxed);
            game = GameState(BitBoard::empty());
            move_index = 0;
            game_index++;
        } else if (config.max_game_moves > 0 && move_index >= config.max_game_moves) {
            out->censored_games++;
            out->censored_moves += move_index;
            game = GameState(BitBoard::empty());
            move_index = 0;
            game_index++;
        }
    }

    if (move_index > 0) {
        out->censored_games++;
        out->censored_moves += move_index;
    }
}

// === Self check
//
// The counting above reaches the same answer as a plain search by a route that
// is easy to get subtly wrong, and a hazard that is quietly too low reads as an
// engine that quietly got better. So: play to a random board, then check that
// the pair table, the plain search, and the move search the engine actually
// uses all agree about which deals are playable.
int selfCheck(const Config &config, const std::vector<Piece> &pool, uint64_t boards) {
    std::mt19937_64 rng(config.seed_base ? config.seed_base : 20240607);
    std::uniform_int_distribution<size_t> piece_dist(0, pool.size() - 1);
    std::uniform_int_distribution<int> depth_dist(0, 60);

    int failures = 0;
    for (uint64_t b = 0; b < boards; ++b) {
        GameState game(BitBoard::empty());
        const int depth = depth_dist(rng);
        for (int i = 0; i < depth && !game.isOver(); ++i) {
            Piece deal[3];
            for (int j = 0; j < config.deal_size; ++j) {
                deal[j] = pool[piece_dist(rng)];
            }
            game = AI::makeMoveSimple(config.weights, game,
                                      PieceSet(deal[0], deal[1], deal[2]));
        }
        if (game.isOver()) {
            continue;
        }

        // Every deal, counted the fast way.
        const BoardHazard hazard =
            boardHazard(game, pool, config.deal_size, 1 << 30);
        if (hazard.capped) {
            std::fprintf(stderr, "board %llu: capped during a self check\n",
                         (unsigned long long)b);
            failures++;
        }

        // A sample of deals, checked both slow ways.
        for (int trial = 0; trial < 200; ++trial) {
            Piece deal[3];
            for (int j = 0; j < config.deal_size; ++j) {
                deal[j] = pool[piece_dist(rng)];
            }
            const bool searched = dealPlayable(game, deal, config.deal_size);
            const bool engine_played = !AI::makeMoveSimple(
                config.weights, game,
                PieceSet(deal[0], deal[1],
                         config.deal_size == 3 ? deal[2] : Piece())).isOver();
            if (searched != engine_played) {
                std::fprintf(stderr,
                             "board %llu: search says %d, engine says %d, on\n%s\n",
                             (unsigned long long)b, (int)searched, (int)engine_played,
                             game.getBitBoard().str().c_str());
                failures++;
            }
        }

        // The fast count has to agree with the slow one over the whole deal
        // space, not just a sample of it, so count it out the slow way too.
        // Cheap for a small pool or a small deal; minutes a board for three
        // pieces out of all forty seven, which is what --exhaustive is for.
        if (config.exhaustive_check || config.deal_size <= 2 || pool.size() <= 12) {
            uint64_t fatal = 0;
            uint64_t total = 0;
            const size_t n = pool.size();
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < (config.deal_size >= 2 ? n : 1); ++j) {
                    for (size_t k = 0; k < (config.deal_size >= 3 ? n : 1); ++k) {
                        Piece deal[3] = {pool[i], pool[j], pool[k]};
                        total++;
                        if (!dealPlayable(game, deal, config.deal_size)) {
                            fatal++;
                        }
                    }
                }
            }
            const double slow = (double)fatal / (double)total;
            if (std::fabs(slow - hazard.probability) > 1e-12) {
                std::fprintf(stderr,
                             "board %llu: table says %.12f, enumeration says %.12f\n%s\n",
                             (unsigned long long)b, hazard.probability, slow,
                             game.getBitBoard().str().c_str());
                failures++;
            }
        }
    }

    std::fprintf(stderr, "%s: %d failure(s) over %llu boards\n",
                 failures == 0 ? "ok" : "FAILED", failures,
                 (unsigned long long)boards);
    return failures == 0 ? 0 : 1;
}

void usage() {
    std::fprintf(stderr,
        "usage: survival [options]\n"
        "\n"
        "  --moves N          total moves to spend across all threads (default 200000)\n"
        "  --deaths N         stop early once N games have ended\n"
        "  --max-game-moves N restart a game that reaches N moves, and count it as censored\n"
        "  --threads N        default: one per core\n"
        "  --seed-base S      deterministic per game seeds S, S+1, ... (default: random)\n"
        "  --hazard-every M   measure the exact hazard every M moves (default: off)\n"
        "  --deal-size N      pieces per deal, 1-3 (default 3)\n"
        "  --pool SPEC        all | no-singles | big | brutal | comma separated indices\n"
        "  --hazard-pool SPEC measure the hazard against these deals instead of the ones\n"
        "                     being played, which reads as how much punishment the boards\n"
        "                     take rather than as a game length (default: --pool)\n"
        "  --hazard-deal-size N  pieces per deal for that measurement (default: --deal-size)\n"
        "  --weights W,...    the 12 evaluation weights (default: the trained ones)\n"
        "  --pair-cap N       boards to examine per piece pair before giving up (default 4096)\n"
        "  --self-check N     check the hazard counting against a plain search on N boards\n"
        "  --exhaustive       make --self-check enumerate every deal, however long it takes\n"
        "  --json             machine readable output on stdout\n");
}

}  // namespace

int main(int argc, char **argv) {
    Config config;
    std::vector<int> pool_indices;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--moves" && has_value) {
            config.move_budget = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--deaths" && has_value) {
            config.death_budget = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--max-game-moves" && has_value) {
            config.max_game_moves = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--threads" && has_value) {
            config.threads = (unsigned)std::atoi(argv[++i]);
        } else if (arg == "--seed-base" && has_value) {
            config.seed_base = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--hazard-every" && has_value) {
            config.hazard_every = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--deal-size" && has_value) {
            config.deal_size = std::atoi(argv[++i]);
        } else if (arg == "--pool" && has_value) {
            config.pool_spec = argv[++i];
        } else if (arg == "--hazard-pool" && has_value) {
            config.hazard_pool_spec = argv[++i];
        } else if (arg == "--hazard-deal-size" && has_value) {
            config.hazard_deal_size = std::atoi(argv[++i]);
        } else if (arg == "--pair-cap" && has_value) {
            config.pair_cap = std::atoi(argv[++i]);
        } else if (arg == "--self-check" && has_value) {
            config.self_check = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--exhaustive") {
            config.exhaustive_check = true;
        } else if (arg == "--json") {
            config.json = true;
        } else if (arg == "--weights" && has_value) {
            const std::string spec = argv[++i];
            size_t pos = 0;
            int n = 0;
            while (pos < spec.size() && n < EvalWeights::NUM_WEIGHTS) {
                size_t comma = spec.find(',', pos);
                if (comma == std::string::npos) {
                    comma = spec.size();
                }
                config.weights.weights[n++] =
                    std::atoi(spec.substr(pos, comma - pos).c_str());
                pos = comma + 1;
            }
            if (n != EvalWeights::NUM_WEIGHTS) {
                std::fprintf(stderr, "--weights needs %d values, got %d\n",
                             EvalWeights::NUM_WEIGHTS, n);
                return 1;
            }
        } else {
            usage();
            return 1;
        }
    }

    if (config.deal_size < 1 || config.deal_size > 3) {
        std::fprintf(stderr, "--deal-size must be 1, 2 or 3\n");
        return 1;
    }
    if (!parsePool(config.pool_spec, &pool_indices)) {
        std::fprintf(stderr, "could not read --pool %s\n", config.pool_spec.c_str());
        return 1;
    }

    std::vector<Piece> pool;
    pool.reserve(pool_indices.size());
    for (const int index : pool_indices) {
        pool.push_back(Piece::byIndex(index));
    }

    // The hazard is measured against the deals being played unless told
    // otherwise. When it is told otherwise it stops being a game length and
    // becomes a stress reading, and the report below says so.
    const bool hazard_is_the_game = config.hazard_pool_spec.empty()
        && (config.hazard_deal_size == 0 || config.hazard_deal_size == config.deal_size);
    std::vector<Piece> hazard_pool = pool;
    if (!config.hazard_pool_spec.empty()) {
        std::vector<int> hazard_indices;
        if (!parsePool(config.hazard_pool_spec, &hazard_indices)) {
            std::fprintf(stderr, "could not read --hazard-pool %s\n",
                         config.hazard_pool_spec.c_str());
            return 1;
        }
        hazard_pool.clear();
        for (const int index : hazard_indices) {
            hazard_pool.push_back(Piece::byIndex(index));
        }
    }
    const int hazard_deal_size =
        config.hazard_deal_size > 0 ? config.hazard_deal_size : config.deal_size;
    if (hazard_deal_size < 1 || hazard_deal_size > 3) {
        std::fprintf(stderr, "--hazard-deal-size must be 1, 2 or 3\n");
        return 1;
    }

    if (config.self_check > 0) {
        return selfCheck(config, pool, config.self_check);
    }

    unsigned num_threads = config.threads;
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
    }
    if (num_threads == 0) {
        num_threads = 1;
    }

    std::vector<uint64_t> seeds(num_threads);
    if (config.seed_base == 0) {
        std::random_device rd;
        for (unsigned t = 0; t < num_threads; ++t) {
            seeds[t] = ((uint64_t)rd() << 32) | rd();
        }
    } else {
        for (unsigned t = 0; t < num_threads; ++t) {
            seeds[t] = config.seed_base + t;
        }
    }

    std::fprintf(stderr,
                 "%llu moves across %u threads, pool=%s (%zu pieces), deal=%d%s\n",
                 (unsigned long long)config.move_budget, num_threads,
                 config.pool_spec.c_str(), pool.size(), config.deal_size,
                 config.hazard_every > 0 ? ", measuring the hazard" : "");

    std::atomic<uint64_t> moves_taken{0};
    std::atomic<uint64_t> deaths_taken{0};
    std::vector<ThreadResult> results(num_threads);

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            runWorker(config, pool, hazard_pool, hazard_deal_size, &moves_taken,
                      &deaths_taken, seeds[t], &results[t]);
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    const double wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // === Pull the threads together.
    uint64_t total_moves = 0;
    uint64_t censored_games = 0;
    uint64_t censored_moves = 0;
    uint64_t capped_boards = 0;
    double hazard_seconds = 0.0;
    std::vector<uint64_t> lengths;
    for (const auto &r : results) {
        total_moves += r.moves;
        censored_games += r.censored_games;
        censored_moves += r.censored_moves;
        capped_boards += r.capped_boards;
        hazard_seconds += r.hazard_seconds;
        lengths.insert(lengths.end(), r.game_lengths.begin(), r.game_lengths.end());
    }
    std::sort(lengths.begin(), lengths.end());
    const uint64_t deaths = lengths.size();

    // Deaths per move, and the game length that implies. This is the estimate
    // the old harness makes, phrased as a rate so that games cut short by the
    // budget still contribute the moves they survived.
    const double death_rate = total_moves > 0 ? (double)deaths / (double)total_moves : 0.0;
    const Interval rate_ci = poissonRateInterval(deaths, total_moves);

    // The same rate read off the boards instead of the deaths. Blocks of
    // consecutive samples stand in for independent ones, because a board and
    // the board a few moves later are nearly the same board.
    double hazard_mean = 0.0;
    double hazard_block_sem = 0.0;
    double hazard_stddev = 0.0;
    double hazard_max = 0.0;
    size_t num_hazard_samples = 0;
    size_t num_blocks = 0;
    size_t chosen_block_size = 0;
    size_t dangerous_samples = 0;  // Boards where a deal in a thousand is fatal.
    double unfittable_mean = 0.0;
    // Standard error as a function of how many consecutive samples are averaged
    // before being treated as one observation. See below.
    std::vector<std::pair<size_t, double>> blocking_curve;
    {
        double sum = 0.0;
        double square_sum = 0.0;
        double unfittable_sum = 0.0;
        for (const auto &r : results) {
            for (const auto &sample : r.hazard_samples) {
                sum += sample.hazard;
                square_sum += sample.hazard * sample.hazard;
                unfittable_sum += sample.unfittable;
                if (sample.hazard > 1e-3) {
                    dangerous_samples++;
                }
                if (sample.hazard > hazard_max) {
                    hazard_max = sample.hazard;
                }
                num_hazard_samples++;
            }
        }
        if (num_hazard_samples > 0) {
            hazard_mean = sum / (double)num_hazard_samples;
            unfittable_mean = unfittable_sum / (double)num_hazard_samples;
        }
        if (num_hazard_samples > 1) {
            const double variance =
                (square_sum - (double)num_hazard_samples * hazard_mean * hazard_mean)
                / (double)(num_hazard_samples - 1);
            hazard_stddev = std::sqrt(std::max(0.0, variance));
        }

        // Consecutive boards are nearly the same board, so treating each sample
        // as an independent observation would claim an error bar several times
        // smaller than the truth. Averaging runs of samples first breaks that
        // up, and the error the run really has is the one the curve settles on:
        // it climbs while the blocks are still shorter than the board's memory
        // and flattens once they are longer. A curve still climbing at the right
        // hand end is a run that has not yet earned any error bar at all.
        for (size_t block = 1; num_hazard_samples / block >= 16; block *= 4) {
            std::vector<double> block_means;
            for (const auto &r : results) {
                double block_sum = 0.0;
                size_t in_block = 0;
                for (const auto &sample : r.hazard_samples) {
                    block_sum += sample.hazard;
                    if (++in_block == block) {
                        block_means.push_back(block_sum / (double)in_block);
                        block_sum = 0.0;
                        in_block = 0;
                    }
                }
            }
            if (block_means.size() < 2) {
                break;
            }
            double block_total = 0.0;
            for (const double m : block_means) {
                block_total += m;
            }
            const double block_mean = block_total / (double)block_means.size();
            double variance = 0.0;
            for (const double m : block_means) {
                variance += (m - block_mean) * (m - block_mean);
            }
            variance /= (double)(block_means.size() - 1);
            const double sem = std::sqrt(variance / (double)block_means.size());
            // The longest blocks are the most decorrelated, so the last point
            // on the curve is the one to quote. Taking the largest instead
            // would read the noise in these estimates as signal, since each is
            // itself only known to within a fifth or so.
            blocking_curve.push_back({block, sem});
            hazard_block_sem = sem;
            chosen_block_size = block;
            num_blocks = block_means.size();
        }
    }

    // What the hazard estimate is worth, in the currency the old harness is
    // paid in. Counting deaths gives a relative error of 1/sqrt(deaths), so a
    // hazard estimate this precise is worth this many deaths -- and a run
    // reporting far more of these than it saw real deaths is a run that got its
    // answer far sooner than playing games out could have.
    const double effective_deaths = hazard_block_sem > 0.0
        ? (hazard_mean / hazard_block_sem) * (hazard_mean / hazard_block_sem)
        : 0.0;

    // The hazard, bucketed by how far into a game the board was. A flat profile
    // is a memoryless game: the board the engine keeps is as dangerous on move
    // 30,000 as on move 3,000, and nothing but the deal decides when it ends.
    struct Bucket {
        uint64_t count = 0;
        double hazard_sum = 0.0;
        double unfittable_sum = 0.0;
        // One entry per game that passed through this stage. Games are
        // independent, so the spread of these is the error bar the bucket has
        // earned -- and without it a bucket built from two games reads exactly
        // like one built from two hundred.
        std::vector<double> per_game;
    };
    std::vector<Bucket> buckets(NUM_BUCKETS);
    for (const auto &r : results) {
        std::vector<double> game_sum(NUM_BUCKETS, 0.0);
        std::vector<uint64_t> game_count(NUM_BUCKETS, 0);
        uint32_t current_game = 0;
        bool started = false;
        for (const auto &sample : r.hazard_samples) {
            if (started && sample.game_index != current_game) {
                for (size_t i = 0; i < NUM_BUCKETS; ++i) {
                    if (game_count[i] > 0) {
                        buckets[i].per_game.push_back(game_sum[i] / (double)game_count[i]);
                        game_sum[i] = 0.0;
                        game_count[i] = 0;
                    }
                }
            }
            current_game = sample.game_index;
            started = true;

            const size_t index = bucketOf(sample.move_index);
            Bucket &bucket = buckets[index];
            bucket.count++;
            bucket.hazard_sum += sample.hazard;
            bucket.unfittable_sum += sample.unfittable;
            game_sum[index] += sample.hazard;
            game_count[index]++;
        }
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            if (game_count[i] > 0) {
                buckets[i].per_game.push_back(game_sum[i] / (double)game_count[i]);
            }
        }
    }

    // The typical hazard at this stage of a game, and how well that is known,
    // taking each game as one observation.
    auto bucketStats = [](const Bucket &bucket) {
        double mean = 0.0;
        double sem = 0.0;
        const size_t n = bucket.per_game.size();
        if (n > 0) {
            double total = 0.0;
            for (const double value : bucket.per_game) {
                total += value;
            }
            mean = total / (double)n;
        }
        if (n > 1) {
            double variance = 0.0;
            for (const double value : bucket.per_game) {
                variance += (value - mean) * (value - mean);
            }
            sem = std::sqrt(variance / (double)(n - 1) / (double)n);
        }
        return std::pair<double, double>(mean, sem);
    };

    // === Report.
    if (config.json) {
        std::printf("{\n");
        std::printf("  \"pool\": \"%s\",\n", config.pool_spec.c_str());
        std::printf("  \"pool_size\": %zu,\n", pool.size());
        std::printf("  \"deal_size\": %d,\n", config.deal_size);
        std::printf("  \"hazard_pool\": \"%s\",\n",
                    config.hazard_pool_spec.empty() ? config.pool_spec.c_str()
                                                    : config.hazard_pool_spec.c_str());
        std::printf("  \"hazard_deal_size\": %d,\n", hazard_deal_size);
        std::printf("  \"hazard_is_game_length\": %s,\n",
                    hazard_is_the_game ? "true" : "false");
        std::printf("  \"weights\": [");
        for (int i = 0; i < EvalWeights::NUM_WEIGHTS; ++i) {
            std::printf("%s%d", i ? ", " : "", config.weights.weights[i]);
        }
        std::printf("],\n");
        std::printf("  \"moves\": %llu,\n", (unsigned long long)total_moves);
        std::printf("  \"deaths\": %llu,\n", (unsigned long long)deaths);
        std::printf("  \"censored_games\": %llu,\n", (unsigned long long)censored_games);
        std::printf("  \"censored_moves\": %llu,\n", (unsigned long long)censored_moves);
        std::printf("  \"death_rate\": %.10g,\n", death_rate);
        std::printf("  \"death_rate_ci95\": [%.10g, %.10g],\n", rate_ci.lo, rate_ci.hi);
        std::printf("  \"hazard_samples\": %zu,\n", num_hazard_samples);
        std::printf("  \"hazard_blocks\": %zu,\n", num_blocks);
        std::printf("  \"hazard_block_size\": %zu,\n", chosen_block_size);
        std::printf("  \"blocking_curve\": [");
        for (size_t i = 0; i < blocking_curve.size(); ++i) {
            std::printf("%s{\"block\": %zu, \"sem\": %.6g}", i ? ", " : "",
                        blocking_curve[i].first, blocking_curve[i].second);
        }
        std::printf("],\n");
        std::printf("  \"hazard_mean\": %.10g,\n", hazard_mean);
        std::printf("  \"hazard_sem\": %.10g,\n", hazard_block_sem);
        std::printf("  \"hazard_stddev\": %.10g,\n", hazard_stddev);
        std::printf("  \"hazard_max\": %.10g,\n", hazard_max);
        std::printf("  \"dangerous_samples\": %zu,\n", dangerous_samples);
        std::printf("  \"effective_deaths\": %.6g,\n", effective_deaths);
        std::printf("  \"unfittable_mean\": %.10g,\n", unfittable_mean);
        std::printf("  \"capped_boards\": %llu,\n", (unsigned long long)capped_boards);
        std::printf("  \"hazard_seconds\": %.4f,\n", hazard_seconds);
        std::printf("  \"wall_seconds\": %.4f,\n", wall_seconds);
        std::printf("  \"game_lengths\": [");
        for (size_t i = 0; i < lengths.size(); ++i) {
            std::printf("%s%llu", i ? ", " : "", (unsigned long long)lengths[i]);
        }
        std::printf("],\n");
        std::printf("  \"hazard_by_move_index\": [\n");
        bool first = true;
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            if (buckets[i].count == 0) {
                continue;
            }
            const auto stats = bucketStats(buckets[i]);
            std::printf("%s    {\"from\": %llu, \"to\": %lld, \"n\": %llu, "
                        "\"games\": %zu, \"hazard\": %.10g, \"sem\": %.10g, "
                        "\"unfittable\": %.4f}",
                        first ? "" : ",\n",
                        (unsigned long long)BUCKET_EDGES[i],
                        i + 1 < NUM_BUCKETS ? (long long)BUCKET_EDGES[i + 1] : -1,
                        (unsigned long long)buckets[i].count,
                        buckets[i].per_game.size(), stats.first, stats.second,
                        buckets[i].unfittable_sum / (double)buckets[i].count);
            first = false;
        }
        std::printf("\n  ]\n}\n");
        return 0;
    }

    std::printf("moves        %llu in %.1fs (%.0f moves/sec)\n",
                (unsigned long long)total_moves, wall_seconds,
                wall_seconds > 0 ? total_moves / wall_seconds : 0.0);
    std::printf("deaths       %llu (%llu game(s) cut short, %llu moves)\n",
                (unsigned long long)deaths, (unsigned long long)censored_games,
                (unsigned long long)censored_moves);
    if (deaths > 0) {
        std::printf("from deaths  mean length %.0f  95%% CI [%.0f, %.0f]  (+-%.1f%%)\n",
                    1.0 / death_rate,
                    rate_ci.hi > 0 ? 1.0 / rate_ci.hi : 0.0,
                    rate_ci.lo > 0 ? 1.0 / rate_ci.lo : 1.0 / 0.0,
                    // A 95% half width, to be read against the hazard's below.
                    // Quoting one standard error here and two there made the
                    // hazard look like the worse of the two estimators.
                    100.0 * 1.96 / std::sqrt((double)deaths));
    }
    if (num_hazard_samples > 0 && hazard_mean > 0.0) {
        std::printf("%s %.0f  95%% CI [%.0f, %.0f]  (+-%.1f%%)\n",
                    hazard_is_the_game ? "from hazard  mean length"
                                       : "stress       1 in",
                    1.0 / hazard_mean,
                    1.0 / (hazard_mean + 1.96 * hazard_block_sem),
                    hazard_mean > 1.96 * hazard_block_sem
                        ? 1.0 / (hazard_mean - 1.96 * hazard_block_sem)
                        : 1.0 / 0.0,
                    100.0 * 1.96 * hazard_block_sem / hazard_mean);
        if (!hazard_is_the_game) {
            std::printf("             deals of %d from '%s' would end the game here; "
                        "higher is better, but it is not a game length\n",
                        hazard_deal_size,
                        config.hazard_pool_spec.empty() ? config.pool_spec.c_str()
                                                        : config.hazard_pool_spec.c_str());
        }
        std::printf("             %zu samples, %zu block(s) of %zu, %.0f%% of the wall "
                    "clock, %.3f pieces with nowhere to go\n",
                    num_hazard_samples, num_blocks, chosen_block_size,
                    100.0 * hazard_seconds / (wall_seconds * (double)num_threads),
                    unfittable_mean);
        // Against the deaths this many moves should produce, not against the
        // deaths it happened to produce: the second is itself a draw from a
        // Poisson, so an unlucky run would flatter the hazard by a factor of
        // two for no reason.
        const double deaths_expected = (double)total_moves * hazard_mean;
        std::printf("             worth %.0f death(s) of precision, from a run that "
                    "should show about %.0f (it showed %llu)\n",
                    effective_deaths, deaths_expected, (unsigned long long)deaths);
        if (deaths_expected > 0.0) {
            std::printf("             so %.1fx the precision per move, %.1fx after paying "
                        "for it\n",
                        effective_deaths / deaths_expected,
                        effective_deaths / deaths_expected
                            / (1.0 + hazard_seconds / (wall_seconds * (double)num_threads
                                                       - hazard_seconds)));
        }
        std::printf("             1 board in %.0f is worth a thousandth of a death, "
                    "worst seen %.3g\n",
                    dangerous_samples > 0
                        ? (double)num_hazard_samples / (double)dangerous_samples
                        : 1.0 / 0.0,
                    hazard_max);
        if (capped_boards > 0) {
            std::printf("             %llu board(s) hit --pair-cap, so the hazard is an "
                        "upper bound there\n", (unsigned long long)capped_boards);
        }
        std::printf("             error bar vs block size:");
        for (const auto &point : blocking_curve) {
            std::printf(" %zu:%.1f%%", point.first,
                        100.0 * 1.96 * point.second / hazard_mean);
        }
        // A curve that has flattened has found the run's real error bar. One
        // whose last point is still the biggest by some margin has not: the
        // blocks are still shorter than the board's memory, and the number
        // above is smaller than the truth by an unknown amount.
        bool still_climbing = false;
        if (blocking_curve.size() >= 3) {
            double best_before_last = 0.0;
            for (size_t i = 0; i + 1 < blocking_curve.size(); ++i) {
                best_before_last = std::max(best_before_last, blocking_curve[i].second);
            }
            still_climbing = blocking_curve.back().second > 1.1 * best_before_last;
        }
        std::printf("%s\n", still_climbing
            ? "  <- still climbing, so the error bar above is optimistic"
            : "");
    }
    if (deaths > 0) {
        std::printf("lengths      p10=%.0f p25=%.0f p50=%.0f p75=%.0f p90=%.0f "
                    "min=%llu max=%llu\n",
                    percentile(lengths, 0.10), percentile(lengths, 0.25),
                    percentile(lengths, 0.50), percentile(lengths, 0.75),
                    percentile(lengths, 0.90),
                    (unsigned long long)lengths.front(),
                    (unsigned long long)lengths.back());
    }
    if (num_hazard_samples > 0) {
        std::printf("\nhazard by move index\n");
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            if (buckets[i].count == 0) {
                continue;
            }
            const auto stats = bucketStats(buckets[i]);
            char range[32];
            if (i + 1 < NUM_BUCKETS) {
                std::snprintf(range, sizeof(range), "%llu-%llu",
                              (unsigned long long)BUCKET_EDGES[i],
                              (unsigned long long)BUCKET_EDGES[i + 1] - 1);
            } else {
                std::snprintf(range, sizeof(range), "%llu+",
                              (unsigned long long)BUCKET_EDGES[i]);
            }
            std::printf("  %-14s games=%-6zu hazard=%-11.3g +-%-9.3g 1/hazard=%.0f\n",
                        range, buckets[i].per_game.size(), stats.first,
                        1.96 * stats.second,
                        stats.first > 0 ? 1.0 / stats.first : 0.0);
        }
    }
    return 0;
}
