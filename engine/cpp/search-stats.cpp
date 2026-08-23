// Reports repeated work and pruning opportunities in the move search.

#include "solver.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct NodeKey {
    uint64_t board_a;
    uint64_t board_b;
    uint64_t first;
    uint64_t second;

    bool operator==(const NodeKey& other) const {
        return board_a == other.board_a && board_b == other.board_b &&
            first == other.first && second == other.second;
    }
};

struct NodeKeyHash {
    size_t operator()(const NodeKey& key) const {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (const uint64_t word :
            {key.board_a, key.board_b, key.first, key.second}) {
            hash = (hash ^ word) * 0x100000001b3ULL;
            hash ^= hash >> 29;
        }
        return static_cast<size_t>(hash);
    }
};

using NodeSet = std::unordered_set<NodeKey, NodeKeyHash>;
using LeafMap = std::unordered_map<NodeKey, uint64_t, NodeKeyHash>;

struct MoveStats {
    uint64_t orderings = 0;
    uint64_t evaluations = 0;
    uint64_t repeated_evaluations = 0;
    uint64_t repeats_across_orderings = 0;
    uint64_t repeats_within_ordering = 0;
    uint64_t evaluations_under_repeated_level2 = 0;
    uint64_t evaluations_under_repeated_level1 = 0;
    uint64_t level2_nodes = 0;
    uint64_t repeated_level2_nodes = 0;
    uint64_t level1_nodes = 0;
    uint64_t repeated_level1_nodes = 0;
    uint64_t leaves_skipped_by_suffix_swap = 0;
    uint64_t leaves_skipped_by_prefix_swap = 0;
    uint64_t leaves_skipped_by_both = 0;
    uint64_t leaves_by_clear_pattern[8] = {0};
    uint64_t repeats_by_clear_pattern[8] = {0};

    void add(const MoveStats& other) {
        orderings += other.orderings;
        evaluations += other.evaluations;
        repeated_evaluations += other.repeated_evaluations;
        repeats_across_orderings += other.repeats_across_orderings;
        repeats_within_ordering += other.repeats_within_ordering;
        evaluations_under_repeated_level2 +=
            other.evaluations_under_repeated_level2;
        evaluations_under_repeated_level1 +=
            other.evaluations_under_repeated_level1;
        level2_nodes += other.level2_nodes;
        repeated_level2_nodes += other.repeated_level2_nodes;
        level1_nodes += other.level1_nodes;
        repeated_level1_nodes += other.repeated_level1_nodes;
        leaves_skipped_by_suffix_swap += other.leaves_skipped_by_suffix_swap;
        leaves_skipped_by_prefix_swap += other.leaves_skipped_by_prefix_swap;
        leaves_skipped_by_both += other.leaves_skipped_by_both;
        for (int i = 0; i < 8; ++i) {
            leaves_by_clear_pattern[i] += other.leaves_by_clear_pattern[i];
            repeats_by_clear_pattern[i] += other.repeats_by_clear_pattern[i];
        }
    }
};

struct Scratch {
    LeafMap leaves;
    NodeSet level2;
    NodeSet level1;
    NodeSet suffix_swap_leaves;
    NodeSet prefix_swap_leaves;
    NodeSet both_rules_leaves;

    void clear() {
        leaves.clear();
        level2.clear();
        level1.clear();
        suffix_swap_leaves.clear();
        prefix_swap_leaves.clear();
        both_rules_leaves.clear();
    }
};

