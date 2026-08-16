// What the game actually does with each board of a pair.
//
// `golden` asks the evaluation whether it prefers A. This asks the game, which
// is a different question and the one that decides whether a pair is worth
// keeping. It answers in two parts.
//
// The exact part. For a given board, every hand the game can deal can be
// enumerated and played by the real search, which gives the expected squares
// cleared by the next set with no sampling error at all. The search sorts the
// hand before it looks at anything (solver.cpp), so what it returns depends on
// which three pieces arrived and not on their order: enumerating unordered
// hands and weighting each by how many of the 47^3 orderings it stands for is
// the same number from 18,424 searches instead of 103,824. The same pass gives
// P(no fit) -- the share of hands that cannot be placed at all, which is this
// board's chance of ending the game on the very next set.
//
// The rolled-out part. A pair claims A is better, and better has to survive
// contact with a few moves of play. Both boards are played forward on one
// shared piece stream and compared on squares cleared. That measure is not
// circular: dealt the same pieces, both boards receive the same number of
// squares, so whichever holds fewer afterwards is exactly whichever cleared
// more, and the evaluation never enters its own test. It is a fair comparison
// only when the two boards start with the same number of squares, which is why
// the README asks for that.
//
// Deaths are counted over every trial and never conditioned away. Dropping the
// trials where a side died leaves that side only the futures it survived, which
// is how a board that wins by dying looks good.
#include "golden_common.h"
#include "game.h"
#include "solver.h"
#include "eval.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace {

void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " [--file PATH] [--window LO HI] [--max-trials N]\n"
              << "       [--stop-at T] [--decide-at T] [--equiv E] [--threads T] [--help]\n"
              << "\n"
              << "Measures what the game does with each board of a golden pair: the exact\n"
              << "expected squares cleared by the next set, and how the two boards compare\n"
              << "over a few sets of real play. Positive numbers favour board A.\n"
              << "\n"
              << "It also reads the same window on the eval's own scale, as 'eval now' and\n"
              << "'eval later'. That comparison is circular and cannot say which side is\n"
              << "right; what it catches is the eval disagreeing with itself -- REVERSES,\n"
              << "BREAKS TIE, or fading to nothing from a confident start. A pair it flags\n"
              << "is worth a look even when the clearing verdict is unresolved.\n"
              << "\n"
              << "Options:\n"
              << "  --file PATH      Golden file (default: auto-detected)\n"
              << "  --window LO HI   Sets averaged into the decision (default 3 6). A pair\n"
              << "                   that sets up a combo is behind at one set and ahead\n"
              << "                   later, so the window starts after the setup moves.\n"
              << "  --max-trials N   Cap on paired rollouts per pair (default 16000)\n"
              << "  --stop-at T      Stop a pair early once |t| reaches this (default 6)\n"
              << "  --decide-at T    Bar for the final verdict (default 3)\n"
              << "  --equiv E        Call a pair negligible once its 95% interval fits\n"
              << "                   inside +/-E squares (default 0.05)\n"
              << "  --threads T      Worker threads (default hardware_concurrency)\n"
              << "  --help           Show this help\n";
}

uint64_t splitMix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int windowLow = 3;
int windowHigh = 6;

struct PairState {
    std::string id;
    BitBoard a = BitBoard::empty();
    BitBoard b = BitBoard::empty();
    double clearedA = 0;
    double clearedB = 0;
    double noFitA = 0;
    double noFitB = 0;
    int horizon = 0;
    std::vector<double> stats;
    // The same window, read on the eval's own scale rather than on squares.
    std::vector<double> evalStats;
    double staticDelta = 0;
    double evalMean = 0;
    double evalT = 0;
    long deathsA = 0;
    long deathsB = 0;
    double mean = 0;
    double t = 0;
    bool decided = false;
    bool negligible = false;
};

// One unordered hand's worth of work is a (board, first piece, second piece);
// the third piece runs inside. Splitting on the first piece alone would give
// items whose sizes run 1128 down to 1, and the threads that drew the small
// ones would sit idle waiting for the ones that drew the large.
struct ExactJob {
    int pair;
    int side;
    int first;
    int second;
};

struct ExactTally {
    long cleared = 0;
    long noFit = 0;
    long searches = 0;
};

