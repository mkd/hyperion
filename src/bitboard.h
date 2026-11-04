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
 * @file bitboard.h
 * @brief Bitboard primitives: board enums, masks, hardware bit intrinsics and
 *        magic-bitboard slider attack lookups.
 *
 * Square numbering is "a8 = 0 ... h1 = 63" (rank 8 first), so a bitboard bit
 * index equals the board square enum. Attack tables are filled once at start-up
 * by initBitboards() and are read-only afterwards, hence safe to share across
 * all search threads.
 */

#ifndef BITBOARD_H
#define BITBOARD_H

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

/// A set of squares: bit `n` is set when square `n` belongs to the set.
using Bitboard = uint64_t;

/// Side to move / piece colour. `Both` indexes the combined occupancy.
enum Side { White, Black, Both, NoColor };

/// Slider kind used when generating magic attack tables.
enum Slider { Rook, Bishop };

/// Piece codes: white pieces first (P..K), then black (p..k).
enum Pieces { P, N, B, R, Q, K, p, n, b, r, q, k, NoPiece = -1 };

/// Board squares in "a8 = 0 ... h1 = 63" order.
enum BoardSquares {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1,
    NoSq
};

/// Algebraic coordinates for each square (`"ns"` for NoSq).
inline constexpr std::array<std::string_view, 65> SquareToCoordinates {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8",
    "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
    "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
    "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
    "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
    "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
    "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
    "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
    "ns",
};

inline constexpr Bitboard AllSquares   = ~Bitboard(0);
inline constexpr Bitboard LightSquares = 0xAA55AA55AA55AA55ULL;
inline constexpr Bitboard DarkSquares  = ~LightSquares;

inline constexpr Bitboard FileA_Mask = 0x0101010101010101ULL;
inline constexpr Bitboard FileB_Mask = FileA_Mask << 1;
inline constexpr Bitboard FileC_Mask = FileA_Mask << 2;
inline constexpr Bitboard FileD_Mask = FileA_Mask << 3;
inline constexpr Bitboard FileE_Mask = FileA_Mask << 4;
inline constexpr Bitboard FileF_Mask = FileA_Mask << 5;
inline constexpr Bitboard FileG_Mask = FileA_Mask << 6;
inline constexpr Bitboard FileH_Mask = FileA_Mask << 7;

inline constexpr Bitboard Rank8_Mask = 0xFFULL;
inline constexpr Bitboard Rank7_Mask = Rank8_Mask << (8 * 1);
inline constexpr Bitboard Rank6_Mask = Rank8_Mask << (8 * 2);
inline constexpr Bitboard Rank5_Mask = Rank8_Mask << (8 * 3);
inline constexpr Bitboard Rank4_Mask = Rank8_Mask << (8 * 4);
inline constexpr Bitboard Rank3_Mask = Rank8_Mask << (8 * 5);
inline constexpr Bitboard Rank2_Mask = Rank8_Mask << (8 * 6);
inline constexpr Bitboard Rank1_Mask = Rank8_Mask << (8 * 7);

/// Masks used to stop leaper shifts from wrapping around the board edges.
inline constexpr Bitboard NotFileA_Mask  = ~FileA_Mask;
inline constexpr Bitboard NotFileH_Mask  = ~FileH_Mask;
inline constexpr Bitboard NotFileHG_Mask = ~(FileG_Mask | FileH_Mask);
inline constexpr Bitboard NotFileAB_Mask = ~(FileA_Mask | FileB_Mask);

/// Squares that must be empty to castle (f1/g1, f8/g8 king side; b..d queen side).
inline constexpr Bitboard FG1_Mask  = Rank1_Mask & (FileF_Mask | FileG_Mask);
inline constexpr Bitboard FG8_Mask  = Rank8_Mask & (FileF_Mask | FileG_Mask);
inline constexpr Bitboard DCB1_Mask = Rank1_Mask & (FileB_Mask | FileC_Mask | FileD_Mask);
inline constexpr Bitboard DCB8_Mask = Rank8_Mask & (FileB_Mask | FileC_Mask | FileD_Mask);

