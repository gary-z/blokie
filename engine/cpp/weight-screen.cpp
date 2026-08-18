// A screen for weight changes that costs seconds instead of an hour.
//
// Full-game hazard is the objective, but measuring it to 2% costs about ninety
// minutes an arm: deaths are rare, and the probe estimator only buys 2.6x because
// its variance is dominated by clustering within a chain rather than by the coin
// flip it removes.
//
// The way out is that hazard is E[p(board)] over the boards a policy visits, and
// p(board) -- the chance a random hand cannot be placed -- does not depend on the
// weights at all. Only where the policy goes does. So run the candidate and the
// baseline from the SAME sampled boards on the SAME piece streams, and difference
// their risk. The two walks stay together until the first disagreement and the
// comparison is paired, which is where the variance goes.
//
// What this cannot see is anything that only shows up beyond the window. It is a
// screen; the finalists still have to face full games.
#include "solver.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// The chance a random hand of three cannot be placed on this board, over a fixed
// set of sampled hands. Both sides of a pair see the same hands, so the estimate
// is noisy in the same direction for both and the difference is not.
double risk(GameState state, uint64_t probe_seed, int probes) {
    std::mt19937_64 rng(probe_seed);
    std::uniform_int_distribution<int> pd(0, Piece::NUM_PIECES - 1);
    int failures = 0;
    for (int i = 0; i < probes; ++i) {
        const PieceSet hand(Piece::byIndex(pd(rng)), Piece::byIndex(pd(rng)),
            Piece::byIndex(pd(rng)));
        if (!AI::tripleFits(state, hand)) ++failures;
    }
    return (double)failures / probes;
}

// Mean risk over a window of play from one board on one piece stream. A death
// inside the window counts as risk 1 for that step and ends the walk, which is
// the same accounting the hazard uses.
double windowRisk(const EvalWeights &weights, BitBoard start, uint64_t stream_seed,
                  int horizon, int probes) {
    GameState state(start);
    std::mt19937_64 rng(stream_seed);
    std::uniform_int_distribution<int> pd(0, Piece::NUM_PIECES - 1);
    double total = 0;
    for (int step = 0; step < horizon; ++step) {
        const PieceSet dealt(Piece::byIndex(pd(rng)), Piece::byIndex(pd(rng)),
            Piece::byIndex(pd(rng)));
        const auto move = AI::makeMoveSimple(weights, state, dealt);
        if (move.evaluation == UINT64_MAX) { total += 1.0; break; }
        state = move.state;
        // The probe stream is keyed on the step, not on the board, so both sides
        // of a pair draw the same hands at the same depth.
        total += risk(state, mix(stream_seed * 1000003ULL + step), probes);
        if (state.isOver()) { total += 1.0; break; }
    }
    return total / horizon;
}

std::vector<int> parseWeights(const char *text) {
    std::vector<int> out;
    const char *p = text;
    while (*p) {
        out.push_back((int)std::strtol(p, nullptr, 10));
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    return out;
}

EvalWeights fromVector(const std::vector<int> &v) {
    EvalWeights w;
    for (int i = 0; i < EvalWeights::NUM_WEIGHTS && i < (int)v.size(); ++i) {
        w.weights[i] = v[i];
    }
    return w;
}

}  // namespace

void printUsage(const char *program) {
    std::printf(
        "usage: %s --candidate W0,..,W13 [options]\n\n"
        "  --base W0,..,W13   the vector to measure against (default: the\n"
        "                     shipped weights)\n"
        "  --candidate ...    the vector to measure\n"
        "  --label TEXT       what to call it in the output line\n"
        "  --boards N         starting boards, spread over one game per thread\n"
        "  --streams N        piece streams per board; both sides share them\n"
        "  --horizon N        sets of three played from each starting board\n"
        "  --probes N         hands sampled to price each board's risk\n"
        "  --floor N          least occupancy a starting board may have\n"
        "  --stride N         keep one qualifying board in N, to decorrelate\n"
        "  --seed S           seeds boards, streams and probes\n"
        "  --threads N        worker threads\n\n"
        "A positive delta means the candidate is riskier, which is worse. The\n"
        "standard error is clustered on the starting board.\n", program);
}

