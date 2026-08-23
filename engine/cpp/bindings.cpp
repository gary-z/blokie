#include "solver.h"
#include <emscripten/bind.h>
#include <algorithm>

using namespace emscripten;

// JS uses 3x 27-bit integers (a=rows 0-2, b=rows 3-5, c=rows 6-8).
// C++ uses 2x uint64_t (a=rows 0-5 [54 bits], b=rows 6-8 [27 bits]).

static BitBoard bbFromJS(uint32_t ja, uint32_t jb, uint32_t jc) {
    return BitBoard(
        (uint64_t)ja | ((uint64_t)jb << 27),
        (uint64_t)jc
    );
}

static val bbToJS(BitBoard bb) {
    val result = val::object();
    result.set("a", (uint32_t)(bb.getA() & 0x7FFFFFF));
    result.set("b", (uint32_t)((bb.getA() >> 27) & 0x7FFFFFF));
    result.set("c", (uint32_t)(bb.getB() & 0x7FFFFFF));
    return result;
}

static uint32_t evaluationToJS(uint64_t evaluation) {
    return (uint32_t)std::min(evaluation, (uint64_t)UINT32_MAX);
}

static val aiMakeMove(
    uint32_t board_a, uint32_t board_b, uint32_t board_c,
    uint32_t p0_a, uint32_t p0_b, uint32_t p0_c,
    uint32_t p1_a, uint32_t p1_b, uint32_t p1_c,
    uint32_t p2_a, uint32_t p2_b, uint32_t p2_c
) {
    const GameState game(bbFromJS(board_a, board_b, board_c));
    const PieceSet pieces(
        Piece(bbFromJS(p0_a, p0_b, p0_c)),
        Piece(bbFromJS(p1_a, p1_b, p1_c)),
        Piece(bbFromJS(p2_a, p2_b, p2_c))
    );
    const auto move = AI::makeMoveSimple(EvalWeights::getDefault(), game, pieces);

    val placements = val::array();
    for (int i = 0; i < 3; ++i) {
        placements.call<void>("push", bbToJS(move.placements[i]));
    }

    val result = val::object();
    result.set("found", move.evaluation != UINT64_MAX);
    result.set("evaluation", evaluationToJS(move.evaluation));
    result.set("placements", placements);
    return result;
}

static uint32_t evaluate(uint32_t board_a, uint32_t board_b, uint32_t board_c) {
    return evaluationToJS(GameState(bbFromJS(board_a, board_b, board_c))
        .simpleEval(EvalWeights::getDefault()));
}

EMSCRIPTEN_BINDINGS(blokie_solver) {
    function("aiMakeMove", &aiMakeMove);
    function("evaluate", &evaluate);
}
