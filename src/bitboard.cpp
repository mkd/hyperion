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
 * @file bitboard.cpp
 * @brief Definitions and start-up initialisation of the attack tables
 *        (leapers and magic-bitboard sliders) declared in bitboard.h.
 */

#include "bitboard.h"

#include <iomanip>
#include <iostream>
#include <span>

Bitboard PawnPushes[2][64];
Bitboard PawnDoublePushes[2][64];
Bitboard PawnAttacks[2][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];

Bitboard BishopMasks[64];
Bitboard RookMasks[64];
Bitboard BishopAttacks[64][512];
Bitboard RookAttacks[64][4096];

uint32_t rng32_state = 1804289383;

namespace {

/// A (rank, file) step used to walk a sliding ray.
struct Step {
    int dr;
    int df;
};

inline constexpr Step BishopSteps[] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
inline constexpr Step RookSteps[]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

/**
 * Walks every ray from `square` along `steps`.
 *
 * Only the coordinate(s) that actually change are bounded to `[lo, hi]`,
 * so `lo = 1, hi = 6` yields the inner-board relevance masks while
 * `lo = 0, hi = 7` yields full attacks.  A ray stops after the first square
 * contained in `block` (that square is included, as it can be captured).
 */
Bitboard walkRays(int square, std::span<const Step> steps, int lo, int hi, Bitboard block) {
    Bitboard attacks = 0ULL;
    const int rank = square / 8;
    const int file = square % 8;

    for (const Step s : steps) {
        for (int r = rank + s.dr, f = file + s.df;
             (s.dr == 0 || (r >= lo && r <= hi)) && (s.df == 0 || (f >= lo && f <= hi));
             r += s.dr, f += s.df) {
            const Bitboard bit = 1ULL << (r * 8 + f);
            attacks |= bit;
            if (bit & block)
                break;
        }
    }
    return attacks;
}

/// Pawn capture targets of `side` standing on `square`.
Bitboard maskPawnAttacks(int side, int square) {
    const Bitboard bb = SqBB[square];
    Bitboard attacks = 0ULL;
    if (side == White) {
        if ((bb >> 7) & NotFileA_Mask) attacks |= (bb >> 7);
        if ((bb >> 9) & NotFileH_Mask) attacks |= (bb >> 9);
    } else {
        if ((bb << 7) & NotFileH_Mask) attacks |= (bb << 7);
        if ((bb << 9) & NotFileA_Mask) attacks |= (bb << 9);
    }
    return attacks;
}

/// Knight jump targets from `square`.
Bitboard maskKnightAttacks(int square) {
    const Bitboard bb = SqBB[square];
    Bitboard attacks = 0ULL;
    if ((bb >> 17) & NotFileH_Mask)  attacks |= (bb >> 17);
    if ((bb >> 15) & NotFileA_Mask)  attacks |= (bb >> 15);
    if ((bb >> 10) & NotFileHG_Mask) attacks |= (bb >> 10);
    if ((bb >> 6)  & NotFileAB_Mask) attacks |= (bb >> 6);
    if ((bb << 17) & NotFileA_Mask)  attacks |= (bb << 17);
    if ((bb << 15) & NotFileH_Mask)  attacks |= (bb << 15);
    if ((bb << 10) & NotFileAB_Mask) attacks |= (bb << 10);
    if ((bb << 6)  & NotFileHG_Mask) attacks |= (bb << 6);
    return attacks;
}

/// King step targets from `square`.
Bitboard maskKingAttacks(int square) {
    const Bitboard bb = SqBB[square];
    Bitboard attacks = 0ULL;
    if (bb >> 8)                   attacks |= (bb >> 8);
    if ((bb >> 9) & NotFileH_Mask) attacks |= (bb >> 9);
    if ((bb >> 7) & NotFileA_Mask) attacks |= (bb >> 7);
    if ((bb >> 1) & NotFileH_Mask) attacks |= (bb >> 1);
    if (bb << 8)                   attacks |= (bb << 8);
    if ((bb << 9) & NotFileA_Mask) attacks |= (bb << 9);
    if ((bb << 7) & NotFileH_Mask) attacks |= (bb << 7);
    if ((bb << 1) & NotFileA_Mask) attacks |= (bb << 1);
    return attacks;
}

/// Bishop attacks from `square` stopping at the blockers in `block`.
Bitboard genBishopAttacks(int square, Bitboard block) {
    return walkRays(square, BishopSteps, 0, 7, block);
}

/// Rook attacks from `square` stopping at the blockers in `block`.
Bitboard genRookAttacks(int square, Bitboard block) {
    return walkRays(square, RookSteps, 0, 7, block);
}

} // namespace

