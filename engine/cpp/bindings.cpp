#include "solver.h"
#include <emscripten/bind.h>
#include <algorithm>
#include <vector>

using namespace emscripten;

// === Bitboard conversion ===
// JS uses 3x 27-bit integers (a=rows 0-2, b=rows 3-5, c=rows 6-8).
// C++ uses 2x uint64_t (a=rows 0-5 [54 bits], b=rows 6-8 [27 bits]).

static BitBoard bbFromJS(uint32_t ja, uint32_t jb, uint32_t jc) {
    return BitBoard(
        (uint64_t)ja | ((uint64_t)jb << 27),
        (uint64_t)jc
    );
}

static void bbToJS(BitBoard bb, uint32_t &oa, uint32_t &ob, uint32_t &oc) {
    oa = (uint32_t)(bb.getA() & 0x7FFFFFF);
    ob = (uint32_t)((bb.getA() >> 27) & 0x7FFFFFF);
    oc = (uint32_t)(bb.getB() & 0x7FFFFFF);
}

// === Placed state: GameState + the placement bitboard ===
struct PlacedState {
    GameState state;
    BitBoard placement;
};

static std::vector<PlacedState> getNextStatesWithPlacements(
    GameState game, Piece piece, bool clears_first
) {
    const auto expected_count = game.getBitBoard().count() + piece.getBitBoard().count();
    std::vector<PlacedState> clears, no_clears;

    auto gen = game.nextStates(piece);
    for (auto it = gen.begin(); it != gen.end(); ++it) {
        PlacedState ps = { *it, it.getPlacement() };
        if (clears_first && ps.state.getBitBoard().count() < expected_count) {
            clears.push_back(ps);
        } else {
            no_clears.push_back(ps);
        }
    }

    if (clears_first) {
        clears.insert(clears.end(), no_clears.begin(), no_clears.end());
        return clears;
    }
    return no_clears;
}

// === Scoring logic (ported from blokie.js) ===

static int getComboMagnitude(BitBoard before_placement, BitBoard placement) {
    BitBoard mid = before_placement | placement;
    int result = 0;
    for (unsigned i = 0; i < 9; ++i) {
        if ((mid & BitBoard::row(i)) == BitBoard::row(i)) result++;
        if ((mid & BitBoard::column(i)) == BitBoard::column(i)) result++;
    }
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c) {
            if ((mid & BitBoard::cube(r, c)) == BitBoard::cube(r, c)) result++;
        }
    }
    return result;
}

static int getMoveScore(bool previous_was_clear, BitBoard prev, BitBoard placement, BitBoard after) {
    // 1 point for each block placed that was not cleared.
    int result = (after - prev).count();
    int combo = getComboMagnitude(prev, placement);
    if (combo == 0) return result;

    // Streak
    if (previous_was_clear) result += 9;

    if (combo <= 2)      result += 18 * combo;
    else if (combo <= 4) result += 36 * combo;
    else if (combo <= 7) result += 54 * combo;
    else                 result += 72 * combo;

    return result;
}

// === Main AI function ===
// This is the full port of ai_make_move from blokie.js.
// It finds the best board state via evaluation, then optimizes the piece
// ordering to maximize score.
//
// Returns a JS object:
// {
//   evaluation: number,
//   new_game_states: [
//     { board: {a,b,c}, previous_piece_placement: {a,b,c}, piece_index: number,
//       previous_move_was_clear: bool, score: number },
//     ...  (3 entries)
//   ]
// }

