#include "golden_common.h"
#include "game.h"
#include "eval.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " [--file PATH] [--verbose] [--strict] [--json] [--help]\n"
              << "\n"
              << "Checks the golden board-pair file. Each pair has board A (preferred)\n"
              << "and board B. The eval should score A < B (lower is better).\n"
              << "\n"
              << "Options:\n"
              << "  --file PATH   Path to golden file (json or txt, default: auto-detected)\n"
              << "  --verbose     Show evals and board diffs for every pair\n"
              << "  --strict      Exit 1 if any pair fails (default: exit 0 unless parse error)\n"
              << "  --json        Emit JSON summary to stdout\n"
              << "  --help        Show this help\n";
}

struct Result {
    golden::GoldenPair pair;
    BitBoard boardA = BitBoard::empty();
    BitBoard boardB = BitBoard::empty();
    uint64_t evalA;
    uint64_t evalB;
    bool pass;
};

int main(int argc, char **argv) {
    std::string file = "";
    bool verbose = false;
    bool strict = false;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            file = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--json") {
            json = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        } else {
            // Positional file
            if (file.empty()) file = arg;
            else {
                std::cerr << "Unexpected argument: " << arg << "\n";
                return 2;
            }
        }
    }

    if (file.empty()) {
        file = golden::findDefaultGoldenFile();
    }

    std::string err;
    auto pairs = golden::parseGoldenFile(file, err);
    if (!err.empty()) {
        std::cerr << "Parse error: " << err << "\n";
        return 2;
    }
    if (pairs.empty()) {
        std::cerr << "No pairs found in " << file << "\n";
        return 2;
    }

    std::vector<Result> results;
    results.reserve(pairs.size());
    int passed = 0;

    for (auto &p : pairs) {
        BitBoard a = golden::boardFromLines(p.boardA);
        BitBoard b = golden::boardFromLines(p.boardB);
        GameState ga(a);
        GameState gb(b);
        uint64_t ea = ga.simpleEvalDefault();
        uint64_t eb = gb.simpleEvalDefault();
        bool pass = ea < eb;
        if (pass) ++passed;
        results.push_back({p, a, b, ea, eb, pass});
    }

    if (json) {
        std::cout << "{\n";
        std::cout << "  \"file\": \"" << file << "\",\n";
        std::cout << "  \"total\": " << results.size() << ",\n";
        std::cout << "  \"passed\": " << passed << ",\n";
        std::cout << "  \"pairs\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            auto &r = results[i];
            std::cout << "    {\"id\":\"" << r.pair.id << "\",\"evalA\":" << r.evalA
                      << ",\"evalB\":" << r.evalB << ",\"delta\":" << (int64_t)r.evalB - (int64_t)r.evalA
                      << ",\"pass\":" << (r.pass ? "true" : "false") << "}";
            if (i + 1 < results.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n}\n";
    } else {
        std::cout << "Golden eval check: " << file << "\n";
        std::cout << "------------------------------------------------------------\n";
        for (auto &r : results) {
            std::string status = r.pass ? "PASS" : "FAIL";
            int64_t delta = (int64_t)r.evalB - (int64_t)r.evalA;
            std::cout << std::left << std::setw(32) << r.pair.id << " " << status
                      << "  A=" << std::setw(8) << r.evalA
                      << " B=" << std::setw(8) << r.evalB
                      << " delta=" << std::setw(8) << delta;
            if (!r.pair.description.empty() && verbose) {
                std::cout << "  # " << r.pair.description.substr(0, 60);
            }
            std::cout << "\n";
            if (verbose) {
                // Show boards side by side if failed or verbose
                if (!r.pass || verbose) {
                    std::cout << "  A (" << r.evalA << "):\n";
                    for (int row = 0; row < 9; ++row) {
                        std::cout << "    " << r.pair.boardA[row] << "\n";
                    }
                    std::cout << "  B (" << r.evalB << "):\n";
                    for (int row = 0; row < 9; ++row) {
                        std::cout << "    " << r.pair.boardB[row] << "\n";
                    }
                    if (!r.pair.description.empty()) {
                        std::cout << "  Desc: " << r.pair.description << "\n";
                    }
                    std::cout << "\n";
                }
            }
        }
        std::cout << "------------------------------------------------------------\n";
        double pct = 100.0 * passed / results.size();
        std::cout << "Summary: " << passed << "/" << results.size() << " passed (" << std::fixed << std::setprecision(1) << pct << "%)";
        if (passed == (int)results.size()) std::cout << "  all PASS";
        else std::cout << "  " << (results.size() - passed) << " FAIL";
        std::cout << "\n";
        if (!strict && passed != (int)results.size()) {
            std::cout << "Note: some pairs do not pass current eval (expected per spec). Use --strict to fail CI.\n";
        }
    }

    if (strict && passed != (int)results.size()) return 1;
    return 0;
}
