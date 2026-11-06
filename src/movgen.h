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
 * @file movgen.h
 * @brief Move encoding, pseudo-legal move generation interface, attack
 *        detection and the incremental `makeMove()` / board snapshot logic.
 *
 * A move is packed in a 24-bit integer:
 *
 * | bits    | field                                   |
 * |---------|-----------------------------------------|
 * | 0 - 5   | source square                           |
 * | 6 - 11  | target square                           |
 * | 12 - 15 | moving piece (`Pieces`)                 |
 * | 16 - 19 | promotion piece (`Pieces`, 0 if none)   |
 * | 20      | capture flag                            |
 * | 21      | double pawn push flag                   |
 * | 22      | en-passant flag                         |
 * | 23      | castling flag                           |
 */

#ifndef MOVGEN_H
#define MOVGEN_H

#include "bitboard.h"
#include "nnue.h"
#include "position.h"
#include "tt.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

/// Fixed-capacity list of moves produced by the move generators.
struct MoveList_t {
    int moves[256];
    int count;
};

/// Packs the move fields into the 24-bit move encoding described above.
[[nodiscard]] constexpr int encodeMove(int fromSq, int toSq, int piece, int promo,
                                       int capture, int dpush, int ep, int castling) noexcept {
    return fromSq | (toSq << 6) | (piece << 12) | (promo << 16) |
           (capture << 20) | (dpush << 21) | (ep << 22) | (castling << 23);
}

[[nodiscard]] constexpr int getMoveSource(int move) noexcept  { return move & 0x3f; }
[[nodiscard]] constexpr int getMoveTarget(int move) noexcept  { return (move & 0xfc0) >> 6; }
[[nodiscard]] constexpr int getMovePiece(int move) noexcept   { return (move & 0xf000) >> 12; }
[[nodiscard]] constexpr int getPromo(int move) noexcept       { return (move & 0xf0000) >> 16; }
/// @return non-zero when `move` is a capture.
[[nodiscard]] constexpr int getMoveCapture(int move) noexcept { return move & 0x100000; }
/// @return non-zero when `move` is a double pawn push.
[[nodiscard]] constexpr int getDoublePush(int move) noexcept  { return move & 0x200000; }
/// @return non-zero when `move` is an en-passant capture.
[[nodiscard]] constexpr int getEp(int move) noexcept          { return move & 0x400000; }
/// @return non-zero when `move` is a castling move.
[[nodiscard]] constexpr int getCastle(int move) noexcept      { return move & 0x800000; }

/// Castling rights that survive a move touching the given square (AND-mask).
inline constexpr int castling_rights[64] = {
    7,  15, 15, 15, 3,  15, 15, 11, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 15, 15, 12, 15, 15, 14
};

/// Generates all pseudo-legal moves (legality is verified by makeMove()).
void generateMoves(MoveList_t &);

/// Generates pseudo-legal captures, en-passant captures and queening/underpromotion pushes.
void generateCapturesAndPromotions(MoveList_t &);

/// Generates pseudo-legal non-capturing, non-promoting moves (including castling).
void generateQuietMoves(MoveList_t &);

/// Prints a move list as a table (debugging aid).
void printMoveList(MoveList_t &);

/// @return the move in UCI long-algebraic notation (`e2e4`, `e7e8q`, or `0000` for no move).
[[nodiscard]] inline std::string prettyMove(int move) {
    if (!move)
        return "0000";
    std::string res;
    res += SquareToCoordinates[getMoveSource(move)];
    res += SquareToCoordinates[getMoveTarget(move)];
    if (const int promo = getPromo(move); promo != 0)
        res += promoChar(promo);
    return res;
}

/// Prints `move` in UCI notation to standard output.
inline void printMove(int move) {
    std::cout << prettyMove(move);
}

/// Appends `move` to `moveList`.
inline void addMove(MoveList_t &moveList, int move) noexcept {
    moveList.moves[moveList.count++] = move;
}

/// @return true when `square` is attacked by any piece of colour `side`.
[[nodiscard]] inline bool isSquareAttacked(int square, int side) noexcept {
    if (square < 0 || square >= 64) return false;
    const int off = (side == White) ? 0 : 6; // piece-code offset of the attacking side

    if (PawnAttacks[side ^ 1][square] & bitboards[P + off]) return true;
    if (KnightAttacks[square] & bitboards[N + off])         return true;
    if (KingAttacks[square] & bitboards[K + off])           return true;
    if (getBishopAttacks(square, occupancies[Both]) & (bitboards[B + off] | bitboards[Q + off])) return true;
    if (getRookAttacks(square, occupancies[Both]) & (bitboards[R + off] | bitboards[Q + off]))   return true;
    return false;
}

