/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef POSITION_H
#define POSITION_H

#include "bitboard.h"
#include "thread.h"
#include <algorithm>
#include <array>
#include <string>

#define FenPosEmpty "8/8/8/8/8/8/8/8 b - - "
#define FenPosStartpos "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define FenPosKiwipete "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - "
#define FenPos3 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - "
#define FenPos4 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
#define FenPos5 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
#define FenPos6 "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"

inline const std::array<std::string, 12> PieceStr {
    "P", "N", "B", "R", "Q", "K", "p", "n", "b", "r", "q", "k",
};

constexpr int charToPiece(unsigned char c) noexcept {
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

constexpr Side colorFromPiece(int piece) noexcept {
    return (piece >= P && piece <= K) ? White : Black;
}

constexpr int pieceType(int piece) noexcept {
    return (piece >= p) ? (piece - 6) : piece;
}

constexpr char promoChar(int promo) noexcept {
    switch (promo) {
        case Q: case q: return 'q';
        case R: case r: return 'r';
        case B: case b: return 'b';
        case N: case n: return 'n';
        default: return '\0';
    }
}

enum castling { wk = 1, wq = 2, bk = 4, bq = 8 };

void resetBoard();
void printBoard();
bool setPosition(const std::string &);
std::string getFEN();

// Fast repetition detection bounded by 50-move rule and stepping by 2 plies
static inline bool isRepetition() noexcept {
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

static inline bool isDraw() noexcept {
    if (fifty > 99)
        return true;

    if (isRepetition())
        return true;

    // Fast check: if any pawns, rooks, or queens remain, material is sufficient
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
        return true; // Only 1 minor piece on board

    if (total_pieces == 4) {
        if (white_knights == 2 || black_knights == 2)
            return true;
        if ((white_knights + white_bishops == 1) && (black_knights + black_bishops == 1))
            return true;
    }

    const Bitboard allBishops = bitboards[B] | bitboards[b];
    if (allBishops && (white_knights + black_knights == 0)) {
        if (!((allBishops & LightSquares) && (allBishops & DarkSquares)))
            return true;
    }

    return false;
}

static inline bool noMajorsOrMinors() noexcept {
    const int side = sideToMove;
    if (side == White)
        return (bitboards[N] | bitboards[B] | bitboards[R] | bitboards[Q]) == 0ULL;
    else
        return (bitboards[n] | bitboards[b] | bitboards[r] | bitboards[q]) == 0ULL;
}

#endif // POSITION_H
