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
 * @file movgen.cpp
 * @brief Pseudo-legal move generation.
 *
 * A single colour-templated generator (`generate<Us, Mode>`) produces the
 * three public move lists (all moves, captures/promotions, quiet moves).  The
 * emission order - pawns, knights, bishops, rooks, queens, king, castling -
 * is part of the engine's behaviour (it breaks move-ordering ties) and must
 * be preserved.
 */

#include "movgen.h"

#include "bitboard.h"
#include "position.h"

#include <cassert>
#include <iostream>

namespace {

/// Which subset of pseudo-legal moves a generator call produces.
enum class GenMode { All, CapturesAndPromotions, Quiets };

/// Compile-time constants that differ between White and Black.
template <Side Us>
struct SideTraits {
    static constexpr Side Them     = (Us == White) ? Black : White;
    static constexpr int  Offset   = (Us == White) ? 0 : 6;         ///< Piece-code offset (P vs p).
    static constexpr int  Push     = (Us == White) ? -8 : 8;        ///< Pawn push delta (a8 = 0 numbering).
    static constexpr int  Base     = (Us == White) ? a1 : a8;       ///< First square of the home rank.
    static constexpr Bitboard PromoRank = (Us == White) ? Rank8_Mask : Rank1_Mask;
    static constexpr Bitboard StartRank = (Us == White) ? Rank2_Mask : Rank7_Mask;

    static constexpr int KingSideRight  = (Us == White) ? wk : bk;
    static constexpr int QueenSideRight = (Us == White) ? wq : bq;
    static constexpr Bitboard KingSideEmpty  = (Us == White) ? FG1_Mask : FG8_Mask;
    static constexpr Bitboard QueenSideEmpty = (Us == White) ? DCB1_Mask : DCB8_Mask;

