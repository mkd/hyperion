/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef SEARCH_H
#define SEARCH_H

#include "movgen.h"
#include <atomic>
#include <cstdint>

inline constexpr int Infinity       = 32000;
inline constexpr int MateValue      = 31000;
inline constexpr int MateScore      = 30000;
inline constexpr int TBWinScore     = 28000;
inline constexpr int MaxSearchDepth = 128;
inline constexpr int MaxHistory     = 16384;

struct Limits_t {
    uint64_t starttime    = 0;
    uint64_t opt_stoptime = 0;
    uint64_t max_stoptime = 0;
    uint64_t stoptime     = 0;
    int depth             = -1;
    int time              = -1;
    int inc               = 0;
    int movetime          = -1;
    int movestogo         = 0;
    bool timeset          = false;
    std::atomic<bool> stopped{false};
    int quit              = 0;
};

extern Limits_t Limits;

void initLMR() noexcept;
int see(int move) noexcept;
bool see_ge(int move, int threshold) noexcept;
int scoreMove(int move, int tt_move) noexcept;
int qsearch(int alpha, int beta);
int negamax(int alpha, int beta, int depth, bool cut_node, int excludedMove = 0);
void search();

#endif // SEARCH_H