MoveResult walk(GameState game, PieceSet piece_set, Scratch& scratch,
    MoveStats& stats) {
    scratch.clear();
    std::sort(piece_set.pieces, piece_set.pieces + 3);
    const int num_pieces = AI::countPieces(piece_set);
    const auto can_clear_with_2_pieces =
        AI::canClearWith2PiecesOrFewer(game, piece_set);

    MoveResult best;
    bool is_first_permutation = true;
    uint64_t ordering_index = 0;
    do {
        stats.orderings++;
        const auto p0 = piece_set.pieces[0];
        const auto p1 = piece_set.pieces[1];
        const auto p2 = piece_set.pieces[2];
        const uint64_t p1_bits = p1.getBitBoard().getA();
        const uint64_t p2_bits = p2.getBitBoard().getA();
        const bool suffix_out_of_order = p2 < p1;
        const auto states_0 = game.nextStatesClearsFirst(p0);
        const auto last_0 = states_0.end();
        for (auto it_0 = states_0.begin(); it_0 != last_0; ++it_0) {
            const auto after_p0 = *it_0;
            const bool p0_cleared = it_0.didClear();
            const auto board_0 = after_p0.getBitBoard();
            const NodeKey key_1{board_0.getA(), board_0.getB(), p1_bits,
                p2_bits};
            const bool level1_repeat = !scratch.level1.insert(key_1).second;
            stats.level1_nodes++;
            stats.repeated_level1_nodes += level1_repeat ? 1 : 0;
            uint64_t evaluations_here = 0;

            const auto states_1 = after_p0.nextStatesClearsFirst(p1);
            const auto last_1 = states_1.end();
            for (auto it_1 = states_1.begin(); it_1 != last_1; ++it_1) {
                const auto after_p1 = *it_1;
                const bool p1_cleared = it_1.didClear();
                if (!is_first_permutation && p1 < p0 &&
                    !p0_cleared && !p1_cleared) {
                    continue;
                }
                const auto board_1 = after_p1.getBitBoard();
                bool prefix_swap_skips = false;
                if (!is_first_permutation && p1 < p0 && !p0_cleared &&
                    p1_cleared) {
                    const auto placement_1 = it_1.getPlacement();
                    const auto cleared_cells =
                        (board_0 | placement_1) - board_1;
                    prefix_swap_skips =
                        !(it_0.getPlacement() & cleared_cells);
                }
                const NodeKey key_2{board_1.getA(), board_1.getB(), p2_bits,
                    0};
                const bool level2_repeat = !scratch.level2.insert(key_2).second;
                stats.level2_nodes++;
                stats.repeated_level2_nodes += level2_repeat ? 1 : 0;
                uint64_t evaluations_under_2 = 0;

                const auto states_2 = after_p1.nextStates(p2);
                const auto last_2 = states_2.end();
                for (auto it_2 = states_2.begin(); it_2 != last_2; ++it_2) {
                    const auto after_p2 = *it_2;
                    const bool p2_cleared = it_2.didClear();
                    if (!is_first_permutation && !p0_cleared &&
                        !p1_cleared && !p2_cleared) {
                        continue;
                    }
                    stats.evaluations++;
                    evaluations_under_2++;
                    const bool suffix_swap_skips = suffix_out_of_order &&
                        !p1_cleared && !p2_cleared;
                    stats.leaves_skipped_by_suffix_swap +=
                        suffix_swap_skips ? 1 : 0;
                    stats.leaves_skipped_by_prefix_swap +=
                        prefix_swap_skips ? 1 : 0;
                    stats.leaves_skipped_by_both +=
                        (suffix_swap_skips || prefix_swap_skips) ? 1 : 0;

                    const auto board_2 = after_p2.getBitBoard();
                    const NodeKey leaf{board_2.getA(), board_2.getB(), 0, 0};
                    const int pattern = (p0_cleared ? 1 : 0) |
                        (p1_cleared ? 2 : 0) | (p2_cleared ? 4 : 0);
                    stats.leaves_by_clear_pattern[pattern]++;
                    if (!suffix_swap_skips) {
                        scratch.suffix_swap_leaves.insert(leaf);
                    }
                    if (!prefix_swap_skips) {
                        scratch.prefix_swap_leaves.insert(leaf);
                    }
                    if (!suffix_swap_skips && !prefix_swap_skips) {
                        scratch.both_rules_leaves.insert(leaf);
                    }
                    const auto [entry, inserted] =
                        scratch.leaves.emplace(leaf, ordering_index);
                    if (!inserted) {
                        stats.repeated_evaluations++;
                        stats.repeats_by_clear_pattern[pattern]++;
                        if (entry->second == ordering_index) {
                            stats.repeats_within_ordering++;
                        } else {
                            stats.repeats_across_orderings++;
                        }
                    }

                    const auto score = after_p2.simpleEvalDefault(
                        best.evaluation);
                    if (score < best.evaluation) {
                        best.evaluation = score;
                        best.state = after_p2;
                        best.placements[0] = it_0.getPlacement();
                        best.placements[1] = it_1.getPlacement();
                        best.placements[2] = it_2.getPlacement();
                    }
                }
                if (level2_repeat) {
                    stats.evaluations_under_repeated_level2 +=
                        evaluations_under_2;
                }
                evaluations_here += evaluations_under_2;
            }
            if (level1_repeat) {
                stats.evaluations_under_repeated_level1 += evaluations_here;
            }
        }
        is_first_permutation = false;
        ordering_index++;
    } while (can_clear_with_2_pieces &&
        std::next_permutation(piece_set.pieces, piece_set.pieces + num_pieces));

    return best;
}