    // Castling squares on the home rank.
    static constexpr int C = Base + 2;
    static constexpr int D = Base + 3;
    static constexpr int E = Base + 4;
    static constexpr int F = Base + 5;
    static constexpr int G = Base + 6;
};

/// Appends the four promotion moves (Q, R, B, N) of a pawn move.
template <Side Us>
inline void addPromotions(MoveList_t &list, int from, int to, int capture) noexcept {
    constexpr int off = SideTraits<Us>::Offset;
    addMove(list, encodeMove(from, to, P + off, Q + off, capture, 0, 0, 0));
    addMove(list, encodeMove(from, to, P + off, R + off, capture, 0, 0, 0));
    addMove(list, encodeMove(from, to, P + off, B + off, capture, 0, 0, 0));
    addMove(list, encodeMove(from, to, P + off, N + off, capture, 0, 0, 0));
}

/// Generates pawn pushes, double pushes, captures, promotions and en passant.
template <Side Us, GenMode Mode>
void generatePawnMoves(MoveList_t &list, [[maybe_unused]] Bitboard occThem, Bitboard occBoth) noexcept {
    using T = SideTraits<Us>;
    constexpr bool WantCaptures = (Mode != GenMode::Quiets);
    constexpr bool WantQuiets   = (Mode != GenMode::CapturesAndPromotions);

    Bitboard pawns = bitboards[P + T::Offset];
    while (pawns) {
        const int from = popLsb(pawns);
        const int to   = from + T::Push;

        // Pushes (promotions count as "captures" for move-generation purposes).
        if (to >= a8 && to <= h1 && !(occBoth & SqBB[to])) {
            if (SqBB[to] & T::PromoRank) {
                if constexpr (WantCaptures)
                    addPromotions<Us>(list, from, to, 0);
            } else if constexpr (WantQuiets) {
                addMove(list, encodeMove(from, to, P + T::Offset, 0, 0, 0, 0, 0));
                if ((SqBB[from] & T::StartRank) && !(occBoth & SqBB[to + T::Push]))
                    addMove(list, encodeMove(from, to + T::Push, P + T::Offset, 0, 0, 1, 0, 0));
            }
        }

        if constexpr (WantCaptures) {
            Bitboard attacks = PawnAttacks[Us][from] & occThem;
            while (attacks) {
                const int capSq = popLsb(attacks);
                if (SqBB[capSq] & T::PromoRank)
                    addPromotions<Us>(list, from, capSq, 1);
                else
                    addMove(list, encodeMove(from, capSq, P + T::Offset, 0, 1, 0, 0, 0));
            }

            if (epsq != NoSq && (PawnAttacks[Us][from] & SqBB[epsq]))
                addMove(list, encodeMove(from, epsq, P + T::Offset, 0, 1, 0, 1, 0));
        }
    }
}

/// Emits the captures and/or quiet moves of one piece given its attack set.
template <Side Us, GenMode Mode>
inline void emitPieceMoves(MoveList_t &list, int piece, int from, Bitboard attacks,
                           [[maybe_unused]] Bitboard occThem, [[maybe_unused]] Bitboard occBoth) noexcept {
    constexpr bool WantCaptures = (Mode != GenMode::Quiets);
    constexpr bool WantQuiets   = (Mode != GenMode::CapturesAndPromotions);

    if constexpr (WantCaptures) {
        Bitboard caps = attacks & occThem;
        while (caps)
            addMove(list, encodeMove(from, popLsb(caps), piece, 0, 1, 0, 0, 0));
    }
    if constexpr (WantQuiets) {
        Bitboard quiets = attacks & ~occBoth;
        while (quiets)
            addMove(list, encodeMove(from, popLsb(quiets), piece, 0, 0, 0, 0, 0));
    }
}

/// Generates the moves of `Us` selected by `Mode`, in the canonical order.
template <Side Us, GenMode Mode>
void generate(MoveList_t &list) noexcept {
    using T = SideTraits<Us>;
    constexpr int off = T::Offset;

    list.count = 0;

    const Bitboard occThem = occupancies[T::Them];
    const Bitboard occBoth = occupancies[Both];

    generatePawnMoves<Us, Mode>(list, occThem, occBoth);

    Bitboard knights = bitboards[N + off];
    while (knights) {
        const int from = popLsb(knights);
        emitPieceMoves<Us, Mode>(list, N + off, from, KnightAttacks[from], occThem, occBoth);
    }

    Bitboard bishops = bitboards[B + off];
    while (bishops) {
        const int from = popLsb(bishops);
        emitPieceMoves<Us, Mode>(list, B + off, from, getBishopAttacks(from, occBoth), occThem, occBoth);
    }

    Bitboard rooks = bitboards[R + off];
    while (rooks) {
        const int from = popLsb(rooks);
        emitPieceMoves<Us, Mode>(list, R + off, from, getRookAttacks(from, occBoth), occThem, occBoth);
    }

    Bitboard queens = bitboards[Q + off];
    while (queens) {
        const int from = popLsb(queens);
        emitPieceMoves<Us, Mode>(list, Q + off, from, getQueenAttacks(from, occBoth), occThem, occBoth);
    }

    if (const Bitboard king = bitboards[K + off]; king) {
        const int from = static_cast<int>(ls1b(king));
        emitPieceMoves<Us, Mode>(list, K + off, from, KingAttacks[from], occThem, occBoth);

        // Castling is a quiet move: the squares between king and rook must be
        // empty and the king may not start on, cross or land on an attacked square.
        if constexpr (Mode != GenMode::CapturesAndPromotions) {
            if ((castle & T::KingSideRight) && !(T::KingSideEmpty & occBoth)) {
                if (!isSquareAttacked(T::E, T::Them) && !isSquareAttacked(T::F, T::Them) &&
                    !isSquareAttacked(T::G, T::Them))
                    addMove(list, encodeMove(T::E, T::G, K + off, 0, 0, 0, 0, 1));
            }
            if ((castle & T::QueenSideRight) && !(T::QueenSideEmpty & occBoth)) {
                if (!isSquareAttacked(T::E, T::Them) && !isSquareAttacked(T::D, T::Them) &&
                    !isSquareAttacked(T::C, T::Them))
                    addMove(list, encodeMove(T::E, T::C, K + off, 0, 0, 0, 0, 1));
            }
        }
    }
}

/// Dispatches on the side to move.
template <GenMode Mode>
inline void generateForSideToMove(MoveList_t &list) noexcept {
    if (sideToMove == White)
        generate<White, Mode>(list);
    else
        generate<Black, Mode>(list);
}

} // namespace

void generateMoves(MoveList_t &moveList) {
    generateForSideToMove<GenMode::All>(moveList);
}

void generateCapturesAndPromotions(MoveList_t &moveList) {
    generateForSideToMove<GenMode::CapturesAndPromotions>(moveList);
}

void generateQuietMoves(MoveList_t &moveList) {
    generateForSideToMove<GenMode::Quiets>(moveList);
}

void printMoveList(MoveList_t &moveList) {
    using std::cout, std::endl;

    if (!moveList.count) {
        cout << endl << "     No moves in the move list!" << endl;
        return;
    }

    cout << endl << "     move    piece   capture   double    enpass    castling" << endl << endl;

    for (int i = 0; i < moveList.count; i++) {
        const int move = moveList.moves[i];
        cout << "     " << SquareToCoordinates[getMoveSource(move)]
             << SquareToCoordinates[getMoveTarget(move)]
             << (getPromo(move) ? promoChar(getPromo(move)) : ' ')
             << "    " << PieceStr[getMovePiece(move)]
             << "       " << (getMoveCapture(move) ? 1 : 0)
             << "         " << (getDoublePush(move) ? 1 : 0)
             << "         " << (getEp(move) ? 1 : 0)
             << "         " << (getCastle(move) ? 1 : 0) << endl;
    }

    cout << endl << "     Total number of moves: " << moveList.count << endl << endl;
}
