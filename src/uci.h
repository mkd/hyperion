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
 * @file uci.h
 * @brief UCI protocol front-end: engine identity, command parsing, perft and bench.
 */

#ifndef UCI_H
#define UCI_H

#include <chrono>
#include <cstdint>
#include <string>

#define EngineName    "Hyperion"
#define EngineVersion "1.0"
#define EngineAuthor  "Claudio M. Camacho"

inline constexpr uint32_t DefaultHashSize = 64;    ///< Default transposition table size (MiB).
inline constexpr uint32_t MinHashSize     = 1;     ///< Smallest allowed `Hash` (MiB).
inline constexpr uint32_t MaxHashSize     = 65536; ///< Largest allowed `Hash` (MiB).

extern uint64_t perft_nodes; ///< Leaf counter of the running perft.

/// Counts the leaf nodes of the legal move tree below the current position.
void perft_driver(int depth);

/// Runs a divide-style perft to `depth`, printing per-move counts, time and NPS.
void perft_test(int depth);

/// Searches a fixed suite of positions to `depth` and prints total nodes/time/NPS.
void bench(int depth = 13);

/// Converts a UCI move string (`e2e4`, `e7e8q`) into a legal move of the current position (0 if illegal).
int parseMove(const std::string &move_str);

/// Handles `position [startpos | fen <fen>] [moves ...]`.
void parsePosition(const std::string &command);

/// Handles `go ...`: sets limits and time budget, then starts the search.
void parseGo(const std::string &line);

/// Main UCI loop: reads commands from standard input until `quit` or EOF.
void uci();

/// @return milliseconds on the steady clock.
[[nodiscard]] inline uint64_t getTimeMs() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

#endif // UCI_H