struct TrialJob {
    int pair;
    int trial;
};

struct TrialResult {
    double stat = 0;
    double evalStat = 0;
    bool diedA = false;
    bool diedB = false;
};

// Mean of a set of paired differences, and how many standard errors it sits
// from zero.
struct MeanAndT { double mean = 0, t = 0, se = 0; };

MeanAndT meanAndT(const std::vector<double> &v) {
    MeanAndT r;
    if (v.size() < 2) return r;
    const double n = (double)v.size();
    for (double x : v) r.mean += x;
    r.mean /= n;
    double variance = 0;
    for (double x : v) { const double d = x - r.mean; variance += d * d; }
    variance /= (n - 1);
    r.se = std::sqrt(variance / n);
    r.t = r.se > 0 ? r.mean / r.se : 0;
    return r;
}

// Pull items off one counter until they run out. Every stage here is a flat
// list spanning every pair, so a thread that finishes one pair's work picks up
// another's instead of waiting at a per-pair barrier.
template <typename Body>
void runQueue(size_t items, unsigned threads, Body body) {
    std::atomic<size_t> next(0);
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t i = next.fetch_add(1);
                if (i >= items) break;
                body(i);
            }
        });
    }
    for (auto &w : workers) w.join();
}

// Play both boards forward on one piece stream, and report the average over the
// window of how many more squares B is carrying than A.
TrialResult runTrial(BitBoard a, BitBoard b, uint64_t seed, int horizon) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);
    GameState game_a(a);
    GameState game_b(b);
    const int start_a = a.count();
    const int start_b = b.count();
    bool alive_a = true;
    bool alive_b = true;
    double total = 0;
    double evalTotal = 0;
    int counted = 0;
    for (int set = 1; set <= horizon; ++set) {
        const PieceSet dealt(Piece::byIndex(piece_dist(rng)),
                             Piece::byIndex(piece_dist(rng)),
                             Piece::byIndex(piece_dist(rng)));
        if (alive_a) {
            const auto move = AI::makeMoveSimpleDefault(game_a, dealt);
            if (move.evaluation == UINT64_MAX || (game_a = move.state).isOver()) alive_a = false;
        }
        if (alive_b) {
            const auto move = AI::makeMoveSimpleDefault(game_b, dealt);
            if (move.evaluation == UINT64_MAX || (game_b = move.state).isOver()) alive_b = false;
        }
        if (set >= windowLow && set <= windowHigh && alive_a && alive_b) {
            total += (double)(game_b.getBitBoard().count() - start_b)
                   - (double)(game_a.getBitBoard().count() - start_a);
            // The same comparison on the eval's own scale. Circular by
            // construction, which is the point: what it detects is the eval
            // disagreeing with itself a few moves later, and no board can be
            // said to disagree with itself by accident.
            evalTotal += (double)game_b.simpleEvalDefault()
                       - (double)game_a.simpleEvalDefault();
            ++counted;
        }
        if (!alive_a && !alive_b) break;
    }
    TrialResult result;
    result.stat = counted ? total / counted : 0.0;
    result.evalStat = counted ? evalTotal / counted : 0.0;
    result.diedA = !alive_a;
    result.diedB = !alive_b;
    return result;
}

}  // namespace

