/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#include "bitboard.h"
#include "tt.h"
#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>

using namespace std;

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

Bitboard genPawnPush(int side, int square) {
    Bitboard bb = SqBB[square];
    return (side == White) ? (bb >> 8) : (bb << 8);
}

Bitboard genPawnDoublePush(int side, int square) {
    Bitboard bb = SqBB[square];
    if (side == White)
        return (bb & Rank2_Mask) ? (bb >> 16) : 0ULL;
    else
        return (bb & Rank7_Mask) ? (bb << 16) : 0ULL;
}

Bitboard maskPawnAttacks(int side, int square) {
    Bitboard attacks = 0ULL;
    Bitboard bb = SqBB[square];
    if (side == White) {
        if ((bb >> 7) & NotFileA_Mask) attacks |= (bb >> 7);
        if ((bb >> 9) & NotFileH_Mask) attacks |= (bb >> 9);
    } else {
        if ((bb << 7) & NotFileH_Mask) attacks |= (bb << 7);
        if ((bb << 9) & NotFileA_Mask) attacks |= (bb << 9);
    }
    return attacks;
}

Bitboard maskKnightAttacks(int square) {
    Bitboard attacks = 0ULL;
    Bitboard bb = SqBB[square];
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

Bitboard maskKingAttacks(int square) {
    Bitboard attacks = 0ULL;
    Bitboard bb = SqBB[square];
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

Bitboard maskBishopAttacks(int square) {
    Bitboard attacks = 0ULL;
    int tr = square / 8;
    int tf = square % 8;
    for (int r = tr + 1, f = tf + 1; r <= 6 && f <= 6; r++, f++) attacks |= (1ULL << (r * 8 + f));
    for (int r = tr - 1, f = tf + 1; r >= 1 && f <= 6; r--, f++) attacks |= (1ULL << (r * 8 + f));
    for (int r = tr + 1, f = tf - 1; r <= 6 && f >= 1; r++, f--) attacks |= (1ULL << (r * 8 + f));
    for (int r = tr - 1, f = tf - 1; r >= 1 && f >= 1; r--, f--) attacks |= (1ULL << (r * 8 + f));
    return attacks;
}

Bitboard maskRookAttacks(int square) {
    Bitboard attacks = 0ULL;
    int tr = square / 8;
    int tf = square % 8;
    for (int r = tr + 1; r <= 6; r++) attacks |= (1ULL << (r * 8 + tf));
    for (int r = tr - 1; r >= 1; r--) attacks |= (1ULL << (r * 8 + tf));
    for (int f = tf + 1; f <= 6; f++) attacks |= (1ULL << (tr * 8 + f));
    for (int f = tf - 1; f >= 1; f--) attacks |= (1ULL << (tr * 8 + f));
    return attacks;
}

Bitboard genBishopAttacks(int square, Bitboard block) {
    Bitboard attacks = 0ULL;
    int tr = square / 8;
    int tf = square % 8;
    for (int r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (int r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (int r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    for (int r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if ((1ULL << (r * 8 + f)) & block) break;
    }
    return attacks;
}

Bitboard genRookAttacks(int square, Bitboard block) {
    Bitboard attacks = 0ULL;
    int tr = square / 8;
    int tf = square % 8;
    for (int r = tr + 1; r <= 7; r++) {
        attacks |= (1ULL << (r * 8 + tf));
        if ((1ULL << (r * 8 + tf)) & block) break;
    }
    for (int r = tr - 1; r >= 0; r--) {
        attacks |= (1ULL << (r * 8 + tf));
        if ((1ULL << (r * 8 + tf)) & block) break;
    }
    for (int f = tf + 1; f <= 7; f++) {
        attacks |= (1ULL << (tr * 8 + f));
        if ((1ULL << (tr * 8 + f)) & block) break;
    }
    for (int f = tf - 1; f >= 0; f--) {
        attacks |= (1ULL << (tr * 8 + f));
        if ((1ULL << (tr * 8 + f)) & block) break;
    }
    return attacks;
}

Bitboard setOccupancy(int index, int bitMask, Bitboard attackMask) {
    Bitboard occupancy = 0ULL;
    for (int count = 0; count < bitMask; count++) {
        int square = ls1b(attackMask);
        attackMask ^= SqBB[square];
        if (index & (1 << count))
            occupancy |= (1ULL << square);
    }
    return occupancy;
}

void initBitmaps() {}

void initLeaperAttacks() {
    for (int square = 0; square < 64; square++) {
        PawnPushes[White][square] = genPawnPush(White, square);
        PawnPushes[Black][square] = genPawnPush(Black, square);
        PawnDoublePushes[White][square] = genPawnDoublePush(White, square);
        PawnDoublePushes[Black][square] = genPawnDoublePush(Black, square);
        PawnAttacks[White][square] = maskPawnAttacks(White, square);
        PawnAttacks[Black][square] = maskPawnAttacks(Black, square);
        KnightAttacks[square] = maskKnightAttacks(square);
        KingAttacks[square] = maskKingAttacks(square);
    }
}

void initSliderAttacks(Slider isBishop) {
    for (int square = 0; square < 64; square++) {
        BishopMasks[square] = maskBishopAttacks(square);
        RookMasks[square] = maskRookAttacks(square);
        Bitboard AttackMask = isBishop ? BishopMasks[square] : RookMasks[square];
        int RelevantBitsCount = countBits(AttackMask);
        int OccupancyIndices = (1 << RelevantBitsCount);

        for (int index = 0; index < OccupancyIndices; index++) {
            Bitboard occupancy = setOccupancy(index, RelevantBitsCount, AttackMask);
            if (isBishop) {
                int MagicIndex = (occupancy * BishopMagicNumbers[square]) >> (64 - BishopRelevantBits[square]);
                BishopAttacks[square][MagicIndex] = genBishopAttacks(square, occupancy);
            } else {
                int MagicIndex = (occupancy * RookMagicNumbers[square]) >> (64 - RookRelevantBits[square]);
                RookAttacks[square][MagicIndex] = genRookAttacks(square, occupancy);
            }
        }
    }
}

void printBitboard(Bitboard bb) {
    cout << "    +----+----+----+----+----+----+----+----+" << endl;
    for (int rank = 0; rank < 8; rank++) {
        cout << setw(3) << 8 - rank << " |";
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            cout << " " << (getBit(bb, square) ? " 1 " : " . ") << "|";
        }
        cout << endl << "    +----+----+----+----+----+----+----+----+" << endl;
    }
    cout << "      a    b    c    d    e    f    g    h" << endl << endl;
    cout << endl << "Bitboard: " << bb << "ULL" << endl << endl;
}

void initBitboards() {
    initBitmaps();
    initLeaperAttacks();
    initSliderAttacks(Bishop);
    initSliderAttacks(Rook);
}