/// @return true when `move` by `sideToMove` delivers check to the opponent king.
[[nodiscard]] inline bool givesCheck(int move) noexcept {
    const int oppKingSq = ls1b(sideToMove == White ? bitboards[k] : bitboards[K]);
    if (oppKingSq < 0 || oppKingSq >= 64) return false;

    const int fromSq = getMoveSource(move);
    const int toSq   = getMoveTarget(move);
    const int promo  = getPromo(move);
    const int pt     = promo ? pieceType(promo) : pieceType(getMovePiece(move));

    // Fast check: direct attacks from destination
    if (pt == N && (KnightAttacks[toSq] & (1ULL << oppKingSq))) return true;
    if (pt == P && (PawnAttacks[sideToMove][toSq] & (1ULL << oppKingSq))) return true;

    // Occupancy after piece moves from fromSq to toSq
    Bitboard occ = (occupancies[Both] ^ (1ULL << fromSq)) | (1ULL << toSq);
    if (getEp(move)) {
        const int capSq = (sideToMove == White) ? (toSq + 8) : (toSq - 8);
        occ ^= (1ULL << capSq);
    }

    // Direct slider checks
    if ((pt == B || pt == Q) && (getBishopAttacks(toSq, occ) & (1ULL << oppKingSq))) return true;
    if ((pt == R || pt == Q) && (getRookAttacks(toSq, occ) & (1ULL << oppKingSq)))   return true;

    // Castling check: rook moves to f1/d1 or f8/d8
    if (getCastle(move)) {
        const int rTo = (toSq == 62) ? 61 : (toSq == 58) ? 59 : (toSq == 6) ? 5 : 3;
        return (getRookAttacks(rTo, occ) & (1ULL << oppKingSq)) != 0;
    }

    // Discovered check: unblocking a friendly slider toward the opponent king
    const int off = (sideToMove == White) ? 0 : 6;
    if (getBishopAttacks(oppKingSq, occ) & (bitboards[B + off] | bitboards[Q + off]) & ~(1ULL << fromSq)) return true;
    if (getRookAttacks(oppKingSq, occ) & (bitboards[R + off] | bitboards[Q + off]) & ~(1ULL << fromSq))   return true;

    return false;
}

/**
 * Copy of every reversible part of the board, used to undo a move by
 * restoring the saved state rather than by "unmaking" it.
 *
 * Construction captures the current thread's position; restore() writes it back.
 */
struct BoardSnapshot {
    Bitboard bb[12];
    Bitboard occ[3];
    int      mail[64];
    int      side;
    int      enpassant;
    int      castleRights;
    int      fiftyMoves;
    uint64_t hash;
    uint64_t pawnHash;
    uint64_t nonPawnHash[2];

    BoardSnapshot() noexcept
        : side(sideToMove), enpassant(epsq), castleRights(castle), fiftyMoves(fifty),
          hash(hash_key), pawnHash(pawn_key), nonPawnHash{non_pawn_key[0], non_pawn_key[1]} {
        std::memcpy(bb, bitboards, sizeof(bb));
        std::memcpy(occ, occupancies, sizeof(occ));
        std::memcpy(mail, mailbox, sizeof(mail));
    }

    /// Restores the board to the state captured at construction.
    void restore() const noexcept {
        std::memcpy(bitboards, bb, sizeof(bb));
        std::memcpy(occupancies, occ, sizeof(occ));
        std::memcpy(mailbox, mail, sizeof(mail));
        sideToMove      = side;
        epsq            = enpassant;
        castle          = castleRights;
        fifty           = fiftyMoves;
        hash_key        = hash;
        pawn_key        = pawnHash;
        non_pawn_key[0] = nonPawnHash[0];
        non_pawn_key[1] = nonPawnHash[1];
    }
};