Bitboard genPawnPush(int side, int square) {
    const Bitboard bb = SqBB[square];
    return (side == White) ? (bb >> 8) : (bb << 8);
}

Bitboard genPawnDoublePush(int side, int square) {
    const Bitboard bb = SqBB[square];
    if (side == White)
        return (bb & Rank2_Mask) ? (bb >> 16) : 0ULL;
    return (bb & Rank7_Mask) ? (bb << 16) : 0ULL;
}

Bitboard maskBishopAttacks(int square) {
    return walkRays(square, BishopSteps, 1, 6, 0ULL);
}

Bitboard maskRookAttacks(int square) {
    return walkRays(square, RookSteps, 1, 6, 0ULL);
}

Bitboard setOccupancy(int index, int bitMask, Bitboard attackMask) {
    Bitboard occupancy = 0ULL;
    for (int count = 0; count < bitMask; count++) {
        const int square = static_cast<int>(ls1b(attackMask));
        attackMask ^= SqBB[square];
        if (index & (1 << count))
            occupancy |= SqBB[square];
    }
    return occupancy;
}

void initLeaperAttacks() {
    for (int square = 0; square < 64; square++) {
        PawnPushes[White][square]       = genPawnPush(White, square);
        PawnPushes[Black][square]       = genPawnPush(Black, square);
        PawnDoublePushes[White][square] = genPawnDoublePush(White, square);
        PawnDoublePushes[Black][square] = genPawnDoublePush(Black, square);
        PawnAttacks[White][square]      = maskPawnAttacks(White, square);
        PawnAttacks[Black][square]      = maskPawnAttacks(Black, square);
        KnightAttacks[square]           = maskKnightAttacks(square);
        KingAttacks[square]             = maskKingAttacks(square);
    }
}

void initSliderAttacks(Slider slider) {
    const bool isBishop = (slider == Bishop);

    for (int square = 0; square < 64; square++) {
        BishopMasks[square] = maskBishopAttacks(square);
        RookMasks[square]   = maskRookAttacks(square);

        const Bitboard attackMask = isBishop ? BishopMasks[square] : RookMasks[square];
        const int relevantBits    = countBits(attackMask);
        const int occupancyCount  = 1 << relevantBits;

        for (int index = 0; index < occupancyCount; index++) {
            const Bitboard occupancy = setOccupancy(index, relevantBits, attackMask);
            if (isBishop) {
                const auto magicIndex =
                    (occupancy * BishopMagicNumbers[square]) >> (64 - BishopRelevantBits[square]);
                BishopAttacks[square][magicIndex] = genBishopAttacks(square, occupancy);
            } else {
                const auto magicIndex =
                    (occupancy * RookMagicNumbers[square]) >> (64 - RookRelevantBits[square]);
                RookAttacks[square][magicIndex] = genRookAttacks(square, occupancy);
            }
        }
    }
}

void printBitboard(Bitboard bb) {
    using std::cout, std::endl, std::setw;

    cout << "    +----+----+----+----+----+----+----+----+" << endl;
    for (int rank = 0; rank < 8; rank++) {
        cout << setw(3) << 8 - rank << " |";
        for (int file = 0; file < 8; file++) {
            const int square = rank * 8 + file;
            cout << " " << (getBit(bb, square) ? " 1 " : " . ") << "|";
        }
        cout << endl << "    +----+----+----+----+----+----+----+----+" << endl;
    }
    cout << "      a    b    c    d    e    f    g    h" << endl << endl;
    cout << endl << "Bitboard: " << bb << "ULL" << endl << endl;
}

void initBitboards() {
    initLeaperAttacks();
    initSliderAttacks(Bishop);
    initSliderAttacks(Rook);
}