int main(int argc, char **argv) {
    std::string file;
    int max_trials = 16000;
    int first_batch = 1000;
    double stop_at = 6.0;
    double decide_at = 3.0;
    double equivalent = 0.05;
    unsigned threads = std::thread::hardware_concurrency();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) file = argv[++i];
        else if (arg == "--window" && i + 2 < argc) {
            windowLow = std::atoi(argv[++i]);
            windowHigh = std::atoi(argv[++i]);
        }
        else if (arg == "--max-trials" && i + 1 < argc) max_trials = std::atoi(argv[++i]);
        else if (arg == "--batch" && i + 1 < argc) first_batch = std::atoi(argv[++i]);
        else if (arg == "--stop-at" && i + 1 < argc) stop_at = std::atof(argv[++i]);
        else if (arg == "--decide-at" && i + 1 < argc) decide_at = std::atof(argv[++i]);
        else if (arg == "--equiv" && i + 1 < argc) equivalent = std::atof(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) threads = (unsigned)std::atoi(argv[++i]);
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
        else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
        else if (file.empty()) file = arg;
        else { std::cerr << "Unexpected argument: " << arg << "\n"; return 2; }
    }

    if (windowLow < 1 || windowHigh < windowLow) {
        std::cerr << "--window needs 1 <= LO <= HI\n";
        return 2;
    }
    if (threads == 0) threads = 1;
    if (file.empty()) file = golden::findDefaultGoldenFile();

    std::string error;
    auto parsed = golden::parseGoldenFile(file, error);
    if (!error.empty()) { std::cerr << "Parse error: " << error << "\n"; return 2; }
    if (parsed.empty()) { std::cerr << "No pairs found in " << file << "\n"; return 2; }

    const auto started = std::chrono::steady_clock::now();
    std::vector<PairState> pairs(parsed.size());
    for (size_t i = 0; i < parsed.size(); ++i) {
        pairs[i].id = parsed[i].id;
        pairs[i].a = golden::boardFromLines(parsed[i].boardA);
        pairs[i].b = golden::boardFromLines(parsed[i].boardB);
        // What `golden` compares, kept so the rolled-out eval has a baseline to
        // be consistent with.
        pairs[i].staticDelta = (double)GameState(pairs[i].b).simpleEvalDefault()
                             - (double)GameState(pairs[i].a).simpleEvalDefault();
    }

    // Every hand the game can deal, for every board, in one list.
    const int piece_count = Piece::NUM_PIECES;
    const double orderings = (double)piece_count * piece_count * piece_count;
    std::vector<ExactJob> exact_jobs;
    exact_jobs.reserve(pairs.size() * 2 * piece_count * (piece_count + 1) / 2);
    for (size_t p = 0; p < pairs.size(); ++p)
        for (int side = 0; side < 2; ++side)
            for (int first = 0; first < piece_count; ++first)
                for (int second = first; second < piece_count; ++second)
                    exact_jobs.push_back({(int)p, side, first, second});

    std::vector<ExactTally> exact_out(exact_jobs.size());
    runQueue(exact_jobs.size(), threads, [&](size_t index) {
        const ExactJob &job = exact_jobs[index];
        const BitBoard board = job.side == 0 ? pairs[job.pair].a : pairs[job.pair].b;
        const GameState start(board);
        const int occupied = board.count();
        ExactTally tally;
        for (int third = job.second; third < piece_count; ++third) {
            const int i = job.first, j = job.second, k = third;
            // How many of the 47^3 dealt orderings this one hand stands for.
            const int weight = (i == j && j == k) ? 1
                             : ((i == j || j == k || i == k) ? 3 : 6);
            const PieceSet hand(Piece::byIndex(i), Piece::byIndex(j), Piece::byIndex(k));
            const auto move = AI::makeMoveSimpleDefault(start, hand);
            ++tally.searches;
            if (move.evaluation == UINT64_MAX) { tally.noFit += weight; continue; }
            const int placed = Piece::byIndex(i).getBitBoard().count()
                             + Piece::byIndex(j).getBitBoard().count()
                             + Piece::byIndex(k).getBitBoard().count();
            tally.cleared += (long)(occupied + placed - move.state.getBitBoard().count()) * weight;
        }
        exact_out[index] = tally;
    });

    long total_searches = 0;
    std::vector<long> cleared_acc(pairs.size() * 2, 0);
    std::vector<long> nofit_acc(pairs.size() * 2, 0);
    for (size_t index = 0; index < exact_jobs.size(); ++index) {
        const int slot = exact_jobs[index].pair * 2 + exact_jobs[index].side;
        cleared_acc[slot] += exact_out[index].cleared;
        nofit_acc[slot] += exact_out[index].noFit;
        total_searches += exact_out[index].searches;
    }
    for (size_t p = 0; p < pairs.size(); ++p) {
        pairs[p].clearedA = cleared_acc[p * 2] / orderings;
        pairs[p].clearedB = cleared_acc[p * 2 + 1] / orderings;
        pairs[p].noFitA = nofit_acc[p * 2] / orderings;
        pairs[p].noFitB = nofit_acc[p * 2 + 1] / orderings;
        // A board that cannot die on the next set has just been shown not to,
        // exactly, so a rollout only has to reach the end of the window. One
        // that can is played further, where its deaths are the thing to watch.
        const bool can_die = pairs[p].noFitA > 0 || pairs[p].noFitB > 0;
        pairs[p].horizon = can_die ? 40 : windowHigh;
    }
    const auto exact_done = std::chrono::steady_clock::now();

    // Rollouts, in rounds. Every pair still undecided contributes its next
    // batch to one list, so the machine stays full even as pairs drop out.
    int batch = first_batch;
    while (true) {
        std::vector<TrialJob> jobs;
        for (size_t p = 0; p < pairs.size(); ++p) {
            if (pairs[p].decided) continue;
            const int have = (int)pairs[p].stats.size();
            if (have >= max_trials) continue;
            const int want = std::min(batch, max_trials - have);
            for (int i = 0; i < want; ++i) jobs.push_back({(int)p, have + i});
        }
        if (jobs.empty()) break;

        std::vector<TrialResult> results(jobs.size());
        runQueue(jobs.size(), threads, [&](size_t index) {
            const TrialJob &job = jobs[index];
            results[index] = runTrial(pairs[job.pair].a, pairs[job.pair].b,
                splitMix64(0x901DE0ULL + (uint64_t)job.trial * 0x9E3779B97F4A7C15ULL
                                       + (uint64_t)job.pair * 0x632BE59BD9B4E019ULL),
                pairs[job.pair].horizon);
        });

        for (size_t index = 0; index < jobs.size(); ++index) {
            PairState &pair = pairs[jobs[index].pair];
            pair.stats.push_back(results[index].stat);
            pair.evalStats.push_back(results[index].evalStat);
            if (results[index].diedA) ++pair.deathsA;
            if (results[index].diedB) ++pair.deathsB;
        }
        for (auto &pair : pairs) {
            if (pair.decided || pair.stats.size() < 2) continue;
            const MeanAndT clears = meanAndT(pair.stats);
            const MeanAndT evals = meanAndT(pair.evalStats);
            const double mean = clears.mean;
            const double se = clears.se;
            pair.mean = mean;
            pair.t = clears.t;
            pair.evalMean = evals.mean;
            pair.evalT = evals.t;
            // Stop early only on a statistic large enough that looking
            // repeatedly cannot account for it; the final look is judged on the
            // ordinary bar. Or stop the other way, once the interval is small
            // enough that the pair has been measured as not mattering.
            if (std::fabs(pair.t) >= stop_at) pair.decided = true;
            else if (std::fabs(mean) + 1.96 * se < equivalent) {
                pair.decided = true;
                pair.negligible = true;
            }
        }
        batch *= 2;
    }
    const auto rollouts_done = std::chrono::steady_clock::now();

    std::cout << "Golden measure: " << file << "\n";
    std::cout << "window N=" << windowLow << ".." << windowHigh
              << "  stop |t|>=" << stop_at << "  decide |t|>=" << decide_at
              << "  max trials " << max_trials << "  threads " << threads << "\n";
    std::cout << std::string(112, '-') << "\n";
    std::cout << std::left << std::setw(28) << "pair"
              << std::setw(12) << "cleared"
              << std::setw(11) << "P(no fit)"
              << std::setw(11) << "window"
              << std::setw(8) << "t"
              << std::setw(11) << "eval now"
              << std::setw(11) << "eval later"
              << std::setw(16) << "self-consistent"
              << std::setw(9) << "trials"
              << std::setw(11) << "deaths A/B"
              << "verdict\n";
    std::cout << std::string(150, '-') << "\n";

    int keep = 0, drop = 0, unresolved = 0, dangerous = 0, inconsistent = 0;
    for (const auto &pair : pairs) {
        const double no_fit = std::max(pair.noFitA, pair.noFitB);
        std::string verdict;
        if (no_fit > 0) {
            // Both boards can end the game outright, so the thing that decides
            // the pair is which one does it less often -- and that number is
            // already exact, from every hand rather than a sample of them.
            std::ostringstream note;
            note << std::fixed << std::setprecision(5);
            if (pair.noFitA < pair.noFitB) note << "can die -- A is safer (" << pair.noFitA << " vs " << pair.noFitB << ")";
            else if (pair.noFitB < pair.noFitA) note << "can die -- B IS SAFER (" << pair.noFitB << " vs " << pair.noFitA << ")";
            else note << "can die -- equally often (" << pair.noFitA << ")";
            verdict = note.str();
            ++dangerous;
        }
        else if (pair.t >= decide_at) { verdict = "keep"; ++keep; }
        else if (pair.t <= -decide_at) { verdict = "DROP or FLIP -- B is better"; ++drop; }
        else if (pair.negligible) { verdict = "negligible -- no measurable difference"; ++unresolved; }
        else { verdict = "unresolved -- weak pair"; ++unresolved; }

        // Does the eval still say later what it says now? A sign it reverses is
        // the eval contradicting itself; a magnitude that all but vanishes is
        // the eval being loud about something that does not survive a move.
        // Neither says which side is right -- an eval can be wrong in a
        // perfectly self-consistent way, and two of these pairs are -- but a
        // pair that trips either one is worth looking at.
        std::string consistency = "-";
        if (pair.evalStats.size() >= 2) {
            // A reversal is only a reversal if the later opinion is real, so
            // that branch needs significance. Fading does not: an eval that was
            // certain and now has no opinion worth measuring has faded to
            // nothing, and demanding significance there would throw away the
            // strongest case -- which is what aligned-on-cube-edge, loud at
            // 56,242 and down to 76, would otherwise have been.
            const bool later_is_real = std::fabs(pair.evalT) >= decide_at;
            const bool now_favours_a = pair.staticDelta > 0;
            const bool later_favours_a = pair.evalMean > 0;
            std::ostringstream c;
            c << std::fixed << std::setprecision(0);
            if (pair.staticDelta == 0) {
                if (later_is_real) { consistency = "BREAKS TIE"; ++inconsistent; }
            } else {
                const double kept = pair.evalMean / pair.staticDelta;
                if (later_is_real && now_favours_a != later_favours_a) {
                    consistency = "REVERSES";
                    ++inconsistent;
                } else if (kept < 0.25) {
                    if (std::fabs(kept) < 0.005) c << "fades to ~0%";
                    else c << "fades to " << (kept * 100) << "%";
                    consistency = c.str();
                    ++inconsistent;
                } else {
                    c << "holds " << (kept * 100) << "%";
                    consistency = c.str();
                }
            }
        }

        std::cout << std::left << std::setw(28) << pair.id
                  << std::showpos << std::fixed << std::setprecision(5)
                  << std::setw(12) << (pair.clearedA - pair.clearedB)
                  << std::noshowpos << std::setprecision(5) << std::setw(11) << no_fit
                  << std::showpos << std::setprecision(3) << std::setw(11) << pair.mean
                  << std::setprecision(1) << std::setw(8) << pair.t
                  << std::setprecision(0) << std::setw(11) << pair.staticDelta
                  << std::setw(11) << pair.evalMean << std::noshowpos
                  << std::setw(16) << consistency
                  << std::setw(9) << pair.stats.size()
                  << std::setw(11) << (std::to_string(pair.deathsA) + "/" + std::to_string(pair.deathsB))
                  << verdict << "\n";
    }

    const auto seconds = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count() / 1000.0;
    };
    std::cout << std::string(150, '-') << "\n";
    std::cout << "Summary: " << keep << " keep, " << drop << " to drop or flip, "
              << unresolved << " unresolved";
    if (dangerous) std::cout << ", " << dangerous << " that can die";
    std::cout << ";  " << inconsistent << " where the eval does not agree with itself\n";
    std::cout << "cleared and window are A minus B, in squares; positive favours A.\n";
    std::cout << "eval now is what golden compares; eval later is the same difference after the\n"
                 "window of play, so self-consistent means the eval still says what it said.\n";
    std::cout << "Exact pass: " << total_searches << " searches ("
              << (long)pairs.size() * 2 * 103823 << " if hands were ordered), "
              << std::setprecision(1) << seconds(started, exact_done) << "s. Rollouts: "
              << seconds(exact_done, rollouts_done) << "s.\n";
    return 0;
}
