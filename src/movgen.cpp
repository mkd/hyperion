/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#include "movgen.h"
#include "bitboard.h"
#include "position.h"
#include <cassert>
#include <iostream>

using namespace std;

void generateMoves(MoveList_t &MoveList) {
    MoveList.count = 0;

    const Bitboard occWhite = occupancies[White];
    const Bitboard occBlack = occupancies[Black];
    const Bitboard occBoth  = occupancies[Both];

    if (sideToMove == White) {
        // 1. White Pawns
        Bitboard pawns = bitboards[P];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq - 8;

            if (toSq >= a8 && !(occBoth & SqBB[toSq])) {
                if (SqBB[toSq] & Rank8_Mask) {
                    addMove(MoveList, encodeMove(fromSq, toSq, P, Q, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, P, R, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, P, B, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, P, N, 0, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, toSq, P, 0, 0, 0, 0, 0));
                    if ((SqBB[fromSq] & Rank2_Mask) && !(occBoth & SqBB[toSq - 8]))
                        addMove(MoveList, encodeMove(fromSq, toSq - 8, P, 0, 0, 1, 0, 0));
                }
            }

            Bitboard attacks = PawnAttacks[White][fromSq] & occBlack;
            while (attacks) {
                const int capSq = popLsb(attacks);
                if (SqBB[capSq] & Rank8_Mask) {
                    addMove(MoveList, encodeMove(fromSq, capSq, P, Q, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, R, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, B, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, N, 1, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, capSq, P, 0, 1, 0, 0, 0));
                }
            }

            if (epsq != NoSq && (PawnAttacks[White][fromSq] & SqBB[epsq])) {
                addMove(MoveList, encodeMove(fromSq, epsq, P, 0, 1, 0, 1, 0));
            }
        }

        // 2. White Knights
        Bitboard knights = bitboards[N];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard moves = KnightAttacks[fromSq] & ~occWhite;
            Bitboard caps = moves & occBlack;
            Bitboard quiets = moves & ~occBlack;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), N, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), N, 0, 0, 0, 0, 0));
        }

        // 3. White Bishops
        Bitboard bishops = bitboards[B];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard moves = getBishopAttacks(fromSq, occBoth) & ~occWhite;
            Bitboard caps = moves & occBlack;
            Bitboard quiets = moves & ~occBlack;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), B, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), B, 0, 0, 0, 0, 0));
        }

        // 4. White Rooks
        Bitboard rooks = bitboards[R];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard moves = getRookAttacks(fromSq, occBoth) & ~occWhite;
            Bitboard caps = moves & occBlack;
            Bitboard quiets = moves & ~occBlack;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), R, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), R, 0, 0, 0, 0, 0));
        }

        // 5. White Queens
        Bitboard queens = bitboards[Q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard moves = getQueenAttacks(fromSq, occBoth) & ~occWhite;
            Bitboard caps = moves & occBlack;
            Bitboard quiets = moves & ~occBlack;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), Q, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), Q, 0, 0, 0, 0, 0));
        }

        // 6. White King
        Bitboard king = bitboards[K];
        if (king) {
            const int fromSq = ls1b(king);
            Bitboard moves = KingAttacks[fromSq] & ~occWhite;
            Bitboard caps = moves & occBlack;
            Bitboard quiets = moves & ~occBlack;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), K, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), K, 0, 0, 0, 0, 0));

            if ((castle & wk) && !(FG1_Mask & occBoth)) {
                if (!isSquareAttacked(e1, Black) && !isSquareAttacked(f1, Black) && !isSquareAttacked(g1, Black))
                    addMove(MoveList, encodeMove(e1, g1, K, 0, 0, 0, 0, 1));
            }
            if ((castle & wq) && !(DCB1_Mask & occBoth)) {
                if (!isSquareAttacked(e1, Black) && !isSquareAttacked(d1, Black) && !isSquareAttacked(c1, Black))
                    addMove(MoveList, encodeMove(e1, c1, K, 0, 0, 0, 0, 1));
            }
        }
    } else {
        // 1. Black Pawns
        Bitboard pawns = bitboards[p];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq + 8;

            if (toSq <= h1 && !(occBoth & SqBB[toSq])) {
                if (SqBB[toSq] & Rank1_Mask) {
                    addMove(MoveList, encodeMove(fromSq, toSq, p, q, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, p, r, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, p, b, 0, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, toSq, p, n, 0, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, toSq, p, 0, 0, 0, 0, 0));
                    if ((SqBB[fromSq] & Rank7_Mask) && !(occBoth & SqBB[toSq + 8]))
                        addMove(MoveList, encodeMove(fromSq, toSq + 8, p, 0, 0, 1, 0, 0));
                }
            }

            Bitboard attacks = PawnAttacks[Black][fromSq] & occWhite;
            while (attacks) {
                const int capSq = popLsb(attacks);
                if (SqBB[capSq] & Rank1_Mask) {
                    addMove(MoveList, encodeMove(fromSq, capSq, p, q, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, r, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, b, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, n, 1, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, capSq, p, 0, 1, 0, 0, 0));
                }
            }

            if (epsq != NoSq && (PawnAttacks[Black][fromSq] & SqBB[epsq])) {
                addMove(MoveList, encodeMove(fromSq, epsq, p, 0, 1, 0, 1, 0));
            }
        }

        // 2. Black Knights
        Bitboard knights = bitboards[n];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard moves = KnightAttacks[fromSq] & ~occBlack;
            Bitboard caps = moves & occWhite;
            Bitboard quiets = moves & ~occWhite;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), n, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), n, 0, 0, 0, 0, 0));
        }

        // 3. Black Bishops
        Bitboard bishops = bitboards[b];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard moves = getBishopAttacks(fromSq, occBoth) & ~occBlack;
            Bitboard caps = moves & occWhite;
            Bitboard quiets = moves & ~occWhite;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), b, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), b, 0, 0, 0, 0, 0));
        }

        // 4. Black Rooks
        Bitboard rooks = bitboards[r];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard moves = getRookAttacks(fromSq, occBoth) & ~occBlack;
            Bitboard caps = moves & occWhite;
            Bitboard quiets = moves & ~occWhite;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), r, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), r, 0, 0, 0, 0, 0));
        }

        // 5. Black Queens
        Bitboard queens = bitboards[q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard moves = getQueenAttacks(fromSq, occBoth) & ~occBlack;
            Bitboard caps = moves & occWhite;
            Bitboard quiets = moves & ~occWhite;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), q, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), q, 0, 0, 0, 0, 0));
        }

        // 6. Black King
        Bitboard king = bitboards[k];
        if (king) {
            const int fromSq = ls1b(king);
            Bitboard moves = KingAttacks[fromSq] & ~occBlack;
            Bitboard caps = moves & occWhite;
            Bitboard quiets = moves & ~occWhite;
            while (caps)   addMove(MoveList, encodeMove(fromSq, popLsb(caps), k, 0, 1, 0, 0, 0));
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), k, 0, 0, 0, 0, 0));

            if ((castle & bk) && !(FG8_Mask & occBoth)) {
                if (!isSquareAttacked(e8, White) && !isSquareAttacked(f8, White) && !isSquareAttacked(g8, White))
                    addMove(MoveList, encodeMove(e8, g8, k, 0, 0, 0, 0, 1));
            }
            if ((castle & bq) && !(DCB8_Mask & occBoth)) {
                if (!isSquareAttacked(e8, White) && !isSquareAttacked(d8, White) && !isSquareAttacked(c8, White))
                    addMove(MoveList, encodeMove(e8, c8, k, 0, 0, 0, 0, 1));
            }
        }
    }
}