uint64_t countUnprunedLeaves(GameState game, PieceSet piece_set) {
    std::sort(piece_set.pieces, piece_set.pieces + 3);
    const int num_pieces = AI::countPieces(piece_set);
    uint64_t leaves = 0;
    do {
        for (const auto after_p0 : game.nextStates(piece_set.pieces[0])) {
            for (const auto after_p1 :
                after_p0.nextStates(piece_set.pieces[1])) {
                for (const auto after_p2 :
                    after_p1.nextStates(piece_set.pieces[2])) {
                    (void)after_p2;
                    leaves++;
                }
            }
        }
    } while (std::next_permutation(piece_set.pieces,
        piece_set.pieces + num_pieces));
    return leaves;
}

struct RunStats {
    MoveStats totals;
    uint64_t moves = 0;
    uint64_t games_ended = 0;
    uint64_t multi_ordering_moves = 0;
    uint64_t moves_with_repeats = 0;
    uint64_t unpruned_leaves = 0;
    uint64_t lost_boards = 0;
    uint64_t tie_broken_differently = 0;
    double seconds = 0;
};

Piece randomPiece(std::mt19937_64& rng,
    std::uniform_int_distribution<int>& piece_dist) {
    return Piece::byIndex(piece_dist(rng));
}

RunStats playMoves(uint64_t seed, uint64_t max_moves, bool unpruned) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> piece_dist(0, Piece::NUM_PIECES - 1);
    GameState game(BitBoard::empty());
    Scratch scratch;
    RunStats run;
    const auto start = std::chrono::steady_clock::now();
    while (run.moves < max_moves) {
        const PieceSet dealt(randomPiece(rng, piece_dist),
            randomPiece(rng, piece_dist), randomPiece(rng, piece_dist));
        MoveStats move;
        const auto instrumented = walk(game, dealt, scratch, move);
        const auto real = AI::makeMoveSimpleDefault(game, dealt);
        if (instrumented.evaluation != real.evaluation) {
            std::fprintf(stderr,
                "instrumented search disagrees with AI::makeMoveSimpleDefault "
                "at move %llu\n",
                static_cast<unsigned long long>(run.moves));
            std::exit(1);
        }
        if (!(instrumented.state.getBitBoard() ==
            real.state.getBitBoard())) {
            run.tie_broken_differently++;
        }
        const std::pair<const char*, const NodeSet*> variants[] = {
            {"suffix swap", &scratch.suffix_swap_leaves},
            {"prefix swap", &scratch.prefix_swap_leaves},
            {"both rules", &scratch.both_rules_leaves},
        };
        for (const auto& [name, boards] : variants) {
            if (boards->size() != scratch.leaves.size()) {
                std::fprintf(stderr,
                    "%s lost %llu of %llu boards at move %llu\n", name,
                    static_cast<unsigned long long>(scratch.leaves.size() -
                        boards->size()),
                    static_cast<unsigned long long>(scratch.leaves.size()),
                    static_cast<unsigned long long>(run.moves));
                run.lost_boards++;
            }
        }
        if (unpruned) {
            run.unpruned_leaves += countUnprunedLeaves(game, dealt);
        }
        run.totals.add(move);
        run.moves++;
        run.multi_ordering_moves += move.orderings > 1 ? 1 : 0;
        run.moves_with_repeats += move.repeated_evaluations > 0 ? 1 : 0;
        game = real.state;
        if (game.isOver()) {
            run.games_ended++;
            game = GameState(BitBoard::empty());
        }
    }
    run.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    return run;
}

double percent(uint64_t part, uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) /
        static_cast<double>(whole);
}

double per(uint64_t part, uint64_t whole) {
    return whole == 0 ? 0.0 : static_cast<double>(part) /
        static_cast<double>(whole);
}

