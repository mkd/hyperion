/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef MOVGEN_H
#define MOVGEN_H

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#ifdef USE_NEW_NNUE
#pragma push_macro("ply")
#pragma push_macro("sideToMove")
#undef ply
#undef sideToMove
#include "stockfish_probe/nnue_incremental.h"
#pragma pop_macro("sideToMove")
#pragma pop_macro("ply")
#define NNUE_SAVE
#define NNUE_DO(m) Stockfish::Incremental::push_state(m);
#define NNUE_DO_NULL Stockfish::Incremental::push_null_state();
#define NNUE_UNDO(m) Stockfish::Incremental::pop_state(m);
#define NNUE_UNDO_NULL Stockfish::Incremental::pop_state(0);
#else
#define NNUE_SAVE
#define NNUE_DO(m)
#define NNUE_DO_NULL
#define NNUE_UNDO(m)
#define NNUE_UNDO_NULL
#endif

#include "bitboard.h"
#include "position.h"
#include "tt.h"

struct MoveList_t {
    int moves[256];
    int count;
};

#define encodeMove(fromSq, toSq, piece, promo, capture, dpush, ep, castling) \
    ((fromSq) | ((toSq) << 6) | ((piece) << 12) | ((promo) << 16) |          \
     ((capture) << 20) | ((dpush) << 21) | ((ep) << 22) | ((castling) << 23))

#define getMoveSource(move) ((move) & 0x3f)
#define getMoveTarget(move) (((move) & 0xfc0) >> 6)
#define getMovePiece(move) (((move) & 0xf000) >> 12)
#define getPromo(move) (((move) & 0xf0000) >> 16)
#define getMoveCapture(move) ((move) & 0x100000)
#define getDoublePush(move) ((move) & 0x200000)
#define getEp(move) ((move) & 0x400000)
#define getCastle(move) ((move) & 0x800000)

inline constexpr int castling_rights[64] = {
    7,  15, 15, 15, 3,  15, 15, 11, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 15, 15, 12, 15, 15, 14
};

void generateMoves(MoveList_t &);
void generateCapturesAndPromotions(MoveList_t &);
void generateQuietMoves(MoveList_t &);
void printMoveList(MoveList_t &);

static inline std::string prettyMove(int move) {
    if (!move) return "0000";
    std::string res = SquareToCoordinates[getMoveSource(move)] + SquareToCoordinates[getMoveTarget(move)];
    if (int pr = getPromo(move); pr != 0)
        res += promoChar(pr);
    return res;
}

static inline void printMove(int move) {
    std::cout << prettyMove(move);
}

static inline void addMove(MoveList_t &MoveList, int move) noexcept {
    MoveList.moves[MoveList.count++] = move;
}

static inline bool isSquareAttacked(int square, int side) noexcept {
    if (side == White) {
        if (PawnAttacks[Black][square] & bitboards[P]) return true;
        if (KnightAttacks[square] & bitboards[N]) return true;
        if (KingAttacks[square] & bitboards[K]) return true;
        if (getBishopAttacks(square, occupancies[Both]) & (bitboards[B] | bitboards[Q])) return true;
        if (getRookAttacks(square, occupancies[Both]) & (bitboards[R] | bitboards[Q])) return true;
    } else {
        if (PawnAttacks[White][square] & bitboards[p]) return true;
        if (KnightAttacks[square] & bitboards[n]) return true;
        if (KingAttacks[square] & bitboards[k]) return true;
        if (getBishopAttacks(square, occupancies[Both]) & (bitboards[b] | bitboards[q])) return true;
        if (getRookAttacks(square, occupancies[Both]) & (bitboards[r] | bitboards[q])) return true;
    }
    return false;
}

#define saveBoard()                                                     \
    Bitboard bitboards_copy[12], occupancies_copy[3];                   \
    int mailbox_copy[64];                                               \
    int side_copy = sideToMove, enpassant_copy = epsq;                  \
    int castle_copy = castle, fifty_copy = fifty;                       \
    uint64_t hash_key_copy = hash_key;                                  \
    uint64_t pawn_key_copy = pawn_key;                                  \
    uint64_t non_pawn_key_copy[2] = {non_pawn_key[0], non_pawn_key[1]}; \
    std::memcpy(bitboards_copy, bitboards, sizeof(bitboards));          \
    std::memcpy(occupancies_copy, occupancies, sizeof(occupancies));    \
    std::memcpy(mailbox_copy, mailbox, sizeof(mailbox));                \
    NNUE_SAVE

#define takeBack(move)                                                  \
    std::memcpy(bitboards, bitboards_copy, sizeof(bitboards));          \
    std::memcpy(occupancies, occupancies_copy, sizeof(occupancies));    \
    std::memcpy(mailbox, mailbox_copy, sizeof(mailbox));                \
    sideToMove = side_copy;                                             \
    epsq = enpassant_copy;                                              \
    castle = castle_copy;                                               \
    fifty = fifty_copy;                                                 \
    hash_key = hash_key_copy;                                           \
    pawn_key = pawn_key_copy;                                           \
    non_pawn_key[0] = non_pawn_key_copy[0];                             \
    non_pawn_key[1] = non_pawn_key_copy[1];

