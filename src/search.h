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
 * @file search.h
 * @brief Search limits, score constants and the public search interface.
 */

#ifndef SEARCH_H
#define SEARCH_H

#include "movgen.h"

#include <atomic>
#include <cstdint>

inline constexpr int Infinity       = 32000; ///< Larger than any real score.
inline constexpr int MateValue      = 31000; ///< Score of "mate in 0"; mate in N plies is `MateValue - N`.
inline constexpr int MateScore      = 30000; ///< Scores beyond this magnitude are mate scores.
inline constexpr int TBWinScore     = 28000; ///< Base score of a tablebase win.
inline constexpr int MaxSearchDepth = 128;   ///< Deepest iteration of iterative deepening.
inline constexpr int MaxHistory     = 16384; ///< Saturation bound of the history tables.

inline constexpr int DefaultMultiPV = 1;     ///< Default number of PV lines at root.
inline constexpr int MinMultiPV     = 1;     ///< Minimum MultiPV option value.
inline constexpr int MaxMultiPV     = 256;   ///< Maximum MultiPV option value.

extern int MultiPV;                          ///< Number of PV lines searched at root.

/// Time and depth limits of the current `go` command (times in milliseconds).
struct Limits_t {
    uint64_t starttime    = 0;      ///< Wall-clock time the search started.
    uint64_t opt_stoptime = 0;      ///< Soft limit: do not start another iteration after this.
    uint64_t max_stoptime = 0;      ///< Hard limit: abort the search at this time.
    uint64_t stoptime     = 0;      ///< Legacy alias of the hard limit.
    int depth             = -1;     ///< `go depth`, or -1 when unset.
    int time              = -1;     ///< Remaining time of the side to move.
    int inc               = 0;      ///< Increment of the side to move.
    int movetime          = -1;     ///< `go movetime`, or -1 when unset.
    int movestogo         = 0;      ///< Moves to the next time control (0 = sudden death).
    bool timeset          = false;  ///< True when the search is time-limited.
    std::atomic<bool> stopped{false}; ///< Set to abort all threads.
    int quit              = 0;
};

extern Limits_t Limits;

/// Fills the late-move-reduction table; call once at start-up.
void initLMR() noexcept;

/// Static exchange evaluation of `move` in centipawns.
int see(int move) noexcept;

/// @return true when the static exchange value of `move` is at least `threshold`.
bool see_ge(int move, int threshold) noexcept;

/// Move-ordering score of `move` (higher is searched first).
int scoreMove(int move, int tt_move) noexcept;

/// Quiescence search (see search.cpp).
int qsearch(int alpha, int beta);

/// Alpha-beta search of the current thread's position (see search.cpp).
int negamax(int alpha, int beta, int depth, bool cut_node, int excludedMove = 0);

/// Result of probing Syzygy tablebases at the root position.
struct RootTbResult {
    int move = 0;   ///< Encoded legal move.
    int score = 0;  ///< Score in centipawns or mate format.
    int wdl = 0;    ///< TB_LOSS, TB_BLESSED_LOSS, TB_DRAW, TB_CURSED_WIN, TB_WIN.
    int dtz = 0;    ///< Distance to zero.
};

/// Attempts to probe Syzygy tablebases at the root position using DTZ tables.
/// Populates `best_res` with the optimal move, score, WDL, and DTZ.
/// If `all_results` is provided, populates and sorts all legal root moves.
/// @return number of legal moves probed (0 if probe failed or unavailable).
int root_tb_probe(RootTbResult &best_res, RootTbResult *all_results = nullptr, int max_results = 0);

/// Starts a search on all threads using the current `Limits`.
void search();

#endif // SEARCH_H
