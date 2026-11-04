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
 * @file position.h
 * @brief Board position helpers: FEN I/O, piece/character tables, repetition
 *        and draw detection.
 *
 * The actual board state (bitboards, mailbox, keys, ...) lives in the
 * per-thread ThreadState (see thread.h) and is accessed through the
 * `bitboards`, `occupancies`, `mailbox`, ... accessor macros so that every
 * search thread works on its own private copy of the position.
 */

#ifndef POSITION_H
#define POSITION_H

#include "bitboard.h"
#include "thread.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

// Well-known FEN strings (perft and testing positions).
inline constexpr std::string_view FenPosEmpty     = "8/8/8/8/8/8/8/8 b - - ";
inline constexpr std::string_view FenPosStartpos  = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
inline constexpr std::string_view FenPosKiwipete  = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - ";
inline constexpr std::string_view FenPos3         = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - ";
inline constexpr std::string_view FenPos4         = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
inline constexpr std::string_view FenPos5         = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
inline constexpr std::string_view FenPos6         = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

/// FEN letter for each piece code (indexed by the `Pieces` enum).
inline constexpr std::array<std::string_view, 12> PieceStr {
    "P", "N", "B", "R", "Q", "K", "p", "n", "b", "r", "q", "k",
};

/// Maps a FEN piece letter to its `Pieces` code (`NoPiece` if not a piece letter).
[[nodiscard]] constexpr int charToPiece(unsigned char c) noexcept {
    switch (c) {
        case 'P': return P;
        case 'N': return N;
        case 'B': return B;
        case 'R': return R;
        case 'Q': return Q;
        case 'K': return K;
        case 'p': return p;
        case 'n': return n;
        case 'b': return b;
        case 'r': return r;
        case 'q': return q;
        case 'k': return k;
        default:  return NoPiece;
    }
}

/// @return the colour owning `piece`.
[[nodiscard]] constexpr Side colorFromPiece(int piece) noexcept {
    return (piece >= P && piece <= K) ? White : Black;
}

/// @return the colour-agnostic piece type (`P`..`K`) of `piece`.
[[nodiscard]] constexpr int pieceType(int piece) noexcept {
    return (piece >= p) ? (piece - 6) : piece;
}

/// @return the UCI promotion letter for `promo`, or `'\0'` if it is not a promotion piece.
[[nodiscard]] constexpr char promoChar(int promo) noexcept {
    switch (promo) {
        case Q: case q: return 'q';
        case R: case r: return 'r';
        case B: case b: return 'b';
        case N: case n: return 'n';
        default: return '\0';
    }
}

/// Castling-right bit flags stored in `castle`.
enum castling { wk = 1, wq = 2, bk = 4, bq = 8 };

/// Clears the board and all incremental state (keys, repetition history, ...).
void resetBoard();

/// Prints the board, FEN, hash key, side to move, en-passant square and castling rights.
void printBoard();

/**
 * Parses a FEN string into the current thread's position.
 * @return false when the FEN is invalid (a king is missing).
 */
bool setPosition(std::string_view fen);

/// @return the FEN string of the current position.
std::string getFEN();

/**
 * Fast repetition detection.
 *
 * The scan is bounded by the 50-move counter and steps by two plies (only
 * positions with the same side to move can repeat).  A single repetition
 * inside the search tree, or a threefold repetition including game history,
 * is reported as a draw.
 */
[[nodiscard]] inline bool isRepetition() noexcept {
    const int min_idx = std::max(0, repetition_index - fifty);
    int hits = 0;
    for (int index = repetition_index - 2; index >= min_idx; index -= 2) {
        if (repetition_table[index] == hash_key) {
            if (index >= root_repetition_index)
                return true;
            if (++hits >= 2)
                return true;
        }
    }
    return false;
}

/**
 * @return true when the current position is a draw by the 50-move rule,
 *         repetition, or insufficient material (K vs K, K+minor vs K,
 *         KNN vs K, same-colour bishops, minor vs minor).
 */
[[nodiscard]] inline bool isDraw() noexcept {
    if (fifty > 99)
        return true;

    if (isRepetition())
        return true;

    // Any pawn, rook or queen means mating material exists.
    if (bitboards[P] | bitboards[p] | bitboards[R] | bitboards[r] | bitboards[Q] | bitboards[q])
        return false;

    const int total_pieces = countBits(occupancies[Both]);
    if (total_pieces == 2)
        return true;

    const int white_knights = countBits(bitboards[N]);
    const int black_knights = countBits(bitboards[n]);
    const int white_bishops = countBits(bitboards[B]);
    const int black_bishops = countBits(bitboards[b]);

    if (total_pieces == 3)
        return true; // Only one minor piece on the board

    if (total_pieces == 4) {
        if (white_knights == 2 || black_knights == 2)
            return true;
        if ((white_knights + white_bishops == 1) && (black_knights + black_bishops == 1))
            return true;
    }

    // Only bishops left, all on squares of one colour.
    const Bitboard allBishops = bitboards[B] | bitboards[b];
    if (allBishops && (white_knights + black_knights == 0)) {
        if (!((allBishops & LightSquares) && (allBishops & DarkSquares)))
            return true;
    }

    return false;
}

/// @return true when the side to move has no knights, bishops, rooks or queens
///         (null-move pruning is unsafe due to zugzwang).
[[nodiscard]] inline bool noMajorsOrMinors() noexcept {
    if (sideToMove == White)
        return (bitboards[N] | bitboards[B] | bitboards[R] | bitboards[Q]) == 0ULL;
    return (bitboards[n] | bitboards[b] | bitboards[r] | bitboards[q]) == 0ULL;
}

#endif // POSITION_H
