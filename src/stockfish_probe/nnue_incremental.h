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
 * @file nnue_incremental.h
 * @brief Incremental NNUE accumulator interface used by the search.
 */

#ifndef NNUE_INCREMENTAL_H
#define NNUE_INCREMENTAL_H

#include "position.h"
#include <deque>
#include <string>

/**
 * The search plays moves on the Hyperion board and mirrors them here, on a
 * Stockfish `Position`, so the NNUE accumulators are updated incrementally
 * instead of being recomputed.  Moves are Hyperion's packed 24-bit moves
 * (see movgen.h); squares and pieces are mapped to Stockfish's encoding.
 *
 * Two stacks exist per thread: the *setup* stack (game history applied via
 * `position ... moves`) and the *search* stack (moves played inside the tree).
 */
namespace Stockfish {
namespace Incremental {

/// Resets to the start position and allocates the search stack.
void init();

/// Applies `g_move` to the position, filling `si` with the dirty-piece info.
void do_move(int g_move, StateInfo *si);

/// Applies a null move (side to move flips, no piece changes).
void do_null_move(StateInfo *si);

/// Reverts the most recent do_move()/do_null_move().
void undo_move(int g_move);

/// Resets the root position to `fen` and clears the setup stack.
void setup_reset(const std::string &fen);

/// Plays a game-history move on the root position.
void setup_move(int g_move);

/// Takes back a game-history move.
void setup_undo(int g_move);

/// Pushes `g_move` on the search stack.
void push_state(int g_move);

/// Pushes a null move on the search stack.
void push_null_state();

/// Pops the most recent search move (or null move).
void pop_state(int g_move);

/// Evaluates the current position with the NNUE (side-to-move relative, centipawns).
int evaluate();

/// Lets sibling moves share the current accumulator as their common parent.
void hint_common_parent();

/// @return the thread-local NNUE position.
const Position &get_global_pos();

/// @return the thread-local stack of game-history states.
const std::deque<StateInfo> &get_setup_states();

/// Copies the main thread's position and setup stack into the calling helper thread.
void sync_from_main_thread(const Position &main_pos, const std::deque<StateInfo> &main_setup);

} // namespace Incremental
} // namespace Stockfish

#endif // NNUE_INCREMENTAL_H
