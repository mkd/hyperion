/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>

  Hyperion is a successor of Gargantua, which is derived from Chess0 and
  inspired by Code Monkey King's bbc-1.4.

  This file is part of Hyperion.

  Hyperion is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Hyperion is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @file eval.h
 * @brief Static evaluation interface and pawn / non-pawn correction history.
 *
 * The raw NNUE score is corrected by learned residuals indexed by the pawn
 * structure hash and by each side's non-pawn-piece hash.  The tables live in
 * the per-thread state (see thread.h) and are scaled by `CorrHistGrain`.
 */

#ifndef EVAL_H
#define EVAL_H

#include "position.h"
#include "thread.h"

#include <algorithm>

inline constexpr int CorrHistGrain = 256;   ///< Fixed-point scale of stored corrections.
inline constexpr int CorrHistMax   = 16384; ///< Table clamp (+/- 64 cp per table).

/// Loads the dual NNUE networks, searching next to the executable if needed.
void initEval(const char* argv0 = nullptr);

/// @return the raw NNUE evaluation (centipawns, side-to-move relative).
int evaluate() noexcept;

/// @return `raw_eval` corrected by correction histories and damped by 50-move rule (clamped to +/-29000).
[[nodiscard]] inline int adjustEvalWithCorrHist(int raw_eval) noexcept {
    const int stm = sideToMove;
    const int pawn_corr = pawn_corrhist[stm][pawn_key & CorrHistMask];
    const int w_np_corr = nonpawn_corrhist[stm][White][non_pawn_key[White] & CorrHistMask];
    const int b_np_corr = nonpawn_corrhist[stm][Black][non_pawn_key[Black] & CorrHistMask];
    const int total_corr = (pawn_corr + (w_np_corr + b_np_corr) / 2) / CorrHistGrain;
    int eval = std::clamp(raw_eval + total_corr, -29000, 29000);
    if (fifty >= 60) {
        if (fifty >= 100)
            return 0;
        eval = (eval * (100 - fifty)) / 40;
    }
    return eval;
}

/// Exponential-moving-average update of one table entry with the given `weight` (out of 256).
inline void updateSingleCorrHist(int &entry, int scaled_diff, int weight) noexcept {
    const int updated = (entry * (256 - weight) + scaled_diff * weight) / 256;
    entry = std::clamp(updated, -CorrHistMax, CorrHistMax);
}

/**
 * Learns from a search result: moves all three correction entries of the
 * current position towards `diff` (search score minus static eval), with a
 * weight growing with the search `depth`.
 */
inline void updateCorrHist(int diff, int depth) noexcept {
    const int stm = sideToMove;
    const int weight = std::min(32, depth * 2 + 2);
    const int scaled_diff = std::clamp(diff * CorrHistGrain, -CorrHistMax, CorrHistMax);

    updateSingleCorrHist(pawn_corrhist[stm][pawn_key & CorrHistMask], scaled_diff, weight);
    updateSingleCorrHist(nonpawn_corrhist[stm][White][non_pawn_key[White] & CorrHistMask], scaled_diff, weight);
    updateSingleCorrHist(nonpawn_corrhist[stm][Black][non_pawn_key[Black] & CorrHistMask], scaled_diff, weight);
}

#endif // EVAL_H