static val aiMakeMove(
    // Game state
    uint32_t board_a, uint32_t board_b, uint32_t board_c,
    int game_score,
    bool previous_move_was_clear,
    // 3 pieces (already left-top justified)
    uint32_t p0_a, uint32_t p0_b, uint32_t p0_c,
    uint32_t p1_a, uint32_t p1_b, uint32_t p1_c,
    uint32_t p2_a, uint32_t p2_b, uint32_t p2_c
) {
    BitBoard boardBB = bbFromJS(board_a, board_b, board_c);
    GameState game(boardBB);
    auto weights = EvalWeights::getDefault();

    Piece pieces[3] = {
        Piece(bbFromJS(p0_a, p0_b, p0_c)),
        Piece(bbFromJS(p1_a, p1_b, p1_c)),
        Piece(bbFromJS(p2_a, p2_b, p2_c)),
    };

    // === Phase 1: Find best end state (ai_make_move_impl equivalent) ===
    // Sort pieces for canonical ordering.
    Piece sorted_pieces[3] = { pieces[0], pieces[1], pieces[2] };
    std::sort(sorted_pieces, sorted_pieces + 3);

    PieceSet ps(sorted_pieces[0], sorted_pieces[1], sorted_pieces[2]);
    bool can_clear = AI::canClearWith2PiecesOrFewer(game, ps);

    uint64_t bestEval = UINT64_MAX;

    // Track best path: 3 placements and 3 resulting boards.
    BitBoard bestPlacements[3] = { BitBoard::empty(), BitBoard::empty(), BitBoard::empty() };
    BitBoard bestBoards[3] = { BitBoard::full(), BitBoard::full(), BitBoard::full() };

    bool is_first_permutation = true;
    do {
        const auto p0 = ps.pieces[0];
        const auto p1 = ps.pieces[1];
        const auto p2 = ps.pieces[2];

        for (const auto &s0 : getNextStatesWithPlacements(game, p0, true)) {
            for (const auto &s1 : getNextStatesWithPlacements(s0.state, p1, true)) {
                const int after_p1_max_count = game.getBitBoard().count() +
                    p0.getBitBoard().count() + p1.getBitBoard().count();
                if (p1 < p0 && s1.state.getBitBoard().count() == after_p1_max_count) {
                    continue;
                }
                for (const auto &s2 : getNextStatesWithPlacements(s1.state, p2, false)) {
                    if (!is_first_permutation &&
                        s2.state.getBitBoard().count() == after_p1_max_count +
                        p2.getBitBoard().count()) {
                        continue;
                    }
                    const auto score = s2.state.simpleEval(weights, bestEval);
                    if (score < bestEval) {
                        bestEval = score;
                        bestPlacements[0] = s0.placement;
                        bestPlacements[1] = s1.placement;
                        bestPlacements[2] = s2.placement;
                        bestBoards[0] = s0.state.getBitBoard();
                        bestBoards[1] = s1.state.getBitBoard();
                        bestBoards[2] = s2.state.getBitBoard();
                    }
                }
            }
        }
        is_first_permutation = false;
    } while (can_clear &&
        std::next_permutation(ps.pieces, ps.pieces + 3));

    // === Phase 2: Optimize score by trying all permutations of the original pieces ===
    // We look for paths that reach the same end board as phase 1, maximizing the game score.

    BitBoard targetBoard = bestBoards[2];
    int bestGameScore = -1;

    struct MoveState {
        BitBoard board = BitBoard::full();
        BitBoard placement = BitBoard::empty();
        int piece_index = 0;
        bool was_clear = false;
        int score = 0;
    };
    MoveState bestMoves[3];

    // All 6 permutations of original piece indices.
    int perm[3] = {0, 1, 2};
    std::sort(perm, perm + 3);

    // Create left-top justified versions for placement matching.
    // pieces[] are already justified (caller handles this).

    do {
        const auto &pp0 = pieces[perm[0]];
        const auto &pp1 = pieces[perm[1]];
        const auto &pp2 = pieces[perm[2]];

        for (const auto &s0 : getNextStatesWithPlacements(game, pp0, false)) {
            // Filter: only consider placements that appeared in the base result.
            bool found0 = (s0.placement == bestPlacements[0]) ||
                          (s0.placement == bestPlacements[1]) ||
                          (s0.placement == bestPlacements[2]);
            if (!found0) continue;

            for (const auto &s1 : getNextStatesWithPlacements(s0.state, pp1, false)) {
                bool found1 = (s1.placement == bestPlacements[0]) ||
                              (s1.placement == bestPlacements[1]) ||
                              (s1.placement == bestPlacements[2]);
                if (!found1) continue;

                for (const auto &s2 : getNextStatesWithPlacements(s1.state, pp2, false)) {
                    bool found2 = (s2.placement == bestPlacements[0]) ||
                                  (s2.placement == bestPlacements[1]) ||
                                  (s2.placement == bestPlacements[2]);
                    if (!found2) continue;

                    if (!(s2.state.getBitBoard() == targetBoard)) continue;

                    bool p0_was_clear = s0.state.getBitBoard().count() <
                        boardBB.count() + pp0.getBitBoard().count();
                    bool p1_was_clear = s1.state.getBitBoard().count() <
                        s0.state.getBitBoard().count() + pp1.getBitBoard().count();
                    bool p2_was_clear = s2.state.getBitBoard().count() <
                        s1.state.getBitBoard().count() + pp2.getBitBoard().count();

                    int score0 = game_score + getMoveScore(previous_move_was_clear,
                        boardBB, s0.placement, s0.state.getBitBoard());
                    int score1 = score0 + getMoveScore(p0_was_clear,
                        s0.state.getBitBoard(), s1.placement, s1.state.getBitBoard());
                    int score2 = score1 + getMoveScore(p1_was_clear,
                        s1.state.getBitBoard(), s2.placement, s2.state.getBitBoard());

                    if (score2 < bestGameScore) continue;
                    if (score2 == bestGameScore && !p2_was_clear) continue;

                    bestGameScore = score2;
                    bestMoves[0] = { s0.state.getBitBoard(), s0.placement, perm[0], p0_was_clear, score0 };
                    bestMoves[1] = { s1.state.getBitBoard(), s1.placement, perm[1], p1_was_clear, score1 };
                    bestMoves[2] = { s2.state.getBitBoard(), s2.placement, perm[2], p2_was_clear, score2 };
                }
            }
        }
    } while (std::next_permutation(perm, perm + 3));

    // === Build result ===
    val result = val::object();
    result.set("evaluation", (uint32_t)std::min(bestEval, (uint64_t)UINT32_MAX));

    val states = val::array();
    for (int i = 0; i < 3; i++) {
        val state = val::object();
        uint32_t ba, bb, bc, pa, pb, pc;
        bbToJS(bestMoves[i].board, ba, bb, bc);
        bbToJS(bestMoves[i].placement, pa, pb, pc);

        val board_obj = val::object();
        board_obj.set("a", ba);
        board_obj.set("b", bb);
        board_obj.set("c", bc);
        state.set("board", board_obj);

        val placement_obj = val::object();
        placement_obj.set("a", pa);
        placement_obj.set("b", pb);
        placement_obj.set("c", pc);
        state.set("previous_piece_placement", placement_obj);

        state.set("piece_index", bestMoves[i].piece_index);
        state.set("previous_move_was_clear", bestMoves[i].was_clear);
        state.set("score", bestMoves[i].score);

        states.call<void>("push", state);
    }
    result.set("new_game_states", states);

    return result;
}

EMSCRIPTEN_BINDINGS(blokie_solver) {
    function("aiMakeMove", &aiMakeMove);
}
