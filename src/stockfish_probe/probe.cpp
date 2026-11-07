#include "probe.h"
#include "attacks.h"
#include "bitboard.h"
#include "position.h"
#include "evaluate.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include <iostream>

namespace Stockfish {

Eval::NNUE::Network global_network;

namespace Probe {

void init(const char *netFile) {
    Attacks::init();
    Position::init();

    Eval::NNUE::EvalFile evalFile;
    const std::string path = (netFile && *netFile) ? netFile : EvalFileDefaultName;
    global_network.load("", path, evalFile);
    if (!evalFile.current.has_value()) {
        std::cerr << "info string Warning: failed to load NNUE network " << path << "\n";
    }
}

int eval(const char *fen) {
    Position pos;
    StateInfo si;
    pos.set(fen, false, &si);
    Eval::NNUE::AccumulatorStack accumulators;
    accumulators.reset();
    Eval::NNUE::AccumulatorCaches caches(global_network);
    return Eval::evaluate(global_network, pos, accumulators, caches, 0);
}

} // namespace Probe
} // namespace Stockfish