int main(int argc, char **argv) {
    int boards_wanted = 400;
    int streams = 6;
    int horizon = 15;
    int probes = 24;
    int floor = 24;
    // Qualifying boards arrive in runs -- one dangerous excursion yields a dozen
    // consecutive crowded boards -- so taking every one of them would count a
    // dozen correlated observations as a dozen independent ones. Skip between
    // keeps.
    int stride = 8;
    uint64_t seed = 777;
    unsigned threads = std::thread::hardware_concurrency();
    std::vector<int> base_v, cand_v;
    std::string label = "candidate";

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--base" && i + 1 < argc) base_v = parseWeights(argv[++i]);
        else if (a == "--candidate" && i + 1 < argc) cand_v = parseWeights(argv[++i]);
        else if (a == "--label" && i + 1 < argc) label = argv[++i];
        else if (a == "--boards" && i + 1 < argc) boards_wanted = std::atoi(argv[++i]);
        else if (a == "--streams" && i + 1 < argc) streams = std::atoi(argv[++i]);
        else if (a == "--horizon" && i + 1 < argc) horizon = std::atoi(argv[++i]);
        else if (a == "--probes" && i + 1 < argc) probes = std::atoi(argv[++i]);
        else if (a == "--floor" && i + 1 < argc) floor = std::atoi(argv[++i]);
        else if (a == "--stride" && i + 1 < argc) stride = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--threads" && i + 1 < argc) threads = (unsigned)std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") { printUsage(argv[0]); return 0; }
        else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
    }
    if (base_v.empty()) {
        const auto d = EvalWeights::getDefault();
        base_v.assign(d.weights, d.weights + EvalWeights::NUM_WEIGHTS);
    }
    if (cand_v.empty()) cand_v = base_v;
    const EvalWeights base = fromVector(base_v);
    const EvalWeights cand = fromVector(cand_v);

    // Starting boards from baseline play, so the window starts where the engine
    // actually finds itself rather than on a board built by hand. Collected from
    // one independent game per thread rather than one long game: crowded boards
    // come in runs, and a dozen consecutive boards from a single dangerous
    // excursion are one observation wearing twelve hats.
    std::vector<BitBoard> boards;
    {
        const int per_thread = (boards_wanted + (int)threads - 1) / (int)threads;
        std::vector<std::vector<BitBoard>> parts(threads);
        std::vector<std::thread> collectors;
        for (unsigned t = 0; t < threads; ++t) {
            collectors.emplace_back([&, t] {
                std::mt19937_64 rng(mix(seed ^ (0xC01EC7ULL + t)));
                std::uniform_int_distribution<int> pd(0, Piece::NUM_PIECES - 1);
                GameState state(BitBoard::empty());
                long seen = 0, guard = 0;
                while ((int)parts[t].size() < per_thread && guard++ < 8000000) {
                    const PieceSet dealt(Piece::byIndex(pd(rng)),
                        Piece::byIndex(pd(rng)), Piece::byIndex(pd(rng)));
                    const auto move = AI::makeMoveSimpleDefault(state, dealt);
                    if (move.evaluation == UINT64_MAX) {
                        state = GameState(BitBoard::empty());
                        continue;
                    }
                    state = move.state;
                    if (state.getBitBoard().count() >= floor &&
                        seen++ % stride == 0) {
                        parts[t].push_back(state.getBitBoard());
                    }
                    if (state.isOver()) state = GameState(BitBoard::empty());
                }
            });
        }
        for (auto &th : collectors) th.join();
        for (auto &part : parts) {
            for (BitBoard b : part) boards.push_back(b);
        }
    }

    const size_t units = boards.size() * (size_t)streams;
    std::vector<double> delta(units, 0);
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (;;) {
                const size_t index = next++;
                if (index >= units) return;
                const size_t board_index = index / streams;
                const size_t stream = index % streams;
                const uint64_t stream_seed =
                    mix(seed ^ (board_index * 0x9E3779B97F4A7C15ULL) ^
                        (stream * 0x632BE59BD9B4E019ULL));
                const double a = windowRisk(base, boards[board_index], stream_seed,
                    horizon, probes);
                const double b = windowRisk(cand, boards[board_index], stream_seed,
                    horizon, probes);
                delta[index] = b - a;   // positive means the candidate is riskier
            }
        });
    }
    for (auto &th : pool) th.join();

    // Clustered on the board: the streams that share a board share its whole
    // future, so they are one observation, not `streams` of them.
    const size_t clusters = boards.size();
    std::vector<double> per_board(clusters, 0);
    for (size_t i = 0; i < units; ++i) per_board[i / streams] += delta[i];
    for (auto &v : per_board) v /= streams;
    double mean = 0;
    for (double d : per_board) mean += d;
    mean /= clusters;
    double var = 0;
    for (double d : per_board) var += (d - mean) * (d - mean);
    var /= (clusters - 1);
    const double se = std::sqrt(var / clusters);
    // Baseline level, for reading the difference as a fraction of the risk.
    double base_level = 0;
    {
        std::atomic<size_t> n2{0};
        std::vector<double> levels(units, 0);
        std::vector<std::thread> p2;
        for (unsigned t = 0; t < threads; ++t) {
            p2.emplace_back([&] {
                for (;;) {
                    const size_t index = n2++;
                    if (index >= units) return;
                    const size_t bi = index / streams;
                    const size_t st = index % streams;
                    levels[index] = windowRisk(base, boards[bi],
                        mix(seed ^ (bi * 0x9E3779B97F4A7C15ULL) ^
                            (st * 0x632BE59BD9B4E019ULL)), horizon, probes);
                }
            });
        }
        for (auto &th : p2) th.join();
        for (double v : levels) base_level += v;
        base_level /= units;
    }

    std::printf("%-26s delta=%+.3e  se=%.3e  t=%+6.2f  base=%.3e  "
                "relative=%+7.2f%%  (%zu boards x %d streams)\n",
                label.c_str(), mean, se, se > 0 ? mean / se : 0.0, base_level,
                base_level > 0 ? 100.0 * mean / base_level : 0.0, clusters,
                streams);
    return 0;
}