void generateCapturesAndPromotions(MoveList_t &MoveList) {
    MoveList.count = 0;

    const Bitboard occWhite = occupancies[White];
    const Bitboard occBlack = occupancies[Black];
    const Bitboard occBoth  = occupancies[Both];

    if (sideToMove == White) {
        Bitboard pawns = bitboards[P];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq - 8;

            if (toSq >= a8 && (SqBB[toSq] & Rank8_Mask) && !(occBoth & SqBB[toSq])) {
                addMove(MoveList, encodeMove(fromSq, toSq, P, Q, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, P, R, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, P, B, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, P, N, 0, 0, 0, 0));
            }

            Bitboard attacks = PawnAttacks[White][fromSq] & occBlack;
            while (attacks) {
                const int capSq = popLsb(attacks);
                if (SqBB[capSq] & Rank8_Mask) {
                    addMove(MoveList, encodeMove(fromSq, capSq, P, Q, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, R, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, B, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, P, N, 1, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, capSq, P, 0, 1, 0, 0, 0));
                }
            }

            if (epsq != NoSq && (PawnAttacks[White][fromSq] & SqBB[epsq])) {
                addMove(MoveList, encodeMove(fromSq, epsq, P, 0, 1, 0, 1, 0));
            }
        }

        Bitboard knights = bitboards[N];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard caps = KnightAttacks[fromSq] & occBlack;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), N, 0, 1, 0, 0, 0));
        }

        Bitboard bishops = bitboards[B];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard caps = getBishopAttacks(fromSq, occBoth) & occBlack;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), B, 0, 1, 0, 0, 0));
        }

        Bitboard rooks = bitboards[R];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard caps = getRookAttacks(fromSq, occBoth) & occBlack;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), R, 0, 1, 0, 0, 0));
        }

        Bitboard queens = bitboards[Q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard caps = getQueenAttacks(fromSq, occBoth) & occBlack;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), Q, 0, 1, 0, 0, 0));
        }

        if (bitboards[K]) {
            const int fromSq = ls1b(bitboards[K]);
            Bitboard caps = KingAttacks[fromSq] & occBlack;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), K, 0, 1, 0, 0, 0));
        }
    } else {
        Bitboard pawns = bitboards[p];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq + 8;

            if (toSq <= h1 && (SqBB[toSq] & Rank1_Mask) && !(occBoth & SqBB[toSq])) {
                addMove(MoveList, encodeMove(fromSq, toSq, p, q, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, p, r, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, p, b, 0, 0, 0, 0));
                addMove(MoveList, encodeMove(fromSq, toSq, p, n, 0, 0, 0, 0));
            }

            Bitboard attacks = PawnAttacks[Black][fromSq] & occWhite;
            while (attacks) {
                const int capSq = popLsb(attacks);
                if (SqBB[capSq] & Rank1_Mask) {
                    addMove(MoveList, encodeMove(fromSq, capSq, p, q, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, r, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, b, 1, 0, 0, 0));
                    addMove(MoveList, encodeMove(fromSq, capSq, p, n, 1, 0, 0, 0));
                } else {
                    addMove(MoveList, encodeMove(fromSq, capSq, p, 0, 1, 0, 0, 0));
                }
            }

            if (epsq != NoSq && (PawnAttacks[Black][fromSq] & SqBB[epsq])) {
                addMove(MoveList, encodeMove(fromSq, epsq, p, 0, 1, 0, 1, 0));
            }
        }

        Bitboard knights = bitboards[n];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard caps = KnightAttacks[fromSq] & occWhite;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), n, 0, 1, 0, 0, 0));
        }

        Bitboard bishops = bitboards[b];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard caps = getBishopAttacks(fromSq, occBoth) & occWhite;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), b, 0, 1, 0, 0, 0));
        }

        Bitboard rooks = bitboards[r];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard caps = getRookAttacks(fromSq, occBoth) & occWhite;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), r, 0, 1, 0, 0, 0));
        }

        Bitboard queens = bitboards[q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard caps = getQueenAttacks(fromSq, occBoth) & occWhite;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), q, 0, 1, 0, 0, 0));
        }

        if (bitboards[k]) {
            const int fromSq = ls1b(bitboards[k]);
            Bitboard caps = KingAttacks[fromSq] & occWhite;
            while (caps) addMove(MoveList, encodeMove(fromSq, popLsb(caps), k, 0, 1, 0, 0, 0));
        }
    }
}

