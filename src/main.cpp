/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#include "bitboard.h"
#include "crash_recovery.h"
#include "eval.h"
#include "position.h"
#include "search.h"
#include "tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    Threads.set(1);
    initBitboards();
    initRandomKeys();
    resetBoard();
    initEval(argv[0]);
    initLMR();
    TT::init(DefaultHashSize);
    setPosition(FenPosStartpos);

    if (argc > 1 && std::string(argv[1]) == "bench") {
        int depth = (argc > 2) ? std::atoi(argv[2]) : 13;
        bench(depth);
        Threads.clear();
        tb_free();
        return 0;
    }

    uci();

    Threads.clear();
    tb_free();

    return 0;
}