static_assert(NotFileA_Mask  == 18374403900871474942ULL);
static_assert(NotFileH_Mask  == 9187201950435737471ULL);
static_assert(NotFileHG_Mask == 4557430888798830399ULL);
static_assert(NotFileAB_Mask == 18229723555195321596ULL);
static_assert(FG1_Mask  == 6917529027641081856ULL);
static_assert(FG8_Mask  == 96ULL);
static_assert(DCB1_Mask == 1008806316530991104ULL);
static_assert(DCB8_Mask == 14ULL);

/// Single-bit bitboard for each square, built at compile time (`SqBB[NoSq] == 0`).
inline constexpr std::array<Bitboard, 65> SqBB = []() consteval {
    std::array<Bitboard, 65> arr{};
    for (int i = 0; i < 64; ++i)
        arr[i] = 1ULL << i;
    arr[64] = 0ULL;
    return arr;
}();

// Precomputed leaper attack tables, indexed by [side][square] or [square].
extern Bitboard PawnPushes[2][64];
extern Bitboard PawnDoublePushes[2][64];
extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];

// Magic-bitboard slider tables.
extern Bitboard BishopMasks[64];
extern Bitboard RookMasks[64];
extern Bitboard BishopAttacks[64][512];
extern Bitboard RookAttacks[64][4096];

/// State of the xorshift32 generator used for Zobrist key generation.
extern uint32_t rng32_state;

/// Number of relevant occupancy bits per square for bishop magics.
inline constexpr int BishopRelevantBits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};

/// Number of relevant occupancy bits per square for rook magics.
inline constexpr int RookRelevantBits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12
};

/// Pre-found rook magic multipliers (one per square).
inline constexpr Bitboard RookMagicNumbers[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,  0x100081001000420ULL,
    0x200020010080420ULL,  0x3001c0002010008ULL,  0x8480008002000100ULL, 0x2080088004402900ULL,
    0x800098204000ULL,     0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,    0x2200800100020080ULL, 0x801000060821100ULL,
    0x80044006422000ULL,   0x100808020004000ULL,  0x12108a0010204200ULL, 0x140848010000802ULL,
    0x481828014002800ULL,  0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,   0x20100080080080ULL,
    0x2000a00200410ULL,    0x20080800400ULL,      0x80088400100102ULL,   0x80004600042881ULL,
    0x4040008040800020ULL, 0x440003000200801ULL,  0x4200011004500ULL,    0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,  0x200046502000484ULL,
    0x480400080088020ULL,  0x1000422010034000ULL, 0x30200100110040ULL,   0x100021010009ULL,
    0x2002080100110004ULL, 0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL, 0x2060820c0120200ULL,
    0x1001004080100ULL,    0x20c020080040080ULL,  0x2935610830022400ULL, 0x44440041009200ULL,
    0x280001040802101ULL,  0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,  0x1004081002402ULL
};

/// Pre-found bishop magic multipliers (one per square).
inline constexpr Bitboard BishopMagicNumbers[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,   0x108060845042010ULL,
    0x581104180800210ULL,  0x2112080446200010ULL, 0x1080820820060210ULL, 0x3c0808410220200ULL,
    0x4050404440404ULL,    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,  0x801010402020200ULL,
    0x400210c3880100ULL,   0x404022024108200ULL,  0x810018200204102ULL,  0x4002801a02003ULL,
    0x85040820080400ULL,   0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,  0x20080002081110ULL,
    0x4001001021004000ULL, 0x800040400a011002ULL, 0xe4004081011002ULL,   0x1c004001012080ULL,
    0x8004200962a00220ULL, 0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL, 0x42008c0340209202ULL,
    0x209188240001000ULL,  0x400408a884001800ULL, 0x110400a6080400ULL,   0x1840060a44020800ULL,
    0x90080104000041ULL,   0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL, 0x300000261044000aULL,
    0x802241102020002ULL,  0x20906061210001ULL,   0x5a84841004010310ULL, 0x4010801011c04ULL,
    0xa010109502200ULL,    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL, 0x4010011029020020ULL
};

/// Pretty-prints a bitboard as an 8x8 ASCII board (debugging aid).
void printBitboard(Bitboard);

/// Pawn single-push target of `square` for `side` (empty if off-board).
Bitboard genPawnPush(int side, int square);