void generateQuietMoves(MoveList_t &MoveList) {
    MoveList.count = 0;
    const Bitboard occBoth = occupancies[Both];

    if (sideToMove == White) {
        Bitboard pawns = bitboards[P];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq - 8;
            if (toSq >= a8 && !(SqBB[toSq] & Rank8_Mask) && !(occBoth & SqBB[toSq])) {
                addMove(MoveList, encodeMove(fromSq, toSq, P, 0, 0, 0, 0, 0));
                if ((SqBB[fromSq] & Rank2_Mask) && !(occBoth & SqBB[toSq - 8]))
                    addMove(MoveList, encodeMove(fromSq, toSq - 8, P, 0, 0, 1, 0, 0));
            }
        }

        Bitboard knights = bitboards[N];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard quiets = KnightAttacks[fromSq] & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), N, 0, 0, 0, 0, 0));
        }

        Bitboard bishops = bitboards[B];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard quiets = getBishopAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), B, 0, 0, 0, 0, 0));
        }

        Bitboard rooks = bitboards[R];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard quiets = getRookAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), R, 0, 0, 0, 0, 0));
        }

        Bitboard queens = bitboards[Q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard quiets = getQueenAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), Q, 0, 0, 0, 0, 0));
        }

        if (bitboards[K]) {
            const int fromSq = ls1b(bitboards[K]);
            Bitboard quiets = KingAttacks[fromSq] & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), K, 0, 0, 0, 0, 0));

            if ((castle & wk) && !(FG1_Mask & occBoth)) {
                if (!isSquareAttacked(e1, Black) && !isSquareAttacked(f1, Black) && !isSquareAttacked(g1, Black))
                    addMove(MoveList, encodeMove(e1, g1, K, 0, 0, 0, 0, 1));
            }
            if ((castle & wq) && !(DCB1_Mask & occBoth)) {
                if (!isSquareAttacked(e1, Black) && !isSquareAttacked(d1, Black) && !isSquareAttacked(c1, Black))
                    addMove(MoveList, encodeMove(e1, c1, K, 0, 0, 0, 0, 1));
            }
        }
    } else {
        Bitboard pawns = bitboards[p];
        while (pawns) {
            const int fromSq = popLsb(pawns);
            const int toSq = fromSq + 8;
            if (toSq <= h1 && !(SqBB[toSq] & Rank1_Mask) && !(occBoth & SqBB[toSq])) {
                addMove(MoveList, encodeMove(fromSq, toSq, p, 0, 0, 0, 0, 0));
                if ((SqBB[fromSq] & Rank7_Mask) && !(occBoth & SqBB[toSq + 8]))
                    addMove(MoveList, encodeMove(fromSq, toSq + 8, p, 0, 0, 1, 0, 0));
            }
        }

        Bitboard knights = bitboards[n];
        while (knights) {
            const int fromSq = popLsb(knights);
            Bitboard quiets = KnightAttacks[fromSq] & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), n, 0, 0, 0, 0, 0));
        }

        Bitboard bishops = bitboards[b];
        while (bishops) {
            const int fromSq = popLsb(bishops);
            Bitboard quiets = getBishopAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), b, 0, 0, 0, 0, 0));
        }

        Bitboard rooks = bitboards[r];
        while (rooks) {
            const int fromSq = popLsb(rooks);
            Bitboard quiets = getRookAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), r, 0, 0, 0, 0, 0));
        }

        Bitboard queens = bitboards[q];
        while (queens) {
            const int fromSq = popLsb(queens);
            Bitboard quiets = getQueenAttacks(fromSq, occBoth) & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), q, 0, 0, 0, 0, 0));
        }

        if (bitboards[k]) {
            const int fromSq = ls1b(bitboards[k]);
            Bitboard quiets = KingAttacks[fromSq] & ~occBoth;
            while (quiets) addMove(MoveList, encodeMove(fromSq, popLsb(quiets), k, 0, 0, 0, 0, 0));

            if ((castle & bk) && !(FG8_Mask & occBoth)) {
                if (!isSquareAttacked(e8, White) && !isSquareAttacked(f8, White) && !isSquareAttacked(g8, White))
                    addMove(MoveList, encodeMove(e8, g8, k, 0, 0, 0, 0, 1));
            }
            if ((castle & bq) && !(DCB8_Mask & occBoth)) {
                if (!isSquareAttacked(e8, White) && !isSquareAttacked(d8, White) && !isSquareAttacked(c8, White))
                    addMove(MoveList, encodeMove(e8, c8, k, 0, 0, 0, 0, 1));
            }
        }
    }
}

void printMoveList(MoveList_t &MoveList) {
    if (!MoveList.count) {
        cout << endl << "     No moves in the move list!" << endl;
        return;
    }

    cout << endl << "     move    piece   capture   double    enpass    castling" << endl << endl;

    for (int i = 0; i < MoveList.count; i++) {
        int move = MoveList.moves[i];
        cout << "     " << SquareToCoordinates[getMoveSource(move)]
             << SquareToCoordinates[getMoveTarget(move)]
             << (getPromo(move) ? promoChar(getPromo(move)) : ' ')
             << "    " << PieceStr[getMovePiece(move)]
             << "       " << (getMoveCapture(move) ? 1 : 0)
             << "         " << (getDoublePush(move) ? 1 : 0)
             << "         " << (getEp(move) ? 1 : 0)
             << "         " << (getCastle(move) ? 1 : 0) << endl;
    }

    cout << endl << "     Total number of moves: " << MoveList.count << endl << endl;
}
