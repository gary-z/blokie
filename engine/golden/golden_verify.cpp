#include "golden_common.h"
#include "game.h"
#include "solver.h"
#include "eval.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cmath>

void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " [--file PATH] [--trials N] [--horizon H] [--threads T] [--seed S] [--probe M] [--help]\n"
              << "\n"
              << "Verifies golden pairs by simulation. For each pair, plays many random\n"
              << "futures from board A and board B using the current engine (makeMoveSimpleDefault)\n"
              << "with the same random piece sequences (common random numbers). Compares\n"
              << "average survival (moves until death) and probe risk (immediate triple-fit rate).\n"
              << "\n"
              << "Options:\n"
              << "  --file PATH     Golden file (default auto)\n"
              << "  --trials N      Monte Carlo rollouts per board (default 200)\n"
              << "  --horizon H     Max sets per rollout before stop (default 2000)\n"
              << "  --threads T     Worker threads (default hardware_concurrency)\n"
              << "  --seed S        Base RNG seed (default 0xC0DE)\n"
              << "  --probe M       Quick probe: sample M triples at starting board only and compare\n"
              << "                  failure rates (much faster, captures immediate risk)\n"
              << "  --verbose       Show per-pair probe details\n"
              << "  --help          Show this help\n"
              << "\n"
              << "Human intuition says A > B. Two simulation signals are reported:\n"
              << "  PLAYOUT: avg survival horizon; higher is better.\n"
              << "  PROBE  : immediate death risk on random triples; lower is better.\n"
              << "Agreement between human, eval, and simulation is reported.\n";
}

struct PlayResult {
    std::string id;
    std::string description;
    BitBoard boardA = BitBoard::empty();
    BitBoard boardB = BitBoard::empty();
    uint64_t evalA;
    uint64_t evalB;
    bool evalPrefA; // true if eval says A better (evalA < evalB)

    // Simulation results
    double avgSurvivalA = 0;
    double avgSurvivalB = 0;
    double survivalDelta = 0; // B - A? Actually A - B for consistency: positive means A lives longer
    bool simPrefA = false; // true if avgA > avgB

    // Probe at starting board only
    double probeFailA = 0; // fraction of random triples that do NOT fit
    double probeFailB = 0;
    bool probePrefA = false; // true if failA < failB

    bool humanEvalAgree = false;
    bool humanSimAgree = false;
    bool humanProbeAgree = false;
    bool evalSimAgree = false;
};

// SplitMix64 for deterministic seeds
uint64_t splitMix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Play one rollout starting from board, using piece sequence derived from seed and sharing across A/B via common sequence.
// We generate piece sequence on the fly via rng seeded with trialSeed. For fair comparison, both boards use same sequence.
// Horizon is number of sets (each set = 3 pieces placed via makeMoveSimple).
int simulateSurvival(BitBoard start, uint64_t trialSeed, int horizon) {
    std::mt19937_64 rng(trialSeed);
    std::uniform_int_distribution<int> pieceDist(0, Piece::NUM_PIECES - 1);
    GameState game(start);
    for (int move = 0; move < horizon; ++move) {
        Piece p0 = Piece::byIndex(pieceDist(rng));
        Piece p1 = Piece::byIndex(pieceDist(rng));
        Piece p2 = Piece::byIndex(pieceDist(rng));
        PieceSet dealt(p0, p1, p2);
        auto result = AI::makeMoveSimpleDefault(game, dealt);
        if (result.evaluation == UINT64_MAX) {
            return move; // died at this move (could not place all three)
        }
        game = result.state;
        if (game.isOver()) return move; // treat as death
    }
    return horizon; // survived whole horizon
}

double probeFailureRate(BitBoard board, uint64_t seed, int probes) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist(0, Piece::NUM_PIECES - 1);
    int fails = 0;
    GameState game(board);
    for (int i = 0; i < probes; ++i) {
        Piece p0 = Piece::byIndex(dist(rng));
        Piece p1 = Piece::byIndex(dist(rng));
        Piece p2 = Piece::byIndex(dist(rng));
        PieceSet ps(p0, p1, p2);
        if (!AI::tripleFits(game, ps)) ++fails;
    }
    return (double)fails / probes;
}

