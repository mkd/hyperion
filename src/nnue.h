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
 * @file nnue.h
 * @brief Thin, inlined facade over the incremental Stockfish NNUE accumulator.
 *
 * Hyperion's per-thread board accessors (`ply`, `sideToMove`, ...) are macros
 * defined in thread.h, which collide with identifiers inside the Stockfish
 * probe headers.  This header is the single place where those macros are
 * temporarily masked while the probe headers are parsed; every other
 * translation unit talks to the network through the `Nnue::` functions below.
 */

#ifndef NNUE_H
#define NNUE_H

#pragma push_macro("ply")
#pragma push_macro("sideToMove")
#undef ply
#undef sideToMove
#include "stockfish_probe/nnue_incremental.h"
#pragma pop_macro("sideToMove")
#pragma pop_macro("ply")

namespace Nnue {

/// Pushes `move` onto the accumulator stack (search).
inline void doMove(int move) { Stockfish::Incremental::push_state(move); }

/// Pops the most recent search move from the accumulator stack.
inline void undoMove(int move) { Stockfish::Incremental::pop_state(move); }

/// Pushes a null move onto the accumulator stack (search).
inline void doNullMove() { Stockfish::Incremental::push_null_state(); }

/// Pops a null move from the accumulator stack.
inline void undoNullMove() { Stockfish::Incremental::pop_state(0); }

/// Plays a game-history move on the root position (UCI `position ... moves`).
inline void setupMove(int move) { Stockfish::Incremental::setup_move(move); }

/// Resets the NNUE root position to `fen`.
inline void setupReset(const std::string &fen) { Stockfish::Incremental::setup_reset(fen); }

/// Evaluates the current position from the side-to-move's point of view (centipawns).
[[nodiscard]] inline int evaluate() { return Stockfish::Incremental::evaluate(); }

/// Hints that sibling moves share the current accumulator as common parent.
inline void hintCommonParent() { Stockfish::Incremental::hint_common_parent(); }

} // namespace Nnue

#endif // NNUE_H