/// Pawn double-push target of `square` for `side` (empty unless on the start rank).
Bitboard genPawnDoublePush(int side, int square);

/// Relevant (inner-board) bishop ray mask for `square`, excluding edges.
Bitboard maskBishopAttacks(int square);

/// Relevant (inner-board) rook ray mask for `square`, excluding edges.
Bitboard maskRookAttacks(int square);

/// Builds the `index`-th subset of `attackMask` (which has `bitMask` bits set).
Bitboard setOccupancy(int index, int bitMask, Bitboard attackMask);

/// Fills the leaper tables (pawn, knight, king attacks and pushes).
void initLeaperAttacks();

/// Fills the magic-bitboard table for the given slider kind.
void initSliderAttacks(Slider);

/// Initialises every attack table. Must be called once before any search.
void initBitboards();

/// @return the bit of `b` at index `pos` (0 or 1).
[[nodiscard]] constexpr int getBit(Bitboard b, int pos) noexcept {
    return int((b >> pos) & 1ULL);
}

/// Sets bit `pos` of `b`.
constexpr void setBit(Bitboard &b, int pos) noexcept {
    b |= (1ULL << pos);
}

/// Clears bit `pos` of `b`.
constexpr void popBit(Bitboard &b, int pos) noexcept {
    b &= ~(1ULL << pos);
}

/// Flips bit `pos` of `b`.
constexpr void toggleBit(Bitboard &b, int pos) noexcept {
    b ^= (1ULL << pos);
}

/// @return true when `b` has two or more bits set.
[[nodiscard]] constexpr bool moreThanOne(Bitboard b) noexcept {
    return (b & (b - 1)) != 0;
}

/// Population count (hardware `popcnt` / `cnt`).
[[nodiscard]] constexpr int countBits(Bitboard bb) noexcept {
    return std::popcount(bb);
}

/// Index of the least significant set bit (undefined result when `bb == 0`).
[[nodiscard]] constexpr unsigned int ls1b(Bitboard bb) noexcept {
    return static_cast<unsigned int>(std::countr_zero(bb));
}

/// Removes and returns the index of the least significant set bit of `b`.
constexpr int popLsb(Bitboard &b) noexcept {
    assert(b);
    const int s = static_cast<int>(std::countr_zero(b));
    b &= b - 1;
    return s;
}

/// Bishop attacks from `square` given board occupancy `occupancy` (magic lookup).
[[nodiscard]] inline Bitboard getBishopAttacks(int square, Bitboard occupancy) noexcept {
    occupancy &= BishopMasks[square];
    occupancy *= BishopMagicNumbers[square];
    occupancy >>= 64 - BishopRelevantBits[square];
    return BishopAttacks[square][occupancy];
}

/// Rook attacks from `square` given board occupancy `occupancy` (magic lookup).
[[nodiscard]] inline Bitboard getRookAttacks(int square, Bitboard occupancy) noexcept {
    occupancy &= RookMasks[square];
    occupancy *= RookMagicNumbers[square];
    occupancy >>= 64 - RookRelevantBits[square];
    return RookAttacks[square][occupancy];
}

/// Queen attacks from `square`: union of bishop and rook attacks.
[[nodiscard]] inline Bitboard getQueenAttacks(int square, Bitboard occupancy) noexcept {
    return getBishopAttacks(square, occupancy) | getRookAttacks(square, occupancy);
}

/// Advances the xorshift32 generator and returns the next 32-bit value.
inline uint32_t rng32() noexcept {
    uint32_t number = rng32_state;
    number ^= number << 13;
    number ^= number >> 17;
    number ^= number << 5;
    rng32_state = number;
    return number;
}

/// Builds a 64-bit pseudo-random value from four 16-bit xorshift32 samples.
inline uint64_t rng64() noexcept {
    const uint64_t n1 = uint64_t(rng32() & 0xFFFF);
    const uint64_t n2 = uint64_t(rng32() & 0xFFFF);
    const uint64_t n3 = uint64_t(rng32() & 0xFFFF);
    const uint64_t n4 = uint64_t(rng32() & 0xFFFF);
    return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

#endif // BITBOARD_H