int main(int argc, char **argv) {
    std::string file;
    int trials = 50;
    int horizon = 500;
    int probeM = 0;
    unsigned threads = 0;
    uint64_t baseSeed = 0xC0DE1234ULL;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--file" && i + 1 < argc) file = argv[++i];
        else if (a == "--trials" && i + 1 < argc) trials = std::atoi(argv[++i]);
        else if (a == "--horizon" && i + 1 < argc) horizon = std::atoi(argv[++i]);
        else if (a == "--probe" && i + 1 < argc) probeM = std::atoi(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) threads = (unsigned)std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) baseSeed = std::strtoull(argv[++i], nullptr, 0);
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--help" || a == "-h") { printUsage(argv[0]); return 0; }
        else if (a.rfind("--",0)==0) { std::cerr << "Unknown option " << a << "\n"; printUsage(argv[0]); return 2; }
        else { if (file.empty()) file = a; else { std::cerr << "Unexpected arg " << a << "\n"; return 2; } }
    }

    if (file.empty()) file = golden::findDefaultGoldenFile();
    std::string err;
    auto pairs = golden::parseGoldenFile(file, err);
    if (!err.empty()) { std::cerr << "Parse error: " << err << "\n"; return 2; }
    if (pairs.empty()) { std::cerr << "No pairs in " << file << "\n"; return 2; }

    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
    }
    threads = std::min<unsigned>(threads, (unsigned)trials);

    std::cout << "Golden verify: " << file << "  trials=" << trials << " horizon=" << horizon;
    if (probeM) std::cout << " probeM=" << probeM;
    std::cout << " threads=" << threads << " seed=0x" << std::hex << baseSeed << std::dec << "\n";
    std::cout << "Human says A > B (A preferred). Eval lower is better. Sim higher survival is better. Probe lower fail is better.\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    std::vector<PlayResult> results;
    results.reserve(pairs.size());
    for (auto &p : pairs) {
        PlayResult r;
        r.id = p.id;
        r.description = p.description;
        r.boardA = golden::boardFromLines(p.boardA);
        r.boardB = golden::boardFromLines(p.boardB);
        r.evalA = GameState(r.boardA).simpleEvalDefault();
        r.evalB = GameState(r.boardB).simpleEvalDefault();
        r.evalPrefA = r.evalA < r.evalB;
        results.push_back(std::move(r));
    }

    // For each pair, compute simulations
    // We'll parallelize per pair? Simpler: iterate pairs sequentially, but parallelize trials within each pair via threads.

    int totalPairs = results.size();
    int evalAgree = 0, simAgree = 0, probeAgree = 0, evalSimAgree = 0;

    for (auto &r : results) {
        // Probe quick check at start board (always do, cheap)
        int probeSamples = probeM ? probeM : 2000; // default probe samples if not quick mode
        // Use deterministic seed per pair
        uint64_t pairSeed = splitMix64(baseSeed ^ std::hash<std::string>{}(r.id));
        r.probeFailA = probeFailureRate(r.boardA, splitMix64(pairSeed + 0xA5A5), probeSamples);
        r.probeFailB = probeFailureRate(r.boardB, splitMix64(pairSeed + 0x5A5A), probeSamples);
        r.probePrefA = r.probeFailA < r.probeFailB - 1e-9; // need strict less, else tie
        // If tie within epsilon, consider probe not preferring (false) - will count as disagree if human strictly pref A

        if (probeM == 0 || probeM) {
            // Also do full playout unless probe-only requested? If user gave --probe, we could skip playout to save time.
            // We'll do both unless probeM && trials==0? For now if probeM and trials==0 skip? But we always do playout unless we want quick.
            // Let's skip playout if probeM explicitly given and user wants quick: honor probeM as quick mode, still do playout unless --trials 0.
            // We'll do playout anyway as long as trials>0.
        }

        if (trials > 0) {
            // Playout simulation with common random numbers
            // For each trial, we need same piece sequence for A and B. So generate trialSeed per trial, then simulate both.
            std::vector<int> survA(trials), survB(trials);
            // Parallelize trials
            std::atomic<int> next(0);
            std::vector<std::thread> workers;
            unsigned tcount = std::min<unsigned>(threads, trials);
            for (unsigned t = 0; t < tcount; ++t) {
                workers.emplace_back([&]() {
                    while (true) {
                        int idx = next.fetch_add(1);
                        if (idx >= trials) break;
                        uint64_t trialSeed = splitMix64(pairSeed + 100000 + idx * 0x9E3779B97F4A7C15ULL);
                        // Common sequence: we could use same seed for both boards, simulation will draw same pieces in same order
                        // Our simulateSurvival already draws pieces from seed deterministically, so using same trialSeed for both achieves common numbers.
                        int a = simulateSurvival(r.boardA, trialSeed, horizon);
                        int b = simulateSurvival(r.boardB, trialSeed, horizon);
                        survA[idx] = a;
                        survB[idx] = b;
                    }
                });
            }
            for (auto &th : workers) th.join();
            double sumA = 0, sumB = 0;
            for (int v : survA) sumA += v;
            for (int v : survB) sumB += v;
            r.avgSurvivalA = sumA / trials;
            r.avgSurvivalB = sumB / trials;
            r.survivalDelta = r.avgSurvivalA - r.avgSurvivalB;
            r.simPrefA = r.avgSurvivalA > r.avgSurvivalB + 1e-9;
            // Tie => false (not pref A)
        } else {
            r.avgSurvivalA = r.avgSurvivalB = horizon;
            r.simPrefA = false;
        }

        r.humanEvalAgree = r.evalPrefA;
        r.humanSimAgree = r.simPrefA;
        r.humanProbeAgree = r.probePrefA;
        r.evalSimAgree = (r.evalPrefA == r.simPrefA);

        if (r.humanEvalAgree) ++evalAgree;
        if (r.humanSimAgree) ++simAgree;
        if (r.humanProbeAgree) ++probeAgree;
        if (r.evalSimAgree) ++evalSimAgree;
    }

    // Report per pair
    std::cout << std::left << std::setw(28) << "ID"
              << " " << std::setw(6) << "EVAL"
              << " " << std::setw(6) << "SIM"
              << " " << std::setw(6) << "PROBE"
              << " " << std::setw(12) << "evalA/B"
              << " " << std::setw(14) << "simA/B"
              << " " << std::setw(14) << "probeA/B"
              << " note\n";
    std::cout << std::string(110, '-') << "\n";
    for (auto &r : results) {
        std::string evalStr = r.evalPrefA ? "A>B" : "B>A";
        if (r.evalA == r.evalB) evalStr = "A=B";
        std::string simStr = r.simPrefA ? "A>B" : (r.avgSurvivalA == r.avgSurvivalB ? "A=B" : "B>A");
        std::string probeStr = r.probePrefA ? "A>B" : (std::fabs(r.probeFailA - r.probeFailB) < 1e-9 ? "A=B" : "B>A");

        std::cout << std::left << std::setw(28) << r.id
                  << " " << std::setw(6) << (r.humanEvalAgree ? "agree" : "DISAG")
                  << " " << std::setw(6) << (r.humanSimAgree ? "agree" : "DISAG")
                  << " " << std::setw(6) << (r.humanProbeAgree ? "agree" : "DISAG")
                  << " " << std::setw(12) << (std::to_string(r.evalA) + "/" + std::to_string(r.evalB))
                  << " " << std::setw(14) << (std::to_string((int)r.avgSurvivalA) + "/" + std::to_string((int)r.avgSurvivalB))
                  << " " << std::setw(14) << (std::to_string((int)(r.probeFailA*1000)) + "/" + std::to_string((int)(r.probeFailB*1000)) + "‰")
                  ;
        // hint if human might be wrong: if both sim and probe disagree with human
        if (!r.humanSimAgree && !r.humanProbeAgree && r.evalSimAgree) {
            std::cout << "  human vs sim/probe";
        } else if (!r.humanEvalAgree) {
            std::cout << "  eval vs human";
        } else if (!r.evalSimAgree) {
            std::cout << "  eval vs sim";
        }
        if (verbose && !r.description.empty()) {
            std::cout << "\n      # " << r.description.substr(0, 100);
        }
        std::cout << "\n";
    }
    std::cout << std::string(110, '-') << "\n";
    std::cout << "Summary: " << totalPairs << " pairs\n";
    std::cout << "  Human vs Eval  agree " << evalAgree << "/" << totalPairs << " (" << std::fixed << std::setprecision(1) << 100.0*evalAgree/totalPairs << "%)\n";
    std::cout << "  Human vs Sim   agree " << simAgree << "/" << totalPairs << " (" << 100.0*simAgree/totalPairs << "%)\n";
    std::cout << "  Human vs Probe agree " << probeAgree << "/" << totalPairs << " (" << 100.0*probeAgree/totalPairs << "%)\n";
    std::cout << "  Eval vs Sim    agree " << evalSimAgree << "/" << totalPairs << " (" << 100.0*evalSimAgree/totalPairs << "%)\n";
    if (probeM == 0) {
        std::cout << "Probe samples per board: " << 2000 << " (starting position only)\n";
    }
    std::cout << "Sim: horizon " << horizon << " sets, trials " << trials << " per board, common RNG.\n";
    std::cout << "\nInterpretation: If Human vs Sim disagrees, intuition may be wrong or horizon too short.\n";
    std::cout << "If Eval vs Sim disagrees, eval feature weights may be narrow or missing.\n";
    std::cout << "Use --trials 1000 --horizon 5000 for tighter verification (slower).\n";

    return 0;
}
