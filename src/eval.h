/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef EVAL_H
#define EVAL_H

#include "position.h"
#include "thread.h"
#include <algorithm>

inline constexpr int CorrHistGrain = 256;
inline constexpr int CorrHistMax   = 16384; // Max +/- 64 cp per table

void initEval(const char* argv0 = nullptr);
int evaluate() noexcept;

static inline int adjustEvalWithCorrHist(int raw_eval) noexcept {
    const int stm = sideToMove;
    const int pawn_corr = pawn_corrhist[stm][pawn_key & CorrHistMask];
    const int w_np_corr = nonpawn_corrhist[stm][White][non_pawn_key[White] & CorrHistMask];
    const int b_np_corr = nonpawn_corrhist[stm][Black][non_pawn_key[Black] & CorrHistMask];
    const int total_corr = (pawn_corr + (w_np_corr + b_np_corr) / 2) / CorrHistGrain;
    return std::clamp(raw_eval + total_corr, -29000, 29000);
}

static inline void updateSingleCorrHist(int &entry, int scaled_diff, int weight) noexcept {
    const int updated = (entry * (256 - weight) + scaled_diff * weight) / 256;
    entry = std::clamp(updated, -CorrHistMax, CorrHistMax);
}

static inline void updateCorrHist(int diff, int depth) noexcept {
    const int stm = sideToMove;
    const int weight = std::min(32, depth * 2 + 2);
    const int scaled_diff = std::clamp(diff * CorrHistGrain, -CorrHistMax, CorrHistMax);

    updateSingleCorrHist(pawn_corrhist[stm][pawn_key & CorrHistMask], scaled_diff, weight);
    updateSingleCorrHist(nonpawn_corrhist[stm][White][non_pawn_key[White] & CorrHistMask], scaled_diff, weight);
    updateSingleCorrHist(nonpawn_corrhist[stm][Black][non_pawn_key[Black] & CorrHistMask], scaled_diff, weight);
}

#endif // EVAL_H