static inline int makeMove(int move) noexcept {
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

    // Handle capture first so mailbox[toSq] is still intact
    if (capture) {
        fifty = 0;
        if (ep) {
            const int capSq = (Us == White) ? (toSq + 8) : (toSq - 8);
            const int capPawn = (Us == White) ? p : P;
            popBit(bitboards[capPawn], capSq);
            popBit(occupancies[Them], capSq);
            mailbox[capSq] = NoPiece;
            hash_key ^= piece_keys[capPawn][capSq];
            pawn_key ^= piece_keys[capPawn][capSq];
        } else {
            const int capPiece = mailbox[toSq];
            if (capPiece != NoPiece) {
                popBit(bitboards[capPiece], toSq);
                popBit(occupancies[Them], toSq);
                hash_key ^= piece_keys[capPiece][toSq];
                if (capPiece == P || capPiece == p)
                    pawn_key ^= piece_keys[capPiece][toSq];
                else
                    non_pawn_key[Them] ^= piece_keys[capPiece][toSq];
            }
        }
    }

    // Move piece from source to target
    popBit(bitboards[piece], fromSq);
    setBit(bitboards[piece], toSq);
    popBit(occupancies[Us], fromSq);
    setBit(occupancies[Us], toSq);
    mailbox[fromSq] = NoPiece;
    mailbox[toSq] = piece;

    hash_key ^= piece_keys[piece][fromSq] ^ piece_keys[piece][toSq];
    if (piece == P || piece == p) {
        pawn_key ^= piece_keys[piece][fromSq] ^ piece_keys[piece][toSq];
        fifty = 0;
    } else {
        non_pawn_key[Us] ^= piece_keys[piece][fromSq] ^ piece_keys[piece][toSq];
        if (!capture)
            fifty++;
    }

    // Handle castling rook movement
    if (castling) {
        switch (toSq) {
            case g1:
                popBit(bitboards[R], h1); setBit(bitboards[R], f1);
                popBit(occupancies[White], h1); setBit(occupancies[White], f1);
                mailbox[h1] = NoPiece; mailbox[f1] = R;
                hash_key ^= piece_keys[R][h1] ^ piece_keys[R][f1];
                non_pawn_key[White] ^= piece_keys[R][h1] ^ piece_keys[R][f1];
                break;
            case c1:
                popBit(bitboards[R], a1); setBit(bitboards[R], d1);
                popBit(occupancies[White], a1); setBit(occupancies[White], d1);
                mailbox[a1] = NoPiece; mailbox[d1] = R;
                hash_key ^= piece_keys[R][a1] ^ piece_keys[R][d1];
                non_pawn_key[White] ^= piece_keys[R][a1] ^ piece_keys[R][d1];
                break;
            case g8:
                popBit(bitboards[r], h8); setBit(bitboards[r], f8);
                popBit(occupancies[Black], h8); setBit(occupancies[Black], f8);
                mailbox[h8] = NoPiece; mailbox[f8] = r;
                hash_key ^= piece_keys[r][h8] ^ piece_keys[r][f8];
                non_pawn_key[Black] ^= piece_keys[r][h8] ^ piece_keys[r][f8];
                break;
            case c8:
                popBit(bitboards[r], a8); setBit(bitboards[r], d8);
                popBit(occupancies[Black], a8); setBit(occupancies[Black], d8);
                mailbox[a8] = NoPiece; mailbox[d8] = r;
                hash_key ^= piece_keys[r][a8] ^ piece_keys[r][d8];
                non_pawn_key[Black] ^= piece_keys[r][a8] ^ piece_keys[r][d8];
                break;
        }
    }

    // Handle pawn promotions
    if (promo) {
        const int pawn = (Us == White) ? P : p;
        popBit(bitboards[pawn], toSq);
        setBit(bitboards[promo], toSq);
        mailbox[toSq] = promo;
        hash_key ^= piece_keys[pawn][toSq] ^ piece_keys[promo][toSq];
        pawn_key ^= piece_keys[pawn][toSq];
        non_pawn_key[Us] ^= piece_keys[promo][toSq];
    }

    if (epsq != NoSq)
        hash_key ^= enpassant_keys[epsq];
    epsq = NoSq;

    if (dpush) {
        epsq = (Us == White) ? (toSq + 8) : (toSq - 8);
        hash_key ^= enpassant_keys[epsq];
    }

    hash_key ^= castle_keys[castle];
    castle &= castling_rights[fromSq];
    castle &= castling_rights[toSq];
    hash_key ^= castle_keys[castle];

    occupancies[Both] = occupancies[White] | occupancies[Black];

    sideToMove = Them;
    hash_key ^= side_key;

    const int kingSq = ls1b((Us == White) ? bitboards[K] : bitboards[k]);
    return !isSquareAttacked(kingSq, Them);
}

#endif // MOVGEN_H
