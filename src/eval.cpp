/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#include "eval.h"
#include "stockfish_probe/probe.h"
#include <filesystem>
#include <string>

#ifdef USE_NEW_NNUE
#pragma push_macro("ply")
#pragma push_macro("sideToMove")
#undef ply
#undef sideToMove
#include "stockfish_probe/nnue_incremental.h"
#pragma pop_macro("sideToMove")
#pragma pop_macro("ply")
#endif

void initEval(const char* argv0) {
    std::string big_net = "nn-b1a57edbea57.nnue";
    std::string small_net = "nn-baff1ede1f90.nnue";

    namespace fs = std::filesystem;
    if (!fs::exists(big_net) && argv0) {
        fs::path exe_dir = fs::path(argv0).parent_path();
        if (fs::exists(exe_dir / big_net)) {
            big_net = (exe_dir / big_net).string();
            small_net = (exe_dir / small_net).string();
        } else if (fs::exists(exe_dir / "src" / big_net)) {
            big_net = (exe_dir / "src" / big_net).string();
            small_net = (exe_dir / "src" / small_net).string();
        }
    }

    Stockfish::Probe::init(big_net.c_str(), small_net.c_str());
}

int evaluate() noexcept {
#ifdef USE_NEW_NNUE
    return Stockfish::Incremental::evaluate();
#else
    return 0;
#endif
}