namespace detail {

/// Moves the castling rook that accompanies a king landing on `kingTo`,
/// updating bitboards, mailbox and the hash keys.  No-op for other squares.
inline void castleRook(int kingTo) noexcept {
    int rookPiece, from, to, color;
    switch (kingTo) {
        case g1: rookPiece = R; from = h1; to = f1; color = White; break;
        case c1: rookPiece = R; from = a1; to = d1; color = White; break;
        case g8: rookPiece = r; from = h8; to = f8; color = Black; break;
        case c8: rookPiece = r; from = a8; to = d8; color = Black; break;
        default: return;
    }

    popBit(bitboards[rookPiece], from);   setBit(bitboards[rookPiece], to);
    popBit(occupancies[color], from);     setBit(occupancies[color], to);
    mailbox[from] = NoPiece;
    mailbox[to]   = rookPiece;

    const uint64_t delta = piece_keys[rookPiece][from] ^ piece_keys[rookPiece][to];
    hash_key ^= delta;
    non_pawn_key[color] ^= delta;
}

} // namespace detail

/**
 * Plays the pseudo-legal `move` on the current thread's board, updating
 * bitboards, mailbox, castling rights, en-passant square, 50-move counter and
 * all Zobrist keys incrementally.
 *
 * @return true if the move is legal (the mover's king is not left in check).
 *         When false the caller must restore the board (see BoardSnapshot).
 */
inline bool makeMove(int move) noexcept {
    assert(move);
    const int fromSq   = getMoveSource(move);
    const int toSq     = getMoveTarget(move);
    const int piece    = getMovePiece(move);
    const int promo    = getPromo(move);
    const int capture  = getMoveCapture(move);
    const int dpush    = getDoublePush(move);
    const int ep       = getEp(move);
    const int castling = getCastle(move);

    const int Us   = sideToMove;
    const int Them = Us ^ 1;

    // Handle the capture first so mailbox[toSq] is still intact.
    if (capture) {
        fifty = 0;
        if (ep) {
            const int capSq   = (Us == White) ? (toSq + 8) : (toSq - 8);
            const int capPawn = (Us == White) ? p : P;
            popBit(bitboards[capPawn], capSq);
            popBit(occupancies[Them], capSq);
            mailbox[capSq] = NoPiece;
            hash_key ^= piece_keys[capPawn][capSq];
            pawn_key ^= piece_keys[capPawn][capSq];
        } else if (const int capPiece = mailbox[toSq]; capPiece != NoPiece) {
            popBit(bitboards[capPiece], toSq);
            popBit(occupancies[Them], toSq);
            hash_key ^= piece_keys[capPiece][toSq];
            if (capPiece == P || capPiece == p)
                pawn_key ^= piece_keys[capPiece][toSq];
            else
                non_pawn_key[Them] ^= piece_keys[capPiece][toSq];
        }
    }

    // Move the piece from source to target.
    popBit(bitboards[piece], fromSq);
    setBit(bitboards[piece], toSq);
    popBit(occupancies[Us], fromSq);
    setBit(occupancies[Us], toSq);
    mailbox[fromSq] = NoPiece;
    mailbox[toSq]   = piece;

    const uint64_t moveDelta = piece_keys[piece][fromSq] ^ piece_keys[piece][toSq];
    hash_key ^= moveDelta;
    if (piece == P || piece == p) {
        pawn_key ^= moveDelta;
        fifty = 0;
    } else {
        non_pawn_key[Us] ^= moveDelta;
        if (!capture)
            fifty++;
    }

    if (castling)
        detail::castleRook(toSq);

    // Replace the pawn by the promotion piece.
    if (promo) {
        const int pawn = (Us == White) ? P : p;
        popBit(bitboards[pawn], toSq);
        setBit(bitboards[promo], toSq);
        mailbox[toSq] = promo;
        hash_key ^= piece_keys[pawn][toSq] ^ piece_keys[promo][toSq];
        pawn_key ^= piece_keys[pawn][toSq];
        non_pawn_key[Us] ^= piece_keys[promo][toSq];
    }

    // En-passant square: clear the old one, set a new one after a double push.
    if (epsq != NoSq)
        hash_key ^= enpassant_keys[epsq];
    epsq = NoSq;

    if (dpush) {
        epsq = (Us == White) ? (toSq + 8) : (toSq - 8);
        hash_key ^= enpassant_keys[epsq];
    }

    // Castling rights.
    hash_key ^= castle_keys[castle];
    castle &= castling_rights[fromSq];
    castle &= castling_rights[toSq];
    hash_key ^= castle_keys[castle];

    occupancies[Both] = occupancies[White] | occupancies[Black];

    sideToMove = Them;
    hash_key ^= side_key;

    const int kingSq = static_cast<int>(ls1b((Us == White) ? bitboards[K] : bitboards[k]));
    return !isSquareAttacked(kingSq, Them);
}

#endif // MOVGEN_H
