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
 * @file nnue_incremental.cpp
 * @brief Incremental NNUE accumulator driver: mirrors Hyperion moves on a Stockfish position.
 */

#include "nnue_incremental.h"
#include "evaluate.h"
#include "nnue/evaluate_nnue.h"
#include "probe.h"
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

namespace {

// Decoding of Hyperion's packed move format (kept in sync with movgen.h, which
// cannot be included here because of identifier clashes with Stockfish headers).
constexpr int getMoveSource(int move)  { return move & 0x3f; }
constexpr int getMoveTarget(int move)  { return (move & 0xfc0) >> 6; }
constexpr int getMovePiece(int move)   { return (move & 0xf000) >> 12; }
constexpr int getPromo(int move)       { return (move & 0xf0000) >> 16; }
constexpr int getEp(int move)          { return move & 0x400000; }
constexpr int getCastle(int move)      { return move & 0x800000; }

/// Hyperion squares count from a8 (0); Stockfish's from a1 (0): flip the rank.
constexpr int mapSquare(int sq) { return sq ^ 56; }

} // namespace

namespace Stockfish {
namespace Incremental {

/// Per-thread NNUE position, its root state, search stack and game-history stack.
thread_local Position global_pos;
thread_local StateInfo global_si;
thread_local std::vector<StateInfo> search_stack;
thread_local size_t stack_ptr = 0;
thread_local std::deque<StateInfo> setup_states;

/// Hyperion piece code -> Stockfish piece code.
static constexpr int g_to_sf_piece[12] = {
    1, 2,  3,  4,  5,  6,  // P N B R Q K -> W_PAWN..W_KING
    9, 10, 11, 12, 13, 14  // p n b r q k -> B_PAWN..B_KING
};

/// Converts a Hyperion piece code to a Stockfish `Piece` (`NO_PIECE` if out of range).
inline Piece map_piece(int g_piece) {
    return (g_piece >= 0 && g_piece <= 11) ? Piece(g_to_sf_piece[g_piece]) : NO_PIECE;
}

/// Resets to the start position and sizes the search stack.
void init() {
    setup_reset("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    search_stack.resize(2048);
    stack_ptr = 0;
}

/// Pushes `move` on the search stack.
void push_state(int move) {
    StateInfo *si = &search_stack[stack_ptr++];
    do_move(move, si);
}

/// Pushes a null move on the search stack.
void push_null_state() {
    StateInfo *si = &search_stack[stack_ptr++];
    do_null_move(si);
}

/// Pops the most recent search move.
void pop_state(int move) {
    undo_move(move);
    --stack_ptr;
}

/// Applies a Hyperion move to the NNUE position, recording dirty pieces for the lazy accumulator update.
void do_move(int move, StateInfo *new_si) {
    new_si->previous = global_pos.state();

    new_si->accumulatorBig.computed[WHITE] = false;
    new_si->accumulatorBig.computed[BLACK] = false;
    new_si->accumulatorSmall.computed[WHITE] = false;
    new_si->accumulatorSmall.computed[BLACK] = false;

    const int from = getMoveSource(move);
    const int to = getMoveTarget(move);
    const int piece_idx = getMovePiece(move);
    const int promo_idx = getPromo(move);

    const Square sf_from = Square(mapSquare(from));
    const Square sf_to = Square(mapSquare(to));
    const Piece sf_piece = map_piece(piece_idx);
    const Piece sf_promo = map_piece(promo_idx);

    DirtyPiece &dp = new_si->dirtyPiece;
    dp.dirty_num = 0;

    Piece captured = global_pos.piece_on(sf_to);

    new_si->nonPawnMaterial[WHITE] = new_si->previous->nonPawnMaterial[WHITE];
    new_si->nonPawnMaterial[BLACK] = new_si->previous->nonPawnMaterial[BLACK];

    if (getEp(move)) {
        const Square cap_sq = make_square(file_of(sf_to), rank_of(sf_from));
        captured = global_pos.piece_on(cap_sq);
        global_pos.remove_piece(cap_sq);
        dp.piece[dp.dirty_num] = captured;
        dp.from[dp.dirty_num] = cap_sq;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;
    } else if (captured != NO_PIECE) {
        if (type_of(captured) != PAWN) {
            new_si->nonPawnMaterial[color_of(captured)] -= PieceValue[captured];
        }
        global_pos.remove_piece(sf_to);
        dp.piece[dp.dirty_num] = captured;
        dp.from[dp.dirty_num] = sf_to;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;
    }

    if (promo_idx != 0) {
        global_pos.remove_piece(sf_from);
        dp.piece[dp.dirty_num] = sf_piece;
        dp.from[dp.dirty_num] = sf_from;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;

        global_pos.put_piece(sf_promo, sf_to);
        dp.piece[dp.dirty_num] = sf_promo;
        dp.from[dp.dirty_num] = SQ_NONE;
        dp.to[dp.dirty_num] = sf_to;
        dp.dirty_num++;

        if (type_of(sf_promo) != PAWN) {
            new_si->nonPawnMaterial[color_of(sf_promo)] += PieceValue[sf_promo];
        }
    } else {
        global_pos.remove_piece(sf_from);
        if (getCastle(move)) {
            Square r_from = SQ_NONE;
            Square r_to = SQ_NONE;
            if (sf_to == SQ_G1)      { r_from = SQ_H1; r_to = SQ_F1; }
            else if (sf_to == SQ_C1) { r_from = SQ_A1; r_to = SQ_D1; }
            else if (sf_to == SQ_G8) { r_from = SQ_H8; r_to = SQ_F8; }
            else if (sf_to == SQ_C8) { r_from = SQ_A8; r_to = SQ_D8; }

            if (r_from != SQ_NONE) {
                const Piece rook = global_pos.piece_on(r_from);
                global_pos.remove_piece(r_from);
                global_pos.put_piece(rook, r_to);
                dp.piece[dp.dirty_num] = rook;
                dp.from[dp.dirty_num] = r_from;
                dp.to[dp.dirty_num] = r_to;
                dp.dirty_num++;
            }
        }
        global_pos.put_piece(sf_piece, sf_to);
        dp.piece[dp.dirty_num] = sf_piece;
        dp.from[dp.dirty_num] = sf_from;
        dp.to[dp.dirty_num] = sf_to;
        dp.dirty_num++;
    }

    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = new_si;

    if (type_of(sf_piece) == PAWN || captured != NO_PIECE) {
        new_si->rule50 = 0;
    } else {
        new_si->rule50 = new_si->previous->rule50 + 1;
    }
}

/// Flips the side to move without moving a piece.
void do_null_move(StateInfo *new_si) {
    new_si->previous = global_pos.state();
    new_si->accumulatorBig.computed[WHITE] = false;
    new_si->accumulatorBig.computed[BLACK] = false;
    new_si->accumulatorSmall.computed[WHITE] = false;
    new_si->accumulatorSmall.computed[BLACK] = false;
    new_si->nonPawnMaterial[WHITE] = new_si->previous->nonPawnMaterial[WHITE];
    new_si->nonPawnMaterial[BLACK] = new_si->previous->nonPawnMaterial[BLACK];
    new_si->dirtyPiece.dirty_num = 0;
    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = new_si;
    new_si->rule50 = new_si->previous->rule50 + 1;
}

/// Reverts the last move by replaying its dirty pieces backwards.
void undo_move(int) {
    StateInfo *old_si = global_pos.state();
    StateInfo *prev_si = old_si->previous;
    const DirtyPiece &dp = old_si->dirtyPiece;

    for (int i = dp.dirty_num - 1; i >= 0; --i) {
        const Piece pc = dp.piece[i];
        const Square f = dp.from[i];
        const Square t = dp.to[i];

        if (t == SQ_NONE) {
            global_pos.put_piece(pc, f);
        } else {
            global_pos.remove_piece(t);
            if (f != SQ_NONE) {
                global_pos.put_piece(pc, f);
            }
        }
    }

    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = prev_si;
}

/// NNUE evaluation of the current position.
int evaluate() {
    return Eval::evaluate(global_pos);
}

/// Hints the accumulator that sibling moves share this position as parent.
void hint_common_parent() {
    Eval::NNUE::hint_common_parent_position(global_pos);
}

/// Sets the root position from `fen` and clears the setup history.
void setup_reset(const std::string &fen) {
    setup_states.clear();
    setup_states.emplace_back();
    setup_states.back().previous = nullptr;
    global_pos.set(fen, &setup_states.back());
    stack_ptr = 0;
}

/// Plays a game-history move on the root position.
void setup_move(int move) {
    setup_states.emplace_back();
    StateInfo *next = &setup_states.back();
    do_move(move, next);
}

/// Takes back a game-history move.
void setup_undo(int move) {
    undo_move(move);
    setup_states.pop_back();
}

/// @return the thread-local NNUE position.
const Position& get_global_pos() {
    return global_pos;
}

/// @return the thread-local setup (game-history) state stack.
const std::deque<StateInfo>& get_setup_states() {
    return setup_states;
}

/// Clones the main thread's NNUE position and re-links its state chain for this helper thread.
void sync_from_main_thread(const Position& main_pos, const std::deque<StateInfo>& main_setup) {
    setup_states = main_setup;
    std::memcpy(&global_pos, &main_pos, sizeof(Position));
    if (!setup_states.empty()) {
        global_pos.st = &setup_states.back();
        for (size_t i = 0; i < setup_states.size(); ++i) {
            setup_states[i].previous = (i == 0) ? nullptr : &setup_states[i - 1];
        }
    } else {
        global_pos.st = nullptr;
    }
    search_stack.resize(2048);
    stack_ptr = 0;
}

} // namespace Incremental
} // namespace Stockfish