void report(const RunStats& run, bool unpruned) {
    const auto& t = run.totals;
    const uint64_t distinct = t.evaluations - t.repeated_evaluations;
    std::printf("moves                        %llu\n",
        static_cast<unsigned long long>(run.moves));
    std::printf("games ended                  %llu\n",
        static_cast<unsigned long long>(run.games_ended));
    std::printf("seconds                      %.1f\n", run.seconds);
    std::printf("\n");
    std::printf("evaluations/move             %.1f\n",
        per(t.evaluations, run.moves));
    std::printf("distinct boards/move         %.1f\n",
        per(distinct, run.moves));
    std::printf("repeated evaluations/move    %.1f (%.1f%% of evaluations)\n",
        per(t.repeated_evaluations, run.moves),
        percent(t.repeated_evaluations, t.evaluations));
    std::printf("  first seen, other ordering %.1f\n",
        per(t.repeats_across_orderings, run.moves));
    std::printf("  first seen, same ordering  %.1f\n",
        per(t.repeats_within_ordering, run.moves));
    std::printf("evaluations per distinct     %.3f\n",
        per(t.evaluations, distinct));
    std::printf("\n");
    std::printf("moves walking >1 ordering    %.1f%%\n",
        percent(run.multi_ordering_moves, run.moves));
    std::printf("moves with any repeat        %.1f%%\n",
        percent(run.moves_with_repeats, run.moves));
    std::printf("orderings/move               %.3f\n",
        per(t.orderings, run.moves));
    if (unpruned) {
        std::printf("unpruned leaves/move         %.1f (prunings remove "
            "%.1f%%)\n", per(run.unpruned_leaves, run.moves),
            percent(run.unpruned_leaves - t.evaluations, run.unpruned_leaves));
    }
    std::printf("\n");
    std::printf("what a dedup would save, each measured against the same "
        "baseline walk\n");
    std::printf("  leaf boards                %.1f/move (%.1f%%)\n",
        per(t.repeated_evaluations, run.moves),
        percent(t.repeated_evaluations, t.evaluations));
    std::printf("  (board, last piece)        %.1f/move (%.1f%%), %.1f%% of "
        "%.1f such nodes repeat\n",
        per(t.evaluations_under_repeated_level2, run.moves),
        percent(t.evaluations_under_repeated_level2, t.evaluations),
        percent(t.repeated_level2_nodes, t.level2_nodes),
        per(t.level2_nodes, run.moves));
    std::printf("  (board, last two pieces)   %.1f/move (%.1f%%), %.1f%% of "
        "%.1f such nodes repeat\n",
        per(t.evaluations_under_repeated_level1, run.moves),
        percent(t.evaluations_under_repeated_level1, t.evaluations),
        percent(t.repeated_level1_nodes, t.level1_nodes),
        per(t.level1_nodes, run.moves));
    std::printf("\n");
    std::printf("candidate ordering rules, each checked every move for "
        "losing a board\n");
    std::printf("  suffix swap                %.1f/move (%.1f%%)\n",
        per(t.leaves_skipped_by_suffix_swap, run.moves),
        percent(t.leaves_skipped_by_suffix_swap, t.evaluations));
    std::printf("  prefix swap, extended      %.1f/move (%.1f%%)\n",
        per(t.leaves_skipped_by_prefix_swap, run.moves),
        percent(t.leaves_skipped_by_prefix_swap, t.evaluations));
    std::printf("  both                       %.1f/move (%.1f%%)\n",
        per(t.leaves_skipped_by_both, run.moves),
        percent(t.leaves_skipped_by_both, t.evaluations));
    std::printf("  repeats left after both    %.1f/move (%.1f%%)\n",
        per(t.repeated_evaluations - t.leaves_skipped_by_both, run.moves),
        percent(t.repeated_evaluations - t.leaves_skipped_by_both,
            t.evaluations));
    std::printf("  moves where a rule lost a board  %llu\n",
        static_cast<unsigned long long>(run.lost_boards));
    std::printf("\n");
    std::printf("against the search in this build, same score every move\n");
    std::printf("  moves settling on a different board of equal score  "
        "%llu (%.3f%%)\n",
        static_cast<unsigned long long>(run.tie_broken_differently),
        percent(run.tie_broken_differently, run.moves));
    std::printf("\n");
    std::printf("leaves by which placements cleared\n");
    std::printf("pattern   leaves          repeats     repeat rate\n");
    for (int pattern = 0; pattern < 8; ++pattern) {
        char name[4] = {
            static_cast<char>((pattern & 1) ? '1' : '.'),
            static_cast<char>((pattern & 2) ? '2' : '.'),
            static_cast<char>((pattern & 4) ? '3' : '.'),
            '\0'};
        std::printf("%-9s %-15llu %-11llu %.1f%%\n", name,
            static_cast<unsigned long long>(t.leaves_by_clear_pattern[pattern]),
            static_cast<unsigned long long>(
                t.repeats_by_clear_pattern[pattern]),
            percent(t.repeats_by_clear_pattern[pattern],
                t.leaves_by_clear_pattern[pattern]));
    }
}

} // namespace

int main(int argc, char** argv) {
    uint64_t max_moves = 20000;
    uint64_t seed = 1;
    bool unpruned = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--unpruned") {
            unpruned = true;
        } else if (arg == "--help") {
            std::printf("usage: search-stats [moves] [--seed N] "
                "[--unpruned]\n");
            return 0;
        } else {
            max_moves = std::strtoull(argv[i], nullptr, 10);
            if (max_moves == 0) {
                std::fprintf(stderr, "moves must be positive\n");
                return 1;
            }
        }
    }
    const auto run = playMoves(seed, max_moves, unpruned);
    std::printf("# search-stats: seed %llu\n\n",
        static_cast<unsigned long long>(seed));
    report(run, unpruned);
    return 0;
}
