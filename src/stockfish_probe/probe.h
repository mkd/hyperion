/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>

  Hyperion is a successor of Gargantua, which is derived from Chess0 and
  inspired by Code Monkey King's bbc-1.4.

  This file is part of Hyperion.  It builds on the Stockfish NNUE code in this
  directory (Copyright (C) the Stockfish developers, GPL-3.0-or-later).

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
 * @file probe.h
 * @brief Stateless NNUE evaluation entry points (network loading and one-shot evaluation).
 */

#ifndef STOCKFISH_PROBE_H
#define STOCKFISH_PROBE_H

namespace Stockfish {
namespace Probe {

/// Loads the big and small NNUE networks from the given file paths.
void init(const char *bigNetFile, const char *smallNetFile);

/// Evaluates the position given as a FEN string (side-to-move relative, centipawns).
int eval(const char *fen);

/// Evaluates a position given as a 64-entry piece board.
int eval(const int pieceBoard[], bool side, int rule50);

/// Evaluates a position given as parallel piece/square lists.
int eval(const int pieces[], const int squares[], int pieceAmount, bool side, int rule50);

} // namespace Probe
} // namespace Stockfish

#endif // STOCKFISH_PROBE_H
